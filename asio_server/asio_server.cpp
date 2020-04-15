// asio_server.cpp : 此文件包含 "main" 函数。程序执行将在此处开始并结束。
//
#include <string>
#pragma pack(push,1)
enum { head_length = 4, packet_length = 8192, uuid_length = 40 };
enum { EVT_CONNECT = 1, EVT_DISCONNECT = 4, EVT_ON_MESSAGE = 2, EVT_SEND_MESSAGE = 3, EVT_ON_TIMER = 5 };
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
		strncpy_s(conn_id, c.c_str(), c.size());
	}

	void set_buf(std::string b) {
		sz = b.size();
		strncpy_s(body, b.c_str(), sz);
	}

	char body[packet_length];
	char head[head_length];
	char conn_id[uuid_length];
};
#pragma pack(pop)
#include <iostream>
#include <boost/bind.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/enable_shared_from_this.hpp>
#include <boost/asio.hpp>
#include <iostream> 
#include <string>
#include <map>
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
#include <boost/unordered_map.hpp>
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

		conn(server* server, boost::asio::io_service& io_service, const std::string conn_id)
			:m_socket(io_service),  id(conn_id) {
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
			//		m_socket.release(ignored_ec);
			m_socket.shutdown(boost::asio::ip::tcp::socket::shutdown_both, ignored_ec);
			m_socket.close(ignored_ec);
		}
	};

public:
	typedef boost::shared_ptr<conn> conn_ptr;
	//typedef boost::unordered_map<std::string, conn_ptr> conn_map;
	tcp::acceptor m_acceptor;

	io_service_pool& pool;
	boost::unordered_map<std::string, conn_ptr> m_all_conn;

	const int m_iocp_time_out;
	explicit server(int port, int iocp_time_out, io_service_pool& p)
		: pool(p),
		m_acceptor(p.get_accept_io_service(), tcp::endpoint(tcp::v4(), port)),
		m_iocp_time_out(iocp_time_out) {

	}

	void send_new_py_event(py_event& evt) {
		que.push(evt);
	}

	void post_start_accept() {
		pool.get_accept_io_service().post(boost::bind(&server::start_accept, this));
	}

	void post_send(std::string conn_id, int msg_type, std::string buf) {
		auto iter = m_all_conn.find(conn_id);
		if (iter == m_all_conn.end())return;
		iter->second->send(msg_type, buf);
		//pool.get_accept_io_service().post(boost::bind(&server::send, this, conn_id, msg_type, buf));
	}

	void post_gc() {
		//pool.get_accept_io_service().post(boost::bind(&server::gc, this));
		//gc();
	}

	boost::atomic<int> on_line_count;

private:

	void gc() {
		auto iter = m_all_conn.begin();
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

	//void send(std::string conn_id, int msg_type, std::string buf) {
	//	auto iter = m_all_conn.find(conn_id);
	//	if (iter == m_all_conn.end())return;
	//	iter->second->send(msg_type, buf);
	//}

	void start_accept() {
		gc();
		boost::uuids::uuid uuid = boost::uuids::random_generator()();
		const std::string tmp_uuid = boost::uuids::to_string(uuid);
		m_all_conn[tmp_uuid] = conn_ptr(new conn(this, pool.get_io_service(), tmp_uuid));

		m_acceptor.async_accept(
			m_all_conn[tmp_uuid]->m_socket,
			boost::bind(&server::handle_accept, this, tmp_uuid, boost::asio::placeholders::error)
		);
	}

	void handle_accept(std::string conn_id, const boost::system::error_code& error) {
		auto iter = m_all_conn.find(conn_id);
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


static server* sv;
extern "C" _declspec(dllexport) void iocp_run(int port, int iocp_time_out, int thread_num) {

	io_service_pool* pool = new io_service_pool(thread_num);
	sv = new server(port, iocp_time_out, *pool);
	sv->m_acceptor.set_option(boost::asio::ip::tcp::acceptor::reuse_address(1));
	sv->m_acceptor.listen();
	sv->post_start_accept();
	pool->run();
}

extern "C" _declspec(dllexport) void iocp_send(int msg_id, const char* p_conn_id, int csz, const char* p_buf, int bsz) {
	std::string conn_id(p_conn_id, csz);
	std::string buf(p_buf, bsz);
	sv->post_send(conn_id, msg_id, buf);

}


extern "C" _declspec(dllexport) int iocp_get_event(py_event* evt) {
	if (que.empty())return 0;
	que.pop(*evt);
	return 1;
}


extern "C" _declspec(dllexport) int iocp_gc() {
	if (!sv)return 0;
	sv->post_gc();
	return sv->on_line_count;
}

void dispose() {
	if (sv) {
		sv->m_all_conn.erase(sv->m_all_conn.begin(), sv->m_all_conn.end());
		sv->pool.stop();
		delete sv;
	}
}
typedef struct
{
	UINT						uMessageSize;			// 数据包大小
	UINT						bMainID;				// 处理主类型
	UINT						bAssistantID;			// 辅助处理类型 ID
	UINT						bHandleCode;			// 数据包处理代码
	UINT						bReserve;				// 保留字段
}  MSG_HEAD;

typedef struct
{
	DWORD						uMessageSize;			// 数据包大小
	DWORD						bMainID;				// 处理主类型
	DWORD						bAssistantID;			// 辅助处理类型 ID
	DWORD						bHandleCode;			// 数据包处理代码
	DWORD						bReserve;				// 保留字段
}  MSG_FOR_SEND;
int main()
{
	std::cout << sizeof(MSG_HEAD);
	std::cout << sizeof(MSG_FOR_SEND);
	iocp_run(9999, 180, 24);
	
	while (1)
	{
		py_event evt;
		iocp_get_event(&evt);
		if (evt.event_type == EVT_ON_MESSAGE) {
			iocp_send(1, evt.conn_id, evt.csz, evt.body, evt.sz);
		}
	}
    std::cout << "Hello World!\n";
}

// 运行程序: Ctrl + F5 或调试 >“开始执行(不调试)”菜单
// 调试程序: F5 或调试 >“开始调试”菜单

// 入门使用技巧: 
//   1. 使用解决方案资源管理器窗口添加/管理文件
//   2. 使用团队资源管理器窗口连接到源代码管理
//   3. 使用输出窗口查看生成输出和其他消息
//   4. 使用错误列表窗口查看错误
//   5. 转到“项目”>“添加新项”以创建新的代码文件，或转到“项目”>“添加现有项”以将现有代码文件添加到项目
//   6. 将来，若要再次打开此项目，请转到“文件”>“打开”>“项目”并选择 .sln 文件
