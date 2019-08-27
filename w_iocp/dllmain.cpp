#include <iostream>
#include <winsock2.h>
#include <windows.h>
#include <mswsock.h>  
#include <string>
#include <iostream>
#include <time.h>
#include <string>
#include<boost/unordered_map.hpp>
#include <boost/lockfree/queue.hpp>
#include <boost/uuid/uuid.hpp>
#include <boost/uuid/uuid_io.hpp>
#include <boost/uuid/uuid_generators.hpp>
#pragma comment(lib, "Ws2_32.lib")
#pragma comment(lib, "Kernel32.lib")
#pragma comment(lib, "Mswsock.lib")
HANDLE g_hIOCP;
enum { head_length = 4, packet_length = 8192, uuid_length = 40 };
#pragma pack(push,1)
struct py_event {
	int sz;
	int csz;
	int msg_type;
	int event_type;
	char body[8192];
	char head[4];
	char conn_id[40];
	void* conn;

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

};
#pragma pack(pop)

struct conn;
enum { EVT_CONNECT = 1, EVT_DISCONNECT = 4, EVT_ON_MESSAGE = 2, EVT_SEND_MESSAGE = 3, EVT_ON_TIMER = 5 };
enum { IO_CONNECT = 1, IO_DISCONNECT = 4, IO_READ = 2, IO_SEND = 3 };
boost::lockfree::queue<py_event, boost::lockfree::capacity<10000>> que;


SOCKET listen_socket;
sockaddr_in listen_address;
int m_iocp_time_out;

boost::unordered_map<std::string, conn*> m_all_conn;
struct packet {

	int offset;
	char _buffer[packet_length];
	std::string conn_id;

	packet() {
		offset = 0;
		ZeroMemory(_buffer, packet_length);
	}

	int push(char* buf, int len) {
		if (offset + len >= packet_length)return -1;
		memcpy(&_buffer[offset], buf, len);
		offset += len;
		if (offset < 4)return 0;
		//获取包长度
		int length = (_buffer[1] & 0x000000ff) << 8 | (_buffer[0] & 0x000000ff);
		int total_length = length + 2;
		if (total_length > packet_length)return -1;
		if (offset >= total_length) {
			//获取完整包
			int msg_type = (_buffer[3] & 0x000000ff) << 8 | (_buffer[2] & 0x000000ff);
			int body_length = total_length - 4;
			//提交消息
			py_event evt;
			memcpy(evt.body, &_buffer[4], body_length);
			evt.sz = body_length;
			evt.msg_type = msg_type;
			evt.event_type = EVT_ON_MESSAGE;
			evt.set_conn_id(conn_id);
			que.push(evt);

			memcpy(&_buffer, &_buffer[total_length], packet_length - total_length);
			offset -= total_length;
		}
		return 0;
	}
};



struct io_evt {
	OVERLAPPED Overlapped;
	int op_type;
	conn* conn;
	WSABUF buf;
	io_evt() {
		conn = NULL;
		op_type = -1;
		ZeroMemory(&Overlapped, sizeof(OVERLAPPED));
		buf.buf = 0;
		buf.len = 0;
	}
};

struct conn
{
	SOCKET sock;
	packet packet;
	std::string conn_id;
	boost::atomic<bool> is_closed;
	int idle;

	conn() {
		is_closed = false;
		idle = 0;
		boost::uuids::uuid uuid = boost::uuids::random_generator()();
		conn_id = boost::uuids::to_string(uuid);
		packet.conn_id = conn_id;
		sock = WSASocket(AF_INET, SOCK_STREAM, 0, NULL, 0, WSA_FLAG_OVERLAPPED);
		CreateIoCompletionPort((HANDLE)sock, g_hIOCP, 0, 0);
	}

	void recv() {
		DWORD dwFlags = 0, offset;
		io_evt* io = new io_evt();
		io->op_type = IO_READ;
		io->conn = this;
		int nRet = WSARecv(sock, &io->buf, 1, &offset, &dwFlags, &io->Overlapped, NULL);
		if (nRet == SOCKET_ERROR && (ERROR_IO_PENDING != WSAGetLastError())) {
			close();
			return;
		}
	}

	void recv_done(char* buf, int offset) {
		int ret = packet.push(buf, offset);
		if (ret < 0) {
			close();
			return;
		}

	}

	void connect() {
		is_closed = false;
		recv();
	}

	void send(char* buf, int len) {
		if (is_closed)return;
		io_evt* io = new io_evt();
		io->op_type = IO_SEND;
		WSABUF buffer;
		buffer.buf = buf;
		buffer.len = len;
		DWORD dwFlags = 0, offset;
		int nRet = WSASend(sock, &buffer, 1, &dwFlags, dwFlags, &io->Overlapped, NULL);
		if (nRet == SOCKET_ERROR && (ERROR_IO_PENDING != WSAGetLastError())) {
			close();
			return;
		}
	}

	void close() {
		if (is_closed)return;
		is_closed = true;
		closesocket(sock);
		py_event evt;
		evt.event_type = EVT_DISCONNECT;
		evt.set_conn_id(conn_id);
		que.push(evt);
	}

	static bool accept() {
		io_evt* evt = new io_evt();
		evt->op_type = IO_CONNECT;
		evt->conn = new conn();
		DWORD dwBytesReceived = 0;
		int ret = AcceptEx(listen_socket, evt->conn->sock,
			NULL, 0,
			sizeof(sockaddr_in) + 16,
			sizeof(sockaddr_in) + 16,
			&dwBytesReceived,
			&(evt->Overlapped));
		if (!ret && WSAGetLastError() != WSA_IO_PENDING) {
			return false;
		}
		return true;
	}

	static void accetp_done(io_evt* io) {
		struct    sockaddr_in    raddr;
		int  raddr_len = sizeof(sockaddr_in);
		py_event evt;
		evt.event_type = EVT_CONNECT;
		evt.set_conn_id(io->conn->conn_id);
		evt.conn = io->conn;
		if (getpeername(io->conn->sock, (struct sockaddr*) & raddr, &raddr_len) == 0) {
			evt.set_buf(inet_ntoa(raddr.sin_addr));
		}
		que.push(evt);

	}

};





DWORD WINAPI work_thread(LPVOID WorkThreadContext) {
	io_evt* evt = NULL;
	DWORD nBytes = 0;
	DWORD dwFlags = 0;
	int nRet = 0;
	DWORD lastError;
	DWORD dwIoSize = 0;
	void* lpCompletionKey = NULL;
	while (1) {
		BOOL rc = GetQueuedCompletionStatus(g_hIOCP,
			&dwIoSize,
			(PULONG_PTR)& lpCompletionKey,
			(LPOVERLAPPED*)& evt,
			INFINITE);
		if (!rc) {
			if (evt != NULL && evt->conn != NULL)evt->conn->close();
			continue;
		}
		if (evt->op_type == IO_CONNECT) {
			conn::accetp_done(evt);
			conn::accept();
			//开始收消息
			evt->conn->recv();
		}
		else if (evt->op_type == IO_READ) {
			//收到消息继续收
			evt->conn->recv_done(evt->buf, evt->buf.len);
			evt->conn->recv();
		}
		delete evt;
	}
}

extern "C" _declspec(dllexport) void iocp_run(int port, int iocp_time_out, int thread_num) {
	for (int i = 0; i < thread_num; ++i) {
		DWORD   dwThreadId;
		HANDLE hThread = CreateThread(NULL, 0, work_thread, 0, 0, &dwThreadId);
		CloseHandle(hThread);
	}
	g_hIOCP = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, thread_num);
	WSADATA wsaData;
	WSAStartup(MAKEWORD(2, 2), &wsaData);
	listen_socket = WSASocket(AF_INET, SOCK_STREAM, IPPROTO_TCP, NULL, 0, WSA_FLAG_OVERLAPPED);
	if (CreateIoCompletionPort((HANDLE)listen_socket, g_hIOCP, 0, 0) == NULL) {
		closesocket(listen_socket);
		throw - 1;
	}
	listen_address.sin_family = AF_INET;
	listen_address.sin_port = htons(port);
	listen_address.sin_addr.S_un.S_addr = htonl(INADDR_ANY);
	bind(listen_socket, (sockaddr*)& listen_address, sizeof(listen_address));
	listen(listen_socket, SOMAXCONN);
	conn::accept();
}

extern "C" _declspec(dllexport) void iocp_send(int msg_type, const char* p_conn_id, int csz, const char* p_buf, int bsz) {

	std::string conn_id(p_conn_id, csz);
	auto iter = m_all_conn.find(conn_id);
	if (iter == m_all_conn.end())return;
	char head[4] = {
		(bsz >> 0) & 0xFF,
		(bsz >> 8) & 0xFF,
		(msg_type >> 0) & 0xFF,
		(msg_type >> 8) & 0xFF
	};
	std::string packet(head);
	packet.append(p_buf, bsz);
	WSABUF buf;
	buf.buf = const_cast<char*>(packet.c_str());
	buf.len = 4 + bsz;
	DWORD dwFlags = 0, offset;
	int nRet = WSASend(iter->second->sock, &buf, 1, &offset, dwFlags, NULL, NULL);
	if (SOCKET_ERROR == nRet && WSA_IO_PENDING != WSAGetLastError())
	{
		iter->second->close();
	}

}


extern "C" _declspec(dllexport) int iocp_get_event(py_event* evt) {
	if (que.empty())return 0;
	que.pop(*evt);
	//处理心跳
	if (evt->event_type == EVT_CONNECT) {
		m_all_conn[evt->conn_id] = (conn*)evt->conn;
	}
	else if (evt->event_type == EVT_DISCONNECT) {
		auto iter = m_all_conn.find(evt->conn_id);
		if (iter != m_all_conn.end()) {
			iter->second->is_closed = true;
		}
	}
	else {
		auto iter = m_all_conn.find(evt->conn_id);
		if (iter != m_all_conn.end()) {
			iter->second->idle = time(0);
		}
	}
	return 1;
}


extern "C" _declspec(dllexport) int iocp_gc() {
	auto iter = m_all_conn.begin();
	while (iter != m_all_conn.end()) {
		conn* c = iter->second;
		if (c->is_closed) {
			m_all_conn.erase(iter++);
			delete c;
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
	return  m_all_conn.size();
}




BOOL APIENTRY DllMain(HMODULE hModule,
	DWORD  ul_reason_for_call,
	LPVOID lpReserved
)
{
	switch (ul_reason_for_call)
	{
	case DLL_PROCESS_ATTACH:
	case DLL_THREAD_ATTACH:
	case DLL_THREAD_DETACH:
	case DLL_PROCESS_DETACH:
		break;
	}
	return TRUE;
}

