#include "StdAfx.h"
#include "ws.h"
#include <boost/python.hpp>
#include <boost/python/module.hpp>
#include <boost/python/def.hpp>
#include <boost/python/list.hpp>
#include <boost/asio.hpp>
#include <boost/lockfree/queue.hpp>
#include <boost/thread/thread.hpp>
#include <boost/thread/recursive_mutex.hpp>
#include <boost/thread/condition.hpp>
#include <boost/unordered_map.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/enable_shared_from_this.hpp>

#include <websocketpp/config/asio_no_tls.hpp>
#include <websocketpp/server.hpp>
#include "py_event.h"

using websocketpp::lib::placeholders::_1;
using websocketpp::lib::placeholders::_2;
using websocketpp::lib::bind;

struct ws_config : public websocketpp::config::asio {
	// pull default settings from our core config
	typedef websocketpp::config::asio core;

	typedef core::concurrency_type concurrency_type;
	typedef core::request_type request_type;
	typedef core::response_type response_type;
	typedef core::message_type message_type;
	typedef core::con_msg_manager_type con_msg_manager_type;
	typedef core::endpoint_msg_manager_type endpoint_msg_manager_type;

	typedef core::alog_type alog_type;
	typedef core::elog_type elog_type;
	typedef core::rng_type rng_type;
	typedef core::endpoint_base endpoint_base;

	static bool const enable_multithreading = true;

	struct transport_config : public core::transport_config {
		typedef core::concurrency_type concurrency_type;
		typedef core::elog_type elog_type;
		typedef core::alog_type alog_type;
		typedef core::request_type request_type;
		typedef core::response_type response_type;

		static bool const enable_multithreading = true;
	};

	typedef websocketpp::transport::asio::endpoint<transport_config>
		transport_type;

	static const websocketpp::log::level elog_level =
		websocketpp::log::elevel::none;
	static const websocketpp::log::level alog_level =
		websocketpp::log::alevel::none;
};

class ws
{
	typedef websocketpp::server<ws_config> wsserver;
	typedef wsserver::message_ptr ws_msg_ptr;
	typedef wsserver::connection_ptr ws_con_ptr;
	class ws_conn :
		public boost::enable_shared_from_this<ws_conn>
	{
	public:
		boost::atomic<bool> is_closed;
		ws_conn(std::string conn_id, websocketpp::connection_hdl hdl, ws* sv) {
			m_conn_id = conn_id;
			m_hdl = hdl;
			m_idle = 0;
			m_sv = sv;
		}

		long idle() {
			return m_idle;
		}

	
		void close() {
			if (is_closed)return;
			is_closed = true;
			py_event packet;
			packet.set_conn_id(m_conn_id);
			packet.event_type = EVT_DISCONNECT;
			m_sv->send_new_py_event(packet);

		}
		
		void send(int msg_type, std::string buf) {
			m_idle = (long)time(0);
			//int sz = buf.size();
			//int total = 4 + sz;
			//int length = total - 2;
			//char head[4] = {
			//	(length >> 0) & 0xFF,
			//	(length >> 8) & 0xFF,
			//	(msg_type >> 0) & 0xFF,
			//	(msg_type >> 8) & 0xFF
			//};
			//std::vector<boost::asio::const_buffer> buffers;
			//buffers.push_back(boost::asio::buffer(head));
			//buffers.push_back(boost::asio::buffer(buf, sz));

		}

		void on_message(std::string payload) {
			m_idle = (long)time(0);
		/*	py_event packet;
			packet.set_conn_id(m_conn_id);
			boost::asio::buffer buf=boost::asio::buffer(payload);*/

			/*payload.copy(packet.head, 4, 0);
			int length = (packet.head[1] & 0x000000ff) << 8 | (packet.head[0] & 0x000000ff);
			if (length != payload.size())return;
			if (length > packet_length)return;
			payload.copy(packet.body, payload.size() - 4, 4);
			packet.event_type = EVT_ON_MESSAGE;
			packet.msg_type = (packet.head[3] & 0x000000ff) << 8 | (packet.head[2] & 0x000000ff);*/
			
		}


		void on_open() {
			m_idle = (long)time(0);
			py_event packet;
			packet.set_conn_id(m_conn_id);
			packet.event_type = EVT_CONNECT;
			m_sv->send_new_py_event(packet);
		}

	private:
		std::string m_conn_id;
		long m_idle;
		websocketpp::connection_hdl  m_hdl;
		ws* m_sv;
	};
	typedef boost::unordered_map<std::string, boost::shared_ptr<ws_conn>> conn_map;
private:
	int m_thread_num;
	wsserver* sv;
	conn_map m_all_conn;
	boost::lockfree::queue<py_event, boost::lockfree::capacity<10000>> ws_que;
	long m_iocp_time_out;

public:

	ws(int thread_num) {
		m_thread_num = thread_num;
		if (m_thread_num <= 0)m_thread_num = 1;

	}
	~ws() {

	}

	wsserver* ws_ptr() {
		return sv;
	}

	//连接
	static void on_open(ws* sv, websocketpp::connection_hdl hdl)
	{
		ws_con_ptr con = sv->ws_ptr()->get_con_from_hdl(hdl);
		const std::string conn_id = con->get_remote_endpoint();
		sv->m_all_conn[conn_id] = boost::shared_ptr<ws_conn>(new ws_conn(conn_id, con, sv));
		sv->m_all_conn[conn_id]->on_open();
	}

	//断开
	static void on_close(ws* sv, websocketpp::connection_hdl hdl)
	{
		ws_con_ptr con = sv->ws_ptr()->get_con_from_hdl(hdl);
		const std::string conn_id = con->get_remote_endpoint();
		sv->remove_conn(conn_id);
	}


	//通信
	static void on_message(ws* sv, websocketpp::connection_hdl hdl, ws_msg_ptr msg)
	{

		const std::string payload = msg->get_payload();
		if (payload.size() > packet_length - 1)return;
		ws_con_ptr con = sv->ws_ptr()->get_con_from_hdl(hdl);
		const std::string conn_id = con->get_remote_endpoint();
		if (!sv->is_exist_conn(conn_id))return;
		sv->m_all_conn[conn_id]->on_message(payload);
	}

	void send(std::string conn_id, int msg_type, std::string buf) {
		if (!is_exist_conn(conn_id))return;
		m_all_conn[conn_id]->send(msg_type,buf);
	}

	//移除连接
	void remove_conn(std::string conn_id) {
		if (!is_exist_conn(conn_id))return;
		m_all_conn.erase(m_all_conn.find(conn_id));
	}

	bool is_exist_conn(std::string conn_id) {
		return  m_all_conn.find(conn_id) == m_all_conn.end();
	}

	void send_new_py_event(py_event& evt) {
		ws_que.push(evt);
	}

	void run(PyObject* cb, int port, int iocp_time_out) {
		m_iocp_time_out = iocp_time_out;
		sv = new wsserver();
		try {
			sv->set_access_channels(websocketpp::log::alevel::all);
			sv->clear_access_channels(websocketpp::log::alevel::frame_payload);
			sv->init_asio();
			//bind
			sv->set_open_handler(bind(&on_open, this, ::_1));
			sv->set_close_handler(bind(&on_close, this, ::_1));
			sv->set_message_handler(bind(&on_message, this, ::_1, ::_2));
			//listen
			sv->listen(port);
			sv->start_accept();
			if (m_thread_num <= 1) {
				sv->run();
			}
			else {
				std::vector<boost::shared_ptr<boost::thread> > threads;
				for (std::size_t i = 0; i < m_thread_num; ++i)
				{
					boost::shared_ptr<boost::thread> thread(new boost::thread(boost::bind(&wsserver::run, sv)));
					threads.push_back(thread);
				}
			}
		}
		catch (websocketpp::exception const & e) {
			std::cout << e.what() << std::endl;
		}
		catch (...) {
			std::cout << "other exception" << std::endl;
		}
		delete sv;
	}


	boost::python::list get_event() {
		boost::python::list r;
		while (!ws_que.empty()) {
			py_event evt;
			ws_que.pop(evt);
			r.append(evt);
		}
		return r;
	}

	int on_line_count() {
		long now = (long)time(0);
		ws::conn_map::iterator iter = m_all_conn.begin();
		while (iter != m_all_conn.end()) {
			if (iter->second->idle != 0) {
				if (now - iter->second->idle > m_iocp_time_out) {
					iter->second->close();
				}
			}
			iter++;
		}
		return  m_all_conn.size();
	}


	void call_latter(int sec, std::string timer_id) {}

	void crash(){}
};

using namespace boost::python;
BOOST_PYTHON_MODULE(pyiocp)
{

	class_<ws>("ws", init<int>())
		.def("send", &ws::send)
		.def("run", &ws::run)
		.def("call_latter", &ws::call_latter)
		.def("get_event", &ws::get_event)
		.def("crash", &ws::crash)
		.def_readonly("on_line_count", &ws::on_line_count)
		;
}
