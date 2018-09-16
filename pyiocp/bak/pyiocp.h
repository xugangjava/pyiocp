#include "stdafx.h"
#include <boost/python.hpp>
#include <boost/python/module.hpp>
#include <boost/python/def.hpp>
#include <boost/bind.hpp>
#include <boost/asio.hpp>
#include <boost/bind.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/enable_shared_from_this.hpp>
#include <boost/asio.hpp>
#include <boost/foreach.hpp>
#include <string>
#include<boost/unordered_map.hpp>
#include <boost/uuid/uuid.hpp>            // uuid class
#include <boost/uuid/uuid_generators.hpp> // generators
#include <boost/uuid/uuid_io.hpp>         // streaming operators etc.

using namespace boost::python;
using boost::asio::ip::tcp;
PyObject *on_connect;
PyObject *on_disconnect;
PyObject *on_message;

class conn
{
public:
	//typedef  boost::shared_ptr<conn> pointer;
	void send(std::string buf);
	void close();

	conn() {
		boost::uuids::uuid uuid = boost::uuids::random_generator()();
		_id= boost::lexical_cast<std::string>(uuid);
	};

	boost::shared_ptr<tcp::socket> socket() { return m_socket; }
	void init(boost::asio::io_service& io_service) {
		m_socket = boost::shared_ptr<tcp::socket>(new tcp::socket(io_service));

	};

	std::string _id;
	std::string get_id() {
		return _id;
	}


	void recv();
private:

	void py_on_message(str buf) {
		if (on_message) {
			call<void>(on_message, _id, buf);
		}
	};
	boost::shared_ptr<tcp::socket>  m_socket;
	enum { max_length = 4096 };
	char m_data[max_length];

	void handle_read(const boost::system::error_code& error, size_t bytes_transferred);
	void handle_write(const boost::system::error_code& error, size_t bytes_transferred);

	void _close();
};




class server
{
public:

	server(boost::asio::io_service& io_service, int port);

private:

	void start_accept();

	void handle_accept(conn* new_connection, const boost::system::error_code& error);

	tcp::acceptor m_acceptor;
};



struct pyiocp
{
	void listen(int port);

	void bind(std::string m, PyObject *callable);

};




