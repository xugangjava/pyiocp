#include "stdafx.h"
#include "py_event.h"
#include <boost/bind.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/enable_shared_from_this.hpp>
#include <boost/asio.hpp>
#include <iostream> 
#include <string>
#include <boost/unordered_map.hpp>
#include <stdio.h>
#include <fstream>
#include <iomanip>
#include <map>
#include <boost/atomic.hpp> 
#include <windows.h>  
#include <memory>
#include <vector>
#include <boost/thread/thread.hpp>
#include <boost/uuid/uuid.hpp>
#include <boost/uuid/uuid_io.hpp>
#include <boost/uuid/uuid_generators.hpp>
#include <boost/thread/recursive_mutex.hpp>
#include <boost/thread/condition.hpp>
#include <boost/lockfree/queue.hpp>
#include <queue>
using boost::asio::ip::tcp;
boost::lockfree::queue<py_event, boost::lockfree::capacity<10000>> que;
class io_service_pool
	: private boost::noncopyable
{
public:
	explicit io_service_pool(int pool_size) : next_io_context_(0)
	{
		if (pool_size < 2) {
			pool_size = 2;
		}
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
		for (std::size_t i = 0; i < io_contexts_.size(); ++i)
		{
			boost::shared_ptr<boost::thread> thread(new boost::thread(
				boost::bind(&boost::asio::io_context::run, io_contexts_[i])));
			threads.push_back(thread);
		}
	}

	void stop()
	{
		for (size_t i = 0; i < io_contexts_.size(); ++i)
			io_contexts_[i]->stop();
	}

	boost::asio::io_service& get_accept_io_service()
	{
		return *io_contexts_[0];
	}

	boost::asio::io_service& get_io_service()
	{
		boost::asio::io_context& io_context = *io_contexts_[next_io_context_];
		++next_io_context_;
		if (next_io_context_ >= io_contexts_.size())
			next_io_context_ = 0;
		return io_context;
	}

private:
	typedef boost::shared_ptr<boost::asio::io_context> io_context_ptr;
	typedef boost::asio::executor_work_guard<
		boost::asio::io_context::executor_type> io_context_work;
	std::vector<io_context_ptr> io_contexts_;
	std::list<io_context_work> work_;
	size_t next_io_context_;
};

class server
{
	class conn :
		public boost::enable_shared_from_this<conn>
	{

	public:
		boost::atomic<bool> is_closed;
		long idle;
		py_event packet;
		tcp::socket  m_socket;
		server* m_server;
		boost::asio::io_service::strand m_strand;
		conn(server* server, boost::asio::io_service& io_service, const std::string conn_id)
			:m_socket(io_service), m_strand(io_service), id(conn_id) {
			m_server = server;
			is_closed = false;
			idle = 0;
		};

		~conn() {
			close();
		}

		const std::string id;


		void recv() {
			packet.set_conn_id(id);
			packet.event_type = EVT_ON_MESSAGE;
			boost::asio::async_read(
				m_socket,
				boost::asio::buffer(packet.head, head_length),
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
				int length = (packet.head[1] & 0x000000ff) << 8 | (packet.head[0] & 0x000000ff);
				int total_len = length + 2;
				packet.msg_type = (packet.head[3] & 0x000000ff) << 8 | (packet.head[2] & 0x000000ff);
				packet.sz = total_len - 4;
				if (packet.sz > 8000) {
					close();
					return;
				}
				boost::asio::async_read(
					m_socket,
					boost::asio::buffer(packet.body, packet.sz),
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
				//packet.set_buf(std::string(body, packet.sz));
				m_server->send_new_py_event(packet);
				recv();
			}
		}

		void send(int msg_type, std::string buf) {
			idle = (long)time(0);
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
			packet.set_conn_id(id);
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

	io_service_pool& pool;
	conn_map m_all_conn;
	boost::asio::io_service::strand m_strand;
	const int m_iocp_time_out;
	explicit server(int port, int iocp_time_out, io_service_pool& p)
		: pool(p),
		m_acceptor(p.get_accept_io_service(), tcp::endpoint(tcp::v4(), port)),
		m_strand(p.get_accept_io_service()),
		m_iocp_time_out(iocp_time_out) {

	}

	void send_new_py_event(py_event& evt) {
		que.push(evt);
	}

	void post_start_accept() {
		pool.get_accept_io_service().post(boost::bind(&server::start_accept, this));
	}

	void post_send(std::string conn_id, int msg_type, std::string buf) {
		pool.get_accept_io_service().post(boost::bind(&server::send, this, conn_id, msg_type, buf));
	}

	void post_gc() {
		pool.get_accept_io_service().post(boost::bind(&server::gc, this));
	}

	boost::atomic<int> on_line_count;

private:

	void gc() {
		server::conn_map::iterator iter = m_all_conn.begin();
		while (iter != m_all_conn.end()) {
			if (iter->second->is_closed) {
				m_all_conn.erase(iter++);
			}
			else {
				iter++;
			}
		}
		long now = (long)time(0);
		iter = m_all_conn.begin();
		while (iter != m_all_conn.end()) {
			if (iter->second->idle != 0) {
				if (now - iter->second->idle > m_iocp_time_out) {
					iter->second->close();
				}
			}
			iter++;
		}
		on_line_count = m_all_conn.size();
	}

	void send(std::string conn_id, int msg_type, std::string buf) {
		server::conn_map::iterator iter = m_all_conn.find(conn_id);
		if (iter == m_all_conn.end())return;
		iter->second->send(msg_type, buf);
	}

	void start_accept() {
		boost::uuids::uuid uuid = boost::uuids::random_generator()();
		const std::string tmp_uuid = boost::uuids::to_string(uuid);
		m_all_conn[tmp_uuid] = conn_ptr(new conn(this, pool.get_io_service(), tmp_uuid));

		m_acceptor.async_accept(
			m_all_conn[tmp_uuid]->m_socket,
			boost::bind(&server::handle_accept, this, tmp_uuid, boost::asio::placeholders::error)
		);
	}

	void handle_accept(std::string conn_id, const boost::system::error_code& error) {
		server::conn_map::iterator iter = m_all_conn.find(conn_id);
		if (iter != m_all_conn.end()) {
			if (!error)
			{
				py_event packet;
				packet.set_conn_id(conn_id);
				packet.event_type = EVT_CONNECT;
				std::string sIp = iter->second->m_socket.remote_endpoint().address().to_v4().to_string();
				packet.set_buf(sIp);
				send_new_py_event(packet);
				//conn->m_socket.set_option(boost::asio::ip::tcp::no_delay(true));
				iter->second->m_socket.set_option(boost::asio::ip::tcp::no_delay(true));
				iter->second->m_socket.set_option(boost::asio::socket_base::keep_alive(true));
				iter->second->recv();

			}
			else
			{
				iter->second->close();
			}
		}
		start_accept();
	}

};
//
//
//class pyiocp
//{
//
//public:
//	server* m_server;
//	int m_thread_num;
//	pyiocp(int thread_num) {
//		m_thread_num = thread_num;
//		m_server = NULL;
//	}
//
//	~pyiocp() {
//		if (m_server) {
//			m_server->m_all_conn.erase(m_server->m_all_conn.begin(), m_server->m_all_conn.end());
//			m_server->pool.stop();
//			delete m_server;
//		}
//	}
//
//	int on_line_count() {
//		if (!m_server)return 0;
//		m_server->post_gc();
//		return m_server->on_line_count;
//	}
//
//	void send(std::string conn_id, int msg_type, std::string buf) {
//		m_server->post_send(conn_id, msg_type, buf);
//	}
//
//	void run(PyObject* cb, int port, int iocp_time_out) {
//		on_py_event = cb;
//		io_service_pool* pool = new io_service_pool(m_thread_num);
//		m_server = new server(port, iocp_time_out, *pool);
//		m_server->m_acceptor.set_option(boost::asio::ip::tcp::acceptor::reuse_address(1));
//		m_server->m_acceptor.listen();
//		m_server->post_start_accept();
//		pool->run();
//	}
//
//
//	void call_on_time(boost::asio::deadline_timer* t, std::string& timer_id, const boost::system::error_code& error) {
//		delete t;
//		if (!error) {
//			py_event evt;
//			evt.event_type = EVT_ON_TIMER;
//			evt.set_buf(timer_id);
//			m_server->send_new_py_event(evt);
//		}
//	}
//
//	list get_event() {
//		list r;
//		while (!que.empty()) {
//			py_event evt;
//			que.pop(evt);
//			r.append(evt);
//		}
//		return r;
//	}
//
//
//	void crash() {
//		throw 0;
//	}
//
//	void call_latter(int sec, std::string timer_id) {
//		if (!m_server)return;
//		boost::asio::deadline_timer* t = new boost::asio::deadline_timer(
//			m_server->pool.get_io_service(),
//			boost::posix_time::seconds(sec));
//		t->async_wait(
//			m_server->m_strand.wrap(boost::bind(
//				&pyiocp::call_on_time,
//				this,
//				t,
//				timer_id,
//				boost::asio::placeholders::error
//			))
//		);
//	}
//};