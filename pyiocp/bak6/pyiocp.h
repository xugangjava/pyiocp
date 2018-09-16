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
#include<boost/unordered_map.hpp>
#include<stdio.h>
#include<fstream>
#include<iomanip>
#include<map>
#include <boost/atomic.hpp> 
#include <windows.h>  
#include <memory>
#include <vector>
#include <boost/thread/thread.hpp>

//using namespace boost::interprocess;
using namespace boost::python;
using boost::asio::ip::tcp;
enum { head_length = 4, packet_length = 8192 };
enum { EVT_CONNECT = 1, EVT_DISCONNECT = 4, EVT_ON_MESSAGE = 2, EVT_SEND_MESSAGE = 3, EVT_ON_TIMER =5 };
struct py_event {
	int conn_id;
	int sz;
	int msg_type;
	char body[packet_length];
	char head[head_length];
	int event_type;

	py_event() {
		conn_id = sz = msg_type = 0;
		memset(body, packet_length, 0);
		event_type = 0;
	}

	std::string buf() {
		return std::string(body, sz);
	}
};

PyObject	*on_py_event;

void py_call(py_event* evt) {
	call<void>(on_py_event,ptr(evt));
	delete evt;
}


/// A pool of io_service objects.
class io_service_pool
{

	
public:

	explicit io_service_pool(std::size_t pool_size) : next_io_service_(0), excpet_thread_(3)
	{
		if (pool_size == 0)
			throw std::runtime_error("io_service_pool size is 0");
		for (std::size_t i = 0; i < pool_size + excpet_thread_; ++i)
		{
			io_service_ptr io_service(new boost::asio::io_service);
			work_ptr work(new boost::asio::io_service::work(*io_service));
			io_services_.push_back(io_service);
			work_.push_back(work);
		}
	}

	void run()
	{
		std::vector<boost::shared_ptr<boost::thread> > threads;
		for (std::size_t i = 0; i < io_services_.size(); ++i)
		{
			boost::shared_ptr<boost::thread> thread(new boost::thread(
				boost::bind(&boost::asio::io_service::run, io_services_[i])));
			threads.push_back(thread);
		}
	}

	void stop()
	{
		for (std::size_t i = 0; i < io_services_.size(); ++i)
			io_services_[i]->stop();
	}


	boost::asio::io_service& get_accept_io_service()
	{
		return *io_services_[0];
	}

	boost::asio::io_service& get_py_io_service()
	{
		return *io_services_[1];
	}

	boost::asio::io_service& get_timer_io_service()
	{
		return *io_services_[2];
	}

	boost::asio::io_service& get_io_service()
	{
		if (io_services_.empty())
			throw std::runtime_error("get_io_service fail");
		if (next_io_service_ >= io_services_.size()|| next_io_service_<excpet_thread_)
			next_io_service_ = excpet_thread_;
		boost::asio::io_service& io_service = *io_services_[next_io_service_];
		++next_io_service_;
		return io_service;
	}

private:
	typedef boost::shared_ptr<boost::asio::io_service> io_service_ptr;
	typedef boost::shared_ptr<boost::asio::io_service::work> work_ptr;

	/// The pool of io_services.
	std::vector<io_service_ptr> io_services_;

	/// The work that keeps the io_services running.
	std::vector<work_ptr> work_;

	/// The next io_service to use for a connection.
	std::size_t next_io_service_;

	std::size_t excpet_thread_;
};

class server
{
	class conn :
		public boost::enable_shared_from_this<conn>
	{
	public:
		server* m_server;
		boost::asio::io_service::strand m_strand;
		conn(server* server,boost::asio::io_service& io_service, int conn_id)
			:m_socket(io_service), m_strand(io_service) {
			m_server = server;
			_id = conn_id;
			is_free = true;
			
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


		void recv() {
			is_free = false;
			py_event* evt = new py_event();
			evt->conn_id = _id;
			evt->event_type = EVT_ON_MESSAGE;
			boost::asio::async_read(
				m_socket,
				boost::asio::buffer(evt->head, head_length),
				m_strand.wrap(boost::bind(
					&conn::handle_read_head,
					shared_from_this(),
					evt,
					_1,
					_2
				))
			);
		}

		void handle_read_head(py_event* evt, const boost::system::error_code& error, size_t bytes_transferred) {
			if (error)
			{
				delete evt;
				close();
			}
			else {
				int length = (evt->head[1] & 0x000000ff) << 8 | (evt->head[0] & 0x000000ff);
				int total_len = length + 2;
				evt->msg_type = (evt->head[3] & 0x000000ff) << 8 | (evt->head[2] & 0x000000ff);
				evt->sz = total_len - 4;
				boost::asio::async_read(
					m_socket,
					boost::asio::buffer(evt->body, evt->sz),
					m_strand.wrap(boost::bind(
						&conn::handle_read_body,
						shared_from_this(),
						evt,
						_1,
						_2
					))
				);
			}
		}

		void handle_read_body(py_event* evt, const boost::system::error_code& error, size_t bytes_transferred) {
			if (error)
			{
				delete evt;
				close();
			}
			else {
				m_server->send_new_py_event(evt);
				recv();
			}
		}



		void send(int msg_type, std::string buf) {
			int total = 4 + buf.size();
			int length = total - 2;
			char head[4] = {
				(length >> 0) & 0xFF,
				(length >> 8) & 0xFF,
				(msg_type >> 0) & 0xFF,
				(msg_type >> 8) & 0xFF
			};
			std::vector<boost::asio::const_buffer> buffers;
			buffers.push_back(boost::asio::buffer(head));
			buffers.push_back(boost::asio::buffer(buf));
			boost::asio::async_write(
				m_socket,
				buffers,
				boost::bind(&conn::handle_write, shared_from_this(), _1, _2)
			);
		}

		void handle_write(const boost::system::error_code& error, size_t bytes_transferred) {
			if (error) {
				close();
			}
		}

		bool close() {
			py_event* evt=new py_event();
			evt->conn_id = _id;
			evt->event_type = EVT_DISCONNECT;
			m_server->send_new_py_event(evt);
			try {
				m_socket.release();
			}
			catch (boost::system::system_error e) {
				return false;
			}
			is_free = true;
			m_server->check();
			return true;
		}
	};

public:
	tcp::acceptor m_acceptor;
	typedef boost::shared_ptr<conn> conn_ptr;
	typedef boost::unordered_map<int, conn_ptr> conn_map;
	boost::atomic<int> m_conn_index;
	boost::atomic<bool> m_is_accept;
	io_service_pool&  pool;
	boost::asio::io_service& m_py_io;
	boost::asio::io_service::strand m_py_strand;
	conn_map m_all_conn;

	server(int port, int max_connection,io_service_pool& p)
		: pool(p), m_acceptor(p.get_accept_io_service(), tcp::endpoint(tcp::v4(), port)), m_py_io(p.get_py_io_service()), m_py_strand(p.get_py_io_service()){

		for (int i = 0; i < max_connection; i++) {
			m_all_conn[i] = conn_ptr(new conn(this,pool.get_io_service(), i));
		}
		m_conn_index = 0;
		
		start_accept();
	}

	void send_new_py_event(py_event* evt) {
		//python消息队列
		m_py_io.post(
			m_py_strand.wrap(boost::bind(
				py_call,
				evt
			))
		);
	  }



	conn_ptr get_free_conn() {
		int try_get_count = 0;
		while (1) {
			if (m_conn_index == m_all_conn.size()) {
				m_conn_index = 0;
			}
			conn_ptr c = m_all_conn[m_conn_index]->shared_from_this();
			if (c->is_free) {
				return c;
			}
			////修复错误连接
			//if (c->is_bad) {
			//	m_all_conn[m_conn_index]= conn_ptr(new conn(this, pool.get_io_service(), m_conn_index));
			//}
			try_get_count++;
			m_conn_index++;
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

	void handle_accept(conn_ptr new_connection, const boost::system::error_code& error) {
		if (!error)
		{
			py_event* evt=new py_event();
			evt->conn_id = new_connection->get_id();
			evt->event_type = EVT_CONNECT;
			std::string sIp = new_connection->socket().remote_endpoint().address().to_v4().to_string();
			evt->sz = sIp.size();
			strcpy(evt->body, sIp.c_str());
			send_new_py_event(evt);
			new_connection->recv();
		}
		else {
			//重置连接
			new_connection->close();
		}
		start_accept();
	}

	void run() {
		pool.run();
		m_py_io.run();
	}
};

class pyiocp
{
public:

	server* m_server;
	io_service_pool pool;

	pyiocp(int thread_num):pool(thread_num) {

	}
	void send(int conn_id, int msg_type, std::string buf) {
		m_server->m_all_conn[conn_id]->send(msg_type, buf);
	}

	void run(PyObject* callback,  int port ,int max_connection) {
		on_py_event = callback;
		m_server = new server(port, max_connection, pool);
		m_server->run();
	}


	void call_on_time(boost::asio::deadline_timer* t, py_event* evt) {
		delete t;
		m_server->send_new_py_event(evt);
	}

	void call_latter_in_thread(int sec, py_event* evt) {
		boost::asio::deadline_timer*  t = new boost::asio::deadline_timer(
			pool.get_timer_io_service(),
			boost::posix_time::seconds(sec));
		t->async_wait(
			boost::bind(
				&pyiocp::call_on_time,
				this,
				t,
				evt
			)
		);
	}

	void call_latter(int sec, std::string timer_id) {
		py_event* evt = new py_event();
		evt->event_type = EVT_ON_TIMER;
		evt->sz = timer_id.size();
		strcpy(evt->body, timer_id.c_str());
		pool.get_timer_io_service().post(boost::bind(&pyiocp::call_latter_in_thread, this, sec, evt));
	}

};




