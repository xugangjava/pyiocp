// dllmain.cpp : 定义 DLL 应用程序的入口点。
#include "pch.h"
#include <winsock2.h>
#include <windows.h>
#include <string>
#include <iostream>
#include <time.h>
#include<boost/unordered_map.hpp>
#include <boost/lockfree/queue.hpp>
#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "kernel32.lib")
#pragma pack(push,1)
enum { head_length = 4, packet_length = 8192, uuid_length = 40 };
enum { EVT_CONNECT = 1, EVT_DISCONNECT = 4, EVT_ON_MESSAGE = 2, EVT_SEND_MESSAGE = 3, EVT_ON_TIMER = 5 };
struct py_event {
	SOCKET   sock;
	OVERLAPPED                  Overlapped;
	WSABUF buf;
	int sz;
	int csz;
	int msg_type;
	int event_type;
	DWORD offset;

	py_event() {
		reset();
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
	char conn_id[uuid_length];

	void reset() {
		csz = sz = msg_type = 0;
		event_type = 0;
		ZeroMemory(body, packet_length);
		ZeroMemory(conn_id, uuid_length);
		buf.buf = body;
		buf.len = packet_length;
	}


	void recv() {
		CreateIoCompletionPort((HANDLE)socket, g_hIOCP, 0, 0);
		DWORD dwFlags = 0;
		int nRet =WSARecv(sock, &buf, 1, &offset, &dwFlags, &Overlapped, NULL);
		if (nRet == SOCKET_ERROR && (ERROR_IO_PENDING != WSAGetLastError())) {
			close();
			return;
		}
	}


	void send() {
		
	}
	
	void connect() {

	}

	void close() {
		closesocket(sock);

	}


};
#pragma pack(pop)
boost::unordered_map<std::string, py_event*> m_all_conn;
boost::lockfree::queue<py_event, boost::lockfree::capacity<10000>> que;
int m_iocp_time_out;

HANDLE g_hIOCP;

DWORD WINAPI WorkerThread(LPVOID WorkThreadContext) {
	py_event* evt = NULL;
	DWORD nBytes = 0;
	DWORD dwFlags = 0;
	int nRet = 0;

	DWORD dwIoSize = 0;
	void* lpCompletionKey = NULL;
	LPOVERLAPPED lpOverlapped = NULL;
	while (1) {
		GetQueuedCompletionStatus(g_hIOCP, &dwIoSize, (PULONG_PTR)& lpCompletionKey, (LPOVERLAPPED*)& lpOverlapped, INFINITE);
		evt = (py_event*)lpOverlapped;
		switch (evt->event_type)
		{
		case EVT_ON_MESSAGE:
			evt->recv();
			break;

		case EVT_CONNECT:
			evt->connect();
			break;

		case EVT_DISCONNECT:
			evt->close();
			break;
		}
	}

}

extern "C" _declspec(dllexport) void iocp_run(int port, int iocp_time_out, int thread_num) {
	for (int i = 0; i < thread_num; ++i) {
		DWORD   dwThreadId;
		HANDLE hThread = CreateThread(NULL, 0, WorkerThread, 0, 0, &dwThreadId);
		CloseHandle(hThread);
	}

	WSADATA wsaData;
	WSAStartup(MAKEWORD(2, 2), &wsaData);
	SOCKET    m_socket = WSASocket(AF_INET, SOCK_STREAM, IPPROTO_TCP, NULL, 0, WSA_FLAG_OVERLAPPED);
	sockaddr_in server;
	server.sin_family = AF_INET;
	server.sin_port = htons(port);
	server.sin_addr.S_un.S_addr = htonl(INADDR_ANY);
	bind(m_socket, (sockaddr*)& server, sizeof(server));
	listen(m_socket, SOMAXCONN);
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
	
	/*iter->second->send(msg_type, buf);

	DWORD dwFlags = 0;
	int nRet = WSASend(sock, &buf, 1, &offset, dwFlags, &Overlapped, NULL);
	if (SOCKET_ERROR == nRet && WSA_IO_PENDING != WSAGetLastError())
	{
		close();
	
	}*/

}


extern "C" _declspec(dllexport) int iocp_get_event(py_event* evt) {


}


extern "C" _declspec(dllexport) int iocp_gc() {
	/*auto iter = m_all_conn.begin();
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
	on_line_count = m_all_conn.size();*/
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

