#include "stdafx.h"
#include <boost/python.hpp>
#include <boost/python/module.hpp>
#include <boost/python/def.hpp>

#include <boost/bind.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/enable_shared_from_this.hpp>
#include <boost/asio.hpp>
#include <iostream>
#include <string>
#include <sstream>
#include<boost/unordered_map.hpp>
#include <boost/uuid/uuid.hpp>            // uuid class
#include <boost/uuid/uuid_generators.hpp> // generators
#include <boost/uuid/uuid_io.hpp>         // streaming operators etc.
#include <boost/thread/thread.hpp>
#include<stdio.h>
#include<fstream>
#include<iomanip>
#include <boost/thread/thread.hpp>
#include<map>
#include <boost/atomic.hpp> 
#include<windows.h>  
#include <boost/thread/mutex.hpp>

using namespace boost::python;
using boost::asio::ip::tcp;
PyObject *on_connect;
PyObject *on_disconnect;
PyObject *on_message;
PyObject *on_py_log;

void py_call(PyObject *callable) {
	call<void>(callable);
}

void py_log(std::string msg) {
	call<void>(on_py_log, msg);
}

void py_on_connect(int conn_id) {
	call<void>(on_connect, conn_id);
}

void py_on_disconnect(int conn_id) {
	call<void>(on_disconnect, conn_id);
}

void py_on_message(int conn_id, int m, str buf) {
	call<void>(on_message, conn_id, m, buf);
}


class io_context_pool
	: private boost::noncopyable
{
public:
	/// Construct the io_context pool.
	explicit io_context_pool(std::size_t pool_size) : next_io_context_(0)
	{
		if (pool_size == 0)
			throw std::runtime_error("io_context_pool size is 0");

		// Give all the io_contexts work to do so that their run() functions will not
		// exit until they are explicitly stopped.
		for (std::size_t i = 0; i < pool_size; ++i)
		{
			io_context_ptr io_context(new boost::asio::io_context);
			io_contexts_.push_back(io_context);
			work_.push_back(boost::asio::make_work_guard(*io_context));
		}
	}

	/// Run all io_context objects in the pool.
	void run() {
		// Create a pool of threads to run all of the io_contexts.
		std::vector<boost::shared_ptr<boost::thread>> threads;
		for (std::size_t i = 0; i < io_contexts_.size(); ++i)
		{
			boost::shared_ptr<boost::thread> thread(new boost::thread(
				boost::bind(&boost::asio::io_context::run, io_contexts_[i])));
			threads.push_back(thread);
		}

		// Wait for all threads in the pool to exit.
		for (std::size_t i = 0; i < threads.size(); ++i)
			threads[i]->join();
	}

	/// Stop all io_context objects in the pool.
	void stop() {
		// Explicitly stop all io_contexts.
		for (std::size_t i = 0; i < io_contexts_.size(); ++i)
			io_contexts_[i]->stop();
	}

	/// Get an io_context to use.
	boost::asio::io_context& get_io_context() {
		// Use a round-robin scheme to choose the next io_context to use.
		boost::asio::io_context& io_context = *io_contexts_[next_io_context_];
		++next_io_context_;
		if (next_io_context_ == io_contexts_.size())
			next_io_context_ = 0;
		return io_context;
	}

private:
	typedef boost::shared_ptr<boost::asio::io_context> io_context_ptr;
	typedef boost::asio::executor_work_guard<
		boost::asio::io_context::executor_type> io_context_work;

	/// The pool of io_contexts.
	std::vector<io_context_ptr> io_contexts_;

	/// The work that keeps the io_contexts running.
	std::list<io_context_work> work_;

	/// The next io_context to use for a connection.
	std::size_t next_io_context_;
};

class server
{
	class conn 
	{
	public:
		server* m_server;
		conn(server* sv, int conn_id)
			:m_socket(*sv->m_io_service){
			m_server = sv;
			_id = conn_id;
			is_free = true;
			m_py_service = m_server->m_py_service;
			m_io_service = m_server->m_io_service;
		};

		int _id;
		int get_id() {
			return _id;
		}

		boost::atomic<bool> is_free;



		tcp::socket& socket() {
			return m_socket;
		}

		tcp::socket  m_socket;
		enum { head_length = 4, packet_length = 8192 };
		//BYTE m_head[head_length];
		//char m_body[packet_length];

		int m_msg_type;
		int m_body_len;

		boost::asio::io_service* m_py_service;
		boost::asio::io_service* m_io_service;

		void recv() {
			is_free = false;
			char* head = new char[head_length];
			boost::asio::async_read(
				m_socket,
				boost::asio::buffer(head, head_length),
				boost::bind(
					&conn::handle_read_head,
					this,
					head,
					_1,
					_2
				)
			);
		}

		void handle_read_head(char* head,const boost::system::error_code& error, size_t bytes_transferred) {
			if (error)
			{
				close();
			}
			else {
				int length = (head[1] & 0x000000ff) << 8 | (head[0] & 0x000000ff);
				int total_len = length + 2;
				m_msg_type = (head[3] & 0x000000ff) << 8 | (head[2] & 0x000000ff);
				m_body_len = total_len - 4;
				char* body = new char[m_body_len];
				boost::asio::async_read(
					m_socket,
					boost::asio::buffer(body, m_body_len),
					boost::bind(
						&conn::handle_read_body,
						this,
						body,
						_1,
						_2
					)
				);
			}
			delete[] head;
		}

		void handle_read_body(char* body,const boost::system::error_code& error, size_t bytes_transferred) {
			if (error)
			{
				close();
			}
			else {
				m_py_service->post(
					boost::bind(
						py_on_message,
						_id,
						m_msg_type,
						str(body, bytes_transferred)
					)
				);

			 /*m_msg_type = m_body_len = 0;
				memset(m_head, 0, head_length);
				memset(m_body, 0, packet_length);*/

				recv();
			}
			delete[] body;
		}

		void io_send(char* buf, int sz) {
			boost::asio::async_write(
				m_socket,
				boost::asio::buffer(buf, sz),
				boost::bind(&conn::handle_write, this, buf, _1, _2)
			);
		}

		void handle_write(char* buf,const boost::system::error_code& error, size_t bytes_transferred) {
			if (error) {
				close();
			}
			delete[] buf;
		}


		void send(int msg_type, std::string str_buf) {

			int total = 4 + str_buf.size();
			int length = total - 2;
			char head[4] = {
				(length >> 0) & 0xFF,
				(length >> 8) & 0xFF,
				(msg_type >> 0) & 0xFF,
				(msg_type >> 8) & 0xFF
			};

			std::stringstream sstr;
			sstr.clear();
			sstr << head;
			sstr << str_buf;
			/*		char* body = new char[body]*/
			char* buf = new char[total];
			sstr >> buf;

			//std::vector<boost::asio::const_buffer> buffers;
			//buffers.push_back(boost::asio::buffer(head));
			//buffers.push_back(boost::asio::buffer(*buf));
			m_io_service->post(
				boost::bind(&conn::io_send, this, buf, total)
			);
		}

	
		bool close() {
			m_py_service->post(
				boost::bind(
					py_on_disconnect,
					_id
				)
			);

			try {
				m_socket.release();
				//m_msg_type = m_body_len = 0;
				//memset(m_head, 0, head_length);
				//memset(m_body, 0, packet_length);
				is_free = true;
			}
			catch (boost::system::system_error e) {
				return false;
			}
			m_server->check();
			return true;
		}
	};


public:
	tcp::acceptor m_acceptor;

	typedef conn* conn_ptr;

	typedef boost::unordered_map<int, conn_ptr> conn_map;

	boost::atomic<int> m_conn_index;
	boost::atomic<bool> m_is_accept;
	conn_map m_all_conn;

	boost::asio::io_service* m_py_service;
	boost::asio::io_service* m_io_service;
	
	server(boost::asio::io_service*  io_service, boost::asio::io_service* py_service, int port, int max_connection)
		: m_acceptor(*io_service, tcp::endpoint(tcp::v4(), port)) {
		m_py_service = py_service;
		m_io_service = io_service;
		for (int i = 0; i < max_connection; i++) {
			m_all_conn[i] = new conn(this, i);
		}
		m_conn_index = 0;
		start_accept();
	}

	conn_ptr get_free_conn() {
		int try_get_count = 0;
		while (1) {
			if (m_conn_index == m_all_conn.size()) {
				m_conn_index = 0;
			}
			conn_ptr c = m_all_conn[m_conn_index++];
			if (c->is_free) {
				return c;
			}
			try_get_count++;
			if (try_get_count >= m_all_conn.size()) {
				break;
			}
		}
		return NULL;
	}

	void check() {
		if (!m_is_accept) {
			start_accept();
		}
	}

	void log(std::string msg) {
		m_py_service->post(
			boost::bind(py_log, msg)
		);
	}



	void start_accept() {
		conn_ptr new_connection = get_free_conn();
		if (!new_connection) {
			//没有空余连接
			m_is_accept = false;
			return;
		}

		m_is_accept = true;
		m_acceptor.async_accept(
			new_connection->socket(),
			boost::bind(&server::handle_accept, this, new_connection, _1)
		);
	}

	void recovery(int conn_id) {
		log("recovery connection");
		m_all_conn[conn_id] = new conn(this, conn_id);
	}


	void handle_accept(conn_ptr new_connection, const boost::system::error_code& error) {
		if (!error)
		{
			m_py_service->post(
				boost::bind(
					py_on_connect,
					new_connection->get_id())
			);
			new_connection->recv();
		}
		else {
			//重置连接
			new_connection->close();
			recovery(new_connection->get_id());
		}
		start_accept();
	}
};

DWORD WINAPI Worker(LPVOID lpParam)
{
	//线程函数  
	boost::asio::io_service* io = (boost::asio::io_service*)lpParam;
	boost::asio::io_service::work wk(*io);
	io->run();
	return 0;
}






struct pyiocp
{

	server* m_server;
	boost::asio::io_service* io_service;//io线程
	boost::asio::io_service* py_service;//python主线程
	boost::asio::io_service* timer_service;//计时器线程
	io_context_pool* pool;
	void init() {
		pool=new  io_context_pool(8);

		py_service = &pool->get_io_context();
		io_service = &pool->get_io_context();
		timer_service = &pool->get_io_context();
	
		//CreateThread(NULL, 0, Worker, io_service, 0, 0);
		//CreateThread(NULL, 0, Worker, timer_service, 0, 0);
//		io_service = timer_service = py_service= new boost::asio::io_service();
	} 

	void start(int port,int max_connection) {
		m_server = new server(io_service, py_service, port, max_connection);
	}

	void listen(int port, int max_connection) {

		io_service->post(
			boost::bind(
				&pyiocp::start,
				this,
				port,
				max_connection
			)
		);
		//boost::asio::io_service::work wk(*py_service);
		pool->run();
	}

	void bind(std::string m, PyObject *callable) {
		if (m == "on_connect")on_connect = callable;
		else if (m == "on_disconnect")on_disconnect = callable;
		else if (m == "on_message")on_message = callable;
		else if (m == "on_py_log")on_py_log = callable;
	}




	void _call_latter_in_py_thread(const boost::system::error_code &ec, boost::asio::deadline_timer* t, PyObject *callable) {
		if (!ec) {
			py_service->post(
				boost::bind(
					py_call,
					callable
				)
			);
		}
		delete t;
	}

	void _call_latter_in_timer_thread(int sec, PyObject *callable) {
		//timter线程中执行等待
		boost::asio::deadline_timer* t = new boost::asio::deadline_timer(*timer_service);
		t->expires_from_now(boost::posix_time::seconds(sec));
		//	t.wait();
		t->async_wait(
			boost::bind(
				&pyiocp::_call_latter_in_py_thread,
				this,
				boost::asio::placeholders::error,
				t,
				callable)
		);
	}

	void call_latter(int sec, PyObject *callable) {
		timer_service->post(
			boost::bind(
				&pyiocp::_call_latter_in_timer_thread,
				this,
				sec,
				callable
			)
		);
	}


	void send(int conn_id, int msg_type, std::string buf) {
		//发送消息
		/*std::ostringstream os;
		os << msg_type;*/
		server::conn_map::iterator it = m_server->m_all_conn.find(conn_id);
		if (it != m_server->m_all_conn.end()) {
			it->second->send(msg_type, buf);
		}
	}

};




