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
using namespace boost::python;
using boost::asio::ip::tcp;
enum { head_length = 4, packet_length = 8192 };
enum { EVT_CONNECT = 1, EVT_DISCONNECT = 4, EVT_ON_MESSAGE = 2, EVT_SEND_MESSAGE = 3, EVT_ON_TIMER = 5 };

PyObject	*on_py_event;
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

void py_call(py_event* evt) {
	call<void>(on_py_event, ptr(evt));
	delete evt;
}

class io_service_pool
	: private boost::noncopyable
{
public:
	explicit io_service_pool(int pool_size) : next_io_context_(0)
	{
		pool_size = max(pool_size, 2);//至少有两个线程 IO线程 PY线程
		for (int i = 0; i < pool_size; ++i)
		{
			io_context_ptr io_context(new boost::asio::io_context);
			io_contexts_.push_back(io_context);
			work_.push_back(boost::asio::make_work_guard(*io_context));
		}
	}

	void run()
	{
		std::vector<boost::shared_ptr<boost::thread> > threads;
		for (std::size_t i = 1; i < io_contexts_.size(); ++i)
		{
			boost::shared_ptr<boost::thread> thread(new boost::thread(
				boost::bind(&boost::asio::io_context::run, io_contexts_[i])));
			threads.push_back(thread);
		}
		//PY线程接管
		//threads[0]->join();
	}

	void stop()
	{
		for (int i = 0; i < io_contexts_.size(); ++i)
			io_contexts_[i]->stop();
	}


	boost::asio::io_service& get_py_io_service() {
		return *io_contexts_[0];
	}


	boost::asio::io_service& get_io_service()
	{
		boost::asio::io_context& io_context = *io_contexts_[next_io_context_];
		++next_io_context_;
		if (next_io_context_ >= io_contexts_.size() || next_io_context_ == 0)
			next_io_context_ = 1;
		return io_context;
	}

private:
	typedef boost::shared_ptr<boost::asio::io_context> io_context_ptr;
	typedef boost::asio::executor_work_guard<
		boost::asio::io_context::executor_type> io_context_work;
	std::vector<io_context_ptr> io_contexts_;
	std::list<io_context_work> work_;
	int next_io_context_;
};

class server
{

	class conn :
		public boost::enable_shared_from_this<conn>
	{

	public:
		server* m_server;
		boost::asio::io_service::strand m_strand;
		conn(server* server, boost::asio::io_service& io_service, int conn_id)
			:m_socket(io_service), m_strand(io_service) {
			m_server = server;
			_id = conn_id;
			is_closed = false;
		};

		int _id;
		int get_id() {
			return _id;
		}

		boost::atomic<bool> is_closed;
		tcp::socket& socket() {
			return m_socket;
		}

		tcp::socket  m_socket;
		void recv() {
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
			int sz = buf.size();
			int total = 4 + sz;
			int length = total - 2;
			char head[4] = {
				(length >> 0) & 0xFF,
				(length >> 8) & 0xFF,
				(msg_type >> 0) & 0xFF,
				(msg_type >> 8) & 0xFF
			};
			std::vector<boost::asio::const_buffer> buffers;
			buffers.push_back(boost::asio::buffer(head));
			buffers.push_back(boost::asio::buffer(buf, sz));
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

		void close() {
			if (is_closed)return;
			is_closed = true;
			boost::system::error_code ignored_ec;
			m_socket.shutdown(boost::asio::ip::tcp::socket::shutdown_both, ignored_ec);
			m_socket.close(ignored_ec);

			py_event* evt = new py_event();
			evt->conn_id = _id;
			evt->event_type = EVT_DISCONNECT;
			m_server->send_new_py_event(evt);

		}
	};

public:
	typedef boost::shared_ptr<conn> conn_ptr;
	typedef boost::unordered_map<int, conn_ptr> conn_map;
	tcp::acceptor m_acceptor;
	int m_conn_index;

	io_service_pool&  pool;
	conn_map m_all_conn;
	boost::asio::io_service::strand m_strand;
	explicit server(int port, io_service_pool& p)
		: pool(p), m_acceptor(p.get_py_io_service(), tcp::endpoint(tcp::v4(), port)), m_strand(p.get_py_io_service()) {
		//	m_acceptor.set_option(boost::asio::ip::tcp::acceptor::reuse_address(true));
		m_conn_index = 0;
		m_acceptor.set_option(boost::asio::socket_base::keep_alive(true));
	}



	void send_new_py_event(py_event* evt) {
		//python消息队列
		pool.get_py_io_service().post(
			m_strand.wrap(boost::bind(
				py_call,
				evt
			))
		);
	}

	void start_accept() {
		conn_map::iterator iter = m_all_conn.begin();
		while (iter != m_all_conn.end()) {
			if (iter->second->is_closed) {
				m_all_conn.erase(iter++);
			}
			else {
				iter++;
			}
		}

		m_all_conn[m_conn_index] = conn_ptr(new conn(this, pool.get_io_service(), m_conn_index));
		conn_ptr new_connection = m_all_conn[m_conn_index]->shared_from_this();

		m_conn_index++;
		m_acceptor.async_accept(
			new_connection->socket(),
			boost::bind(&server::handle_accept, this, new_connection, boost::asio::placeholders::error)
		);
	}


	void handle_accept(conn_ptr new_connection, const boost::system::error_code& error) {
		if (!error)
		{
			py_event* evt = new py_event();
			evt->conn_id = new_connection->get_id();
			evt->event_type = EVT_CONNECT;
			std::string sIp = new_connection->socket().remote_endpoint().address().to_v4().to_string();
			evt->sz = sIp.size();
			strcpy(evt->body, sIp.c_str());
			send_new_py_event(evt);
			new_connection->socket().set_option(boost::asio::ip::tcp::no_delay(true));
			new_connection->recv();
		}
		else {
			//重置连接
			new_connection->close();
		}
		start_accept();
	}


};


class pyiocp
{
public:
	server* m_server;
	int m_thread_num;
	std::string latter_call;
	pyiocp(int thread_num) {
		m_thread_num = thread_num;
		m_server = NULL;
		latter_call = "";
	}

	int on_line_count() {
		if (!m_server)return 0;
		return m_server->m_all_conn.size();
	}

	void send(int conn_id, int msg_type, std::string buf) {
		m_server->m_all_conn[conn_id]->send(msg_type, buf);
	}

	void run(PyObject* cb, int port) {
		on_py_event = cb;
		io_service_pool pool(m_thread_num);
		m_server = new server(port, pool);
		if (latter_call != "") {
			call_latter(2, latter_call);
		}
		m_server->m_acceptor.set_option(boost::asio::ip::tcp::acceptor::reuse_address(1));
		m_server->m_acceptor.listen();
		m_server->start_accept();
		pool.run();
		pool.get_py_io_service().run();
	}


	void call_on_time(boost::asio::deadline_timer* t, py_event* evt , const boost::system::error_code& error) {
		delete t;
		if (!error) {
			m_server->send_new_py_event(evt);
		}
	}


	void call_latter(int sec, std::string timer_id) {
		//等待初始化后运行
		if (!m_server) {
			latter_call = timer_id;
			return;
		}
		py_event* evt = new py_event();
		evt->event_type = EVT_ON_TIMER;
		evt->sz = timer_id.size();
		strcpy(evt->body, timer_id.c_str());

		boost::asio::deadline_timer*  t = new boost::asio::deadline_timer(
			m_server->pool.get_py_io_service(),
			boost::posix_time::seconds(sec));
		t->async_wait(
			m_server->m_strand.wrap(boost::bind(
				&pyiocp::call_on_time,
				this,
				t,
				evt,
				boost::asio::placeholders::error
			))
		);
	}

};




