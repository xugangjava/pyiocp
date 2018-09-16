#include "stdafx.h"
#include "pyiocp.h"



void conn::close() {
	if (on_disconnect) {
		call<void>(on_disconnect, _id);
	}

	m_socket->get_io_service().post(
		boost::bind(
			&conn::_close,
			this)
	);
}

void conn::send(std::string buf) {
	m_socket->async_write_some(boost::asio::buffer(buf),
		boost::bind(
			&conn::handle_write, 
			this,
			_1,
			_2)
	);
	//m_socket->write_some(boost::asio::buffer(buf));
}

void conn::recv() {
	m_socket->async_read_some(boost::asio::buffer(m_data, max_length),
		boost::bind(
			&conn::handle_read, 
			this,
			_1,
			_2)
	);
}

void conn::handle_read(const boost::system::error_code& error, size_t bytes_transferred) {
	if (error)
	{
		close();
		return;
	}
	m_socket->get_io_service().post(
		boost::bind(
			&conn::py_on_message,
			this,
			str(m_data,bytes_transferred)
		)
	);
	recv();
}

void conn::handle_write(const boost::system::error_code& error, size_t bytes_transferred) {
	if (error)
	{
		close();
		return;
	}
}




void conn::_close() {
	m_socket->shutdown(tcp::socket::shutdown_receive);
	m_socket->close();
	delete this;
}



server::server(boost::asio::io_service& io_service, int port)
	: m_acceptor(io_service, tcp::endpoint(tcp::v4(), port)) {
	start_accept();
}


void server::start_accept() {
	conn* new_connection = new conn();
	new_connection->init(m_acceptor.get_io_service());
	m_acceptor.async_accept(
		*new_connection->socket(),
		boost::bind(
			&server::handle_accept,
			this,
			new_connection,
			_1
		)
	);
}

void server::handle_accept(conn* new_connection, const boost::system::error_code& error) {
	if (!error)
	{
		if (on_connect) {
			call<void>(on_connect, ptr(new_connection));
		}
		//boost::asio::socket_base::keep_alive kp(true);
		//new_connection->socket()->set_option(kp);
		//boost::asio::socket_base::send_buffer_size sbz(8192);
		//new_connection->socket()->set_option(sbz);
		new_connection->recv();
	}

	start_accept();
}


void pyiocp::listen(int port) {
	boost::asio::io_service io_service;
	server server(io_service, port);
	io_service.run();
}

void pyiocp::bind(std::string m, PyObject *callable) {
	if (m == "on_connect")on_connect = callable;
	else if (m == "on_disconnect")on_disconnect = callable;
	else if (m == "on_message")on_message = callable;
}



BOOST_PYTHON_MODULE(pyiocp)
{
	class_<pyiocp>("pyiocp")
		.def("listen", &pyiocp::listen)
		.def("bind", &pyiocp::bind)
		;

	class_<conn>("pyconn", no_init)
		.def("send", &conn::send)
		.def("close", &conn::close)
		.add_property("id", &conn::get_id);
		;
}

