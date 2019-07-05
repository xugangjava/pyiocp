// iocp.cpp : 定义控制台应用程序的入口点。
//

#include "stdafx.h"



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
#define _WEBSOCKETPP_NO_CPP11_SYSTEM_ERROR_
#include <websocketpp/config/asio_no_tls.hpp>
#include <websocketpp/server.hpp>

using boost::asio::ip::tcp;
enum { head_length = 4, packet_length = 8192, uuid_length = 40 };
enum { EVT_CONNECT = 1, EVT_DISCONNECT = 4, EVT_ON_MESSAGE = 2, EVT_SEND_MESSAGE = 3, EVT_ON_TIMER = 5 };

#pragma pack(push,1)
struct py_event {

	int sz;
	int csz;
	int msg_type;
	int event_type;

	py_event() {
		sz = msg_type = 0;
		event_type = 0;
	}
	void set_conn_id(std::string c) {
		csz = c.size();
		strncpy(conn_id, c.c_str(), c.size());
	}

	void set_buf(std::string b) {
		sz = b.size();
		strncpy(body, b.c_str(), sz);
	}

	std::string get_buf() {
		return std::string(body, sz);
	}

	std::string get_conn_id() {
		return std::string(conn_id, csz);
	}


	//char buf[packet_length];
	char body[packet_length];
	char head[head_length];
	char conn_id[uuid_length];
};
#pragma pack(pop)


#pragma region wesocket

boost::lockfree::queue<py_event, boost::lockfree::capacity<10000>> ws_que;

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
	private:
		long m_idle;
		websocketpp::connection_hdl  m_hdl;

	public:
		bool is_closed;
		ws_conn(websocketpp::connection_hdl hdl) {
			m_hdl = hdl;
			m_idle = 0;
		}

		~ws_conn() {
			close();
		}

		websocketpp::connection_hdl hdl() {
			return m_hdl;
		}

		long idle() {
			return m_idle;
		}

		void refresh() {
			m_idle = (long)time(0);
		}

		void close() {
			if (is_closed)return;
			is_closed = true;
		}

	};
	typedef boost::unordered_map<std::string, boost::shared_ptr<ws_conn>> conn_map;

public:
	int m_thread_num;
	wsserver* sv;
	conn_map m_all_conn;
	long m_iocp_time_out;
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
		const std::string conn_id = sv->ws_ptr()->get_con_from_hdl(hdl)->get_remote_endpoint();
		sv->m_all_conn[conn_id] = boost::shared_ptr<ws_conn>(new ws_conn(hdl));
		py_event packet;
		packet.set_conn_id(conn_id);
		packet.event_type = EVT_CONNECT;
		sv->send_new_py_event(packet);
	}

	//断开
	static void on_close(ws* sv, websocketpp::connection_hdl hdl)
	{
		ws_con_ptr con = sv->ws_ptr()->get_con_from_hdl(hdl);
		const std::string conn_id = con->get_remote_endpoint();
		if (!sv->is_exist_conn(conn_id))return;
		sv->remove_conn(conn_id);
		py_event packet;
		packet.set_conn_id(conn_id);
		packet.event_type = EVT_DISCONNECT;
		sv->send_new_py_event(packet);
	}


	//通信
	static void on_message(ws* sv, websocketpp::connection_hdl hdl, ws_msg_ptr msg)
	{
		if (msg->get_opcode() != websocketpp::frame::opcode::binary) {
			return;
		}
		const std::string payload = msg->get_payload();
		if (payload.size() > packet_length - 1)return;
		ws_con_ptr con = sv->ws_ptr()->get_con_from_hdl(hdl);
		const std::string conn_id = con->get_remote_endpoint();
		if (!sv->is_exist_conn(conn_id))return;
		sv->m_all_conn[conn_id]->refresh();
		py_event packet;
		payload.copy(packet.head, 4, 0);
		int length = (packet.head[1] & 0x000000ff) << 8 | (packet.head[0] & 0x000000ff);
		if (length != payload.size())return;
		if (length > packet_length)return;
		payload.copy(packet.body, payload.size() - 4, 4);
		packet.event_type = EVT_ON_MESSAGE;
		packet.msg_type = (packet.head[3] & 0x000000ff) << 8 | (packet.head[2] & 0x000000ff);
		sv->send_new_py_event(packet);
	}



	//移除连接
	void remove_conn(std::string conn_id) {
		if (!is_exist_conn(conn_id))return;
		m_all_conn.erase(m_all_conn.find(conn_id));
	}

	bool is_exist_conn(std::string conn_id) {
		return m_all_conn.find(conn_id) == m_all_conn.end();
	}

	void send_new_py_event(py_event& evt) {
		ws_que.push(evt);
	}


#pragma region 接口函数
	void send(std::string conn_id, int msg_type, std::string buf) {
		if (!is_exist_conn(conn_id))return;
		m_all_conn[conn_id]->refresh();
		int sz = buf.size();
		int total = 4 + sz;
		int length = total - 2;
		char head[4] = {
			(length >> 0) & 0xFF,
			(length >> 8) & 0xFF,
			(msg_type >> 0) & 0xFF,
			(msg_type >> 8) & 0xFF
		};
		std::string packet = buf;
		packet.insert(0, head, sizeof(head));
		sv->send(m_all_conn[conn_id]->hdl(), packet, websocketpp::frame::opcode::binary);
	}

	void run(int port, int iocp_time_out) {
		m_iocp_time_out = iocp_time_out;
		sv = new wsserver();
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





	void call_latter(int sec, std::string timer_id) {}

	void crash() {}
#pragma endregion


};

#pragma endregion


int main()
{
	//listen(8989, 10000, 20);

	/*boost::uuids::uuid uuid = boost::uuids::random_generator()();
	const string tmp_uuid = boost::uuids::to_string(uuid);
	cout << tmp_uuid.size() << endl;
	system("pause");*/

	ws ws(1);
	ws.run(9055, 180);

	return 0;
}

