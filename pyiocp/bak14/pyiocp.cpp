#include "stdafx.h"
#include "pyiocp.h"

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
#include <boost/uuid/uuid.hpp>
#include <boost/uuid/uuid_io.hpp>
#include <boost/uuid/uuid_generators.hpp>

using namespace boost::python;
using boost::asio::ip::tcp;
enum { head_length = 4, packet_length = 8192, uuid_length = 40 };
enum { EVT_CONNECT = 1, EVT_DISCONNECT = 4, EVT_ON_MESSAGE = 2, EVT_SEND_MESSAGE = 3, EVT_ON_TIMER = 5 };

PyObject	*on_py_event;

struct py_event {
	std::string buf;
	std::string conn_id;
	int sz;
	int msg_type;
	int event_type;
	py_event() {
		sz = msg_type = 0;
		event_type = 0;
	}
};

void py_call(py_event& evt) {
	call<void>(on_py_event, evt);
}

class io_service_pool
	: private boost::noncopyable
{
public:
	explicit io_service_pool(int pool_size) : next_io_context_(0)
	{
		//至少有两个线程 IO线程 PY线程
		pool_size = pool_size + 3;
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
	}

	void stop()
	{
		for (int i = 0; i < io_contexts_.size(); ++i)
			io_contexts_[i]->stop();
	}


	boost::asio::io_service& get_py_io_service() {
		return *io_contexts_[0];
	}

	boost::asio::io_service& get_accept_io_service() {
		return *io_contexts_[1];
	}

	boost::asio::io_service& get_io_service()
	{
		boost::asio::io_context& io_context = *io_contexts_[next_io_context_];
		++next_io_context_;
		if (next_io_context_ >= io_contexts_.size() || next_io_context_ == 0)
			next_io_context_ = 2;
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
		boost::atomic<bool> is_closed;
		boost::atomic<bool> is_open;
		char body[packet_length];
		char head[head_length];
		boost::atomic<time_t> active_time;
		py_event packet;
		tcp::socket  m_socket;
		server* m_server;
		boost::asio::io_service::strand m_strand;
		conn(server* server, boost::asio::io_service& io_service, const std::string conn_id)
			:m_socket(io_service), m_strand(io_service) {
			m_server = server;
			id = conn_id;
			is_closed = false;
			is_open = false;
			active_time = time(0);
		};

		~conn() {
			close();
		}

		std::string id;

		tcp::socket& socket() {
			return m_socket;
		}


		void recv() {
			packet.conn_id = id;
			packet.event_type = EVT_ON_MESSAGE;
			boost::asio::async_read(
				m_socket,
				boost::asio::buffer(head, head_length),
				m_strand.wrap(boost::bind(
					&conn::handle_read_head,
					shared_from_this(),
					_1,
					_2
				))
			);
		}

		void handle_read_head(const boost::system::error_code& error, size_t bytes_transferred) {
			if (error)
			{
				close();
			}
			else {
				int length = (head[1] & 0x000000ff) << 8 | (head[0] & 0x000000ff);
				int total_len = length + 2;
				packet.msg_type = (head[3] & 0x000000ff) << 8 | (head[2] & 0x000000ff);
				packet.sz = total_len - 4;
				boost::asio::async_read(
					m_socket,
					boost::asio::buffer(body, packet.sz),
					m_strand.wrap(boost::bind(
						&conn::handle_read_body,
						shared_from_this(),
						_1,
						_2
					))
				);
			}
		}

		void handle_read_body(const boost::system::error_code& error, size_t bytes_transferred) {
			if (error)
			{
				close();
			}
			else {
				packet.buf = std::string(body, packet.sz);
				m_server->send_new_py_event(packet);
				recv();
			}
		}

		void send(int msg_type, std::string buf) {
			active_time = time(0);
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

		void check() {
			if (!is_open)return;
			//防止空连接
			if (difftime(time(0), active_time) > 300) {
				m_socket.get_io_service().post(
					boost::bind(&conn::close, shared_from_this())
				);
			}
		}

		void close() {
			if (is_closed)return;
			is_closed = true;
			packet.conn_id = id;
			packet.event_type = EVT_DISCONNECT;
			m_server->send_new_py_event(packet);
			boost::system::error_code ignored_ec;
			m_socket.release(ignored_ec);
			m_socket.shutdown(boost::asio::ip::tcp::socket::shutdown_both, ignored_ec);
			m_socket.close(ignored_ec);
		}


	};

public:
	typedef boost::shared_ptr<conn> conn_ptr;
	typedef boost::unordered_map<std::string, conn_ptr> conn_map;
	tcp::acceptor m_acceptor;

	io_service_pool&  pool;
	conn_map m_all_conn;
	boost::asio::io_service::strand m_strand;
	explicit server(int port, io_service_pool& p)
		: pool(p),
		m_acceptor(p.get_accept_io_service(), tcp::endpoint(tcp::v4(), port)),
		m_strand(p.get_py_io_service()) {
	}

	void send_new_py_event(py_event& evt) {
		pool.get_py_io_service().post(
			m_strand.wrap(boost::bind(
				py_call,
				evt
			))
		);
	}

	void start_accept() {
		boost::uuids::uuid uuid = boost::uuids::random_generator()();
		const std::string tmp_uuid = boost::uuids::to_string(uuid);
		m_all_conn[tmp_uuid] = conn_ptr(new conn(this, pool.get_io_service(), tmp_uuid));
		conn_ptr conn = m_all_conn[tmp_uuid]->shared_from_this();
		m_acceptor.async_accept(
			conn->socket(),
			boost::bind(&server::handle_accept, this, conn, boost::asio::placeholders::error)
		);
	}


	void handle_accept(conn_ptr conn, const boost::system::error_code& error) {
		if (!error)
		{
			py_event packet;
			packet.conn_id = conn->id;
			packet.event_type = EVT_CONNECT;
			std::string sIp = conn->socket().remote_endpoint().address().to_v4().to_string();
			packet.sz = sIp.size();
			packet.buf = sIp;
			////	strcpy(packet.body, sIp.c_str());
			//	conn->active_time = time(0);
			conn->is_open = true;
			send_new_py_event(packet);
			conn->socket().set_option(boost::asio::ip::tcp::no_delay(true));
			conn->recv();
		}
		else {
			//重置连接
			conn->close();
		}
		start_accept();
	/*	pool.get_py_io_service().post(
			boost::bind(&server::start_accept, this)
		);*/
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

	~pyiocp() {
		if (m_server) {
			m_server->m_all_conn.erase(m_server->m_all_conn.begin(), m_server->m_all_conn.end());
			m_server->pool.stop();
			delete m_server;
		}
	}

	int on_line_count() {
		if (!m_server)return 0;
		server::conn_map::iterator iter = m_server->m_all_conn.begin();
		while (iter != m_server->m_all_conn.end()) {
			if (iter->second->is_closed) {
				m_server->m_all_conn.erase(iter++);
			}
			else {
				iter++;
			}
		}
		iter = m_server->m_all_conn.begin();
		while (iter != m_server->m_all_conn.end()) {
			iter->second->check();
			iter++;
		}
		return m_server->m_all_conn.size();
	}

	bool send(std::string conn_id, int msg_type, std::string buf) {
		server::conn_map::iterator iter = m_server->m_all_conn.find(conn_id);
		if (iter == m_server->m_all_conn.end())return false;
		iter->second->send(msg_type, buf);
		return true;
	}

	bool is_connected(std::string conn_id) {
		return m_server->m_all_conn.find(conn_id) == m_server->m_all_conn.end();
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


	void call_on_time(boost::asio::deadline_timer* t, std::string& timer_id, const boost::system::error_code& error) {
		delete t;
		if (!error) {
			py_event evt;
			evt.event_type = EVT_ON_TIMER;
			evt.sz = timer_id.size();
			evt.buf = timer_id;
			//		strcpy(evt.body, timer_id.c_str());
			m_server->send_new_py_event(evt);
		}
	}


	void call_latter(int sec, std::string timer_id) {
		//等待初始化后运行
		if (!m_server) {
			latter_call = timer_id;
			return;
		}
		boost::asio::deadline_timer*  t = new boost::asio::deadline_timer(
			m_server->pool.get_io_service(),
			boost::posix_time::seconds(sec));
		t->async_wait(
			m_server->m_strand.wrap(boost::bind(
				&pyiocp::call_on_time,
				this,
				t,
				timer_id,
				boost::asio::placeholders::error
			))
		);
	}
};


BOOST_PYTHON_MODULE(pyiocp)
{
	class_<pyiocp>("pyiocp", init<int>())
		.def("send", &pyiocp::send)
		.def("run", &pyiocp::run)
		.def("call_latter", &pyiocp::call_latter)
		.def("is_connected", &pyiocp::is_connected)
		.def_readonly("on_line_count", &pyiocp::on_line_count)
		;

	class_<py_event>("py_event")
		.def_readonly("conn_id", &py_event::conn_id)
		.def_readonly("event_type", &py_event::event_type)
		.def_readonly("msg_type", &py_event::msg_type)
		.def_readonly("buf", &py_event::buf)
		;
}

