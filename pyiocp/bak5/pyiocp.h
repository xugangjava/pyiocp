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
#include <boost/uuid/uuid.hpp>            // uuid class
#include <boost/uuid/uuid_generators.hpp> // generators
#include <boost/uuid/uuid_io.hpp>         // streaming operators etc.
#include <boost/thread/thread.hpp>
#include<stdio.h>
#include<fstream>
#include<iomanip>
#include<map>
#include <boost/atomic.hpp> 
#include<windows.h>  

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

class server
{
	class conn :
		public boost::enable_shared_from_this<conn>
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
		BYTE m_head[head_length];
		char m_body[packet_length];

		int m_msg_type;
		int m_body_len;

		boost::asio::io_service* m_py_service;
		boost::asio::io_service* m_io_service;

		void recv() {
			is_free = false;
			boost::asio::async_read(
				m_socket,
				boost::asio::buffer(m_head, head_length),
				boost::bind(
					&conn::handle_read_head,
					shared_from_this(),
					_1,
					_2
				)
			);
		}

		void handle_read_head(const boost::system::error_code& error, size_t bytes_transferred) {
			if (error)
			{
				close();
			}
			else {
				int length = (m_head[1] & 0x000000ff) << 8 | (m_head[0] & 0x000000ff);
				int total_len = length + 2;
				m_msg_type = (m_head[3] & 0x000000ff) << 8 | (m_head[2] & 0x000000ff);
				m_body_len = total_len - 4;
				boost::asio::async_read(
					m_socket,
					boost::asio::buffer(m_body, m_body_len),
					boost::bind(
						&conn::handle_read_body,
						shared_from_this(),
						_1,
						_2
					)
				);
			}
		}

		void handle_read_body(const boost::system::error_code& error, size_t bytes_transferred) {
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
						str(m_body, bytes_transferred)
					)
				);
				m_msg_type = m_body_len = 0;
				memset(m_head, 0, head_length);
				memset(m_body, 0, packet_length);
				recv();
			}
		}

		void io_send(int msg_type, std::string* buf) {
			int total = 4 + buf->size();
			int length = total - 2;
			char head[4] = {
				(length >> 0) & 0xFF,
				(length >> 8) & 0xFF,
				(msg_type >> 0) & 0xFF,
				(msg_type >> 8) & 0xFF
			};

			std::vector<boost::asio::const_buffer> buffers;
			buffers.push_back(boost::asio::buffer(head));
			buffers.push_back(boost::asio::buffer(*buf));
			boost::asio::async_write(
				m_socket,
				buffers,
				boost::bind(&conn::handle_write, shared_from_this(), _1, _2)
			);
			delete buf;
		}

		void send(int msg_type, std::string buf) {
			std::string* send_buf = new std::string(buf);
			m_io_service->post(
				boost::bind(
					&conn::io_send,
					shared_from_this(),
					msg_type,
					send_buf
				)
			);
		}

		void handle_write(const boost::system::error_code& error, size_t bytes_transferred) {
			if (error) {
				close();
			}
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

	typedef boost::shared_ptr<conn> conn_ptr;

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
			m_all_conn[i] = conn_ptr(new conn(this, i));
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
			conn_ptr c = m_all_conn[m_conn_index++]->shared_from_this();
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
			//û�п�������
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
		m_all_conn[conn_id] = conn_ptr(new conn(this, conn_id));
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
			//��������
			new_connection->close();
			recovery(new_connection->get_id());
		}
		start_accept();
	}
};

DWORD WINAPI Worker(LPVOID lpParam)
{
	//�̺߳���  
	boost::asio::io_service* io = (boost::asio::io_service*)lpParam;
	boost::asio::io_service::work wk(*io);
	io->run();
	return 0;
}




struct pypacket
{
	pypacket() {
		buffer = packet = "";
		msg_type = 0;
	}
	std::string buffer;

	int msg_type;
	std::string packet;

	bool recv(std::string data) {
		buffer += data;
		int len_buf = buffer.size();
		if (len_buf< 4)return false;
		int length = (buffer[1] & 0x000000ff) << 8 | (buffer[0] & 0x000000ff);
		int total_len = length + 2;
		if (len_buf < total_len)return false;
		msg_type = (buffer[3] & 0x000000ff) << 8 | (buffer[2] & 0x000000ff);
		packet = buffer.substr(4, total_len-4);
		buffer = buffer.substr(total_len, len_buf- total_len);
		return true;
	}
};

struct pyiocp
{

	server* m_server;
	boost::asio::io_service* io_service;//io�߳�
	boost::asio::io_service* py_service;//python���߳�
	boost::asio::io_service* timer_service;//��ʱ���߳�

	void init() {
		//py_service = new boost::asio::io_service();
		//io_service = new boost::asio::io_service();
		//timer_service = new boost::asio::io_service();
		//CreateThread(NULL, 0, Worker, io_service, 0, 0);
		//CreateThread(NULL, 0, Worker, timer_service, 0, 0);
		io_service = timer_service = py_service= new boost::asio::io_service();
	} 

	void listen(int port, int max_connection) {
		m_server = new server(io_service, py_service, port, max_connection);
		boost::asio::io_service::work wk(*py_service);
		py_service->run();
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
		//timter�߳���ִ�еȴ�
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
		//������Ϣ
		/*std::ostringstream os;
		os << msg_type;*/
		server::conn_map::iterator it = m_server->m_all_conn.find(conn_id);
		if (it != m_server->m_all_conn.end()) {
			it->second->send(msg_type, buf);
		}
	}

	void close(int conn_id) {
		server::conn_map::iterator it = m_server->m_all_conn.find(conn_id);
		it->second->close();
	}

};




