#include "stdafx.h"
#include <boost/python.hpp>
#include <boost/python/module.hpp>
#include <boost/python/def.hpp>

#include <boost/bind.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/enable_shared_from_this.hpp>
#include <boost/asio.hpp>

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

using namespace boost::python;
using boost::asio::ip::tcp;
PyObject *on_connect;
PyObject *on_disconnect;
PyObject *on_message;

class conn:
	public boost::enable_shared_from_this<conn>
{
public:
	conn(boost::asio::io_service& io_service, int conn_id) :m_socket(io_service) {
		_id = conn_id;
		is_free = true;
		is_abort = false;
	};

	int _id;
	int get_id() {
		return _id;
	}

	boost::atomic<bool> is_free;
	boost::atomic<bool> is_abort;
	void py_on_message(int m, str buf) {
		if (on_message) {
			call<void>(on_message, _id, m, buf);
		}
	}

	tcp::socket& socket() {
		return m_socket;
	}

	tcp::socket  m_socket;
	enum { head_length = 4, packet_length = 8192 };
	BYTE m_head[head_length];
	int m_msg_type;
	int m_body_len;
	char m_body[packet_length];

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
			m_socket.get_io_service().post(
				boost::bind(
					&conn::py_on_message,
					shared_from_this(),
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
			boost::bind(
				&conn::handle_write,
				shared_from_this(),
				_1,
				_2)
		);
	
	/*	m_socket.async_write_some(
			boost::asio::buffer(buf),
			boost::bind(
				&conn::handle_write,
				shared_from_this(),
				_1,
				_2)
		);*/
		//m_socket->write_some(boost::asio::buffer(buf));
	}

	void handle_write(const boost::system::error_code& error, size_t bytes_transferred) {
		if (error)
		{
			close();
		}
	}

	bool close() {
		if (on_disconnect) {
			call<void>(on_disconnect, _id);
		}
		try {
			m_socket.release();
			m_socket.close();
			m_msg_type = m_body_len = 0;
			memset(m_head, 0, head_length);
			memset(m_body, 0, packet_length);
			is_free = true;
		}
		catch (boost::system::system_error e) {
			return false;
		}
		return true;
	}
};

struct server
{
	tcp::acceptor m_acceptor;

	typedef boost::shared_ptr<conn> conn_ptr;
	typedef boost::unordered_map<int, conn_ptr> conn_map;

	boost::atomic<int> m_conn_index;

	conn_map m_all_conn;

	server(boost::asio::io_service& io_service, int port, int max_connection)
		: m_acceptor(io_service, tcp::endpoint(tcp::v4(), port)) {
		for (int i = 0; i < max_connection; i++) {
			m_all_conn[i] = conn_ptr(new conn(m_acceptor.get_io_service(), i));
		}
		m_conn_index = 0;
		start_accept();
	}

	conn_ptr get_free_conn() {
		while (1) {
			if (m_conn_index == m_all_conn.size()) {
				m_conn_index = 0;
			}
			conn_ptr c = m_all_conn[m_conn_index++];
			if (c->is_free) {
				return c;
			}
		}
		return NULL;
	}

	void start_accept() {
		conn_ptr new_connection = get_free_conn();
		//new_connection->init(m_acceptor.get_io_service());
		m_acceptor.async_accept(
			new_connection->socket(),
			boost::bind(
				&server::handle_accept,
				this,
				new_connection,
				_1
			)
		);
	}

	void recovery(int conn_id) {
		m_all_conn[conn_id] = conn_ptr(new conn(m_acceptor.get_io_service(), conn_id));
	}

	void handle_accept(conn_ptr new_connection, const boost::system::error_code& error) {
		if (!error)
		{
			if (on_connect) {
				call<void>(on_connect, new_connection->get_id());
			}
			new_connection->recv();
		}
		else {
			new_connection->close();
			//ÖØÖÃÁ¬½Ó
			recovery(new_connection->get_id());
		}
		start_accept();
	}
};



struct pyiocp
{
	server* m_server;

	void listen(int port, int max_connection) {
		boost::asio::io_service io_service;
		m_server = new server(io_service, port, max_connection);
		io_service.run();
	}

	void bind(std::string m, PyObject *callable) {
		if (m == "on_connect")on_connect = callable;
		else if (m == "on_disconnect")on_disconnect = callable;
		else if (m == "on_message")on_message = callable;
	}

	void send(int conn_id, int msg_type, std::string buf) {
		

		server::conn_map::iterator it = m_server->m_all_conn.find(conn_id);
		if (it != m_server->m_all_conn.end()) {

			it->second->send(msg_type,buf);
		}
	}

};




