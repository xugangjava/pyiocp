// dllmain.cpp : 定义 DLL 应用程序的入口点。
#include "pch.h"
#include "iocp.h"

static server* sv;

extern "C" _declspec(dllexport) MSG_GP_S_LogonByNameStruct login_struct(char* buf, int sz) {
	MSG_GP_S_LogonByNameStruct r;
	ZeroMemory(&r, sizeof(r));
	if (sz != sizeof(r))return r;
	memcpy_s(&r, sz, buf, sz);
	return r;
}

extern "C" _declspec(dllexport) MSG_GR_S_RoomLogon login_room_struct(char* buf, int sz) {
	MSG_GR_S_RoomLogon r;
	ZeroMemory(&r, sizeof(r));
	if (sz != sizeof(r))return r;
	memcpy_s(&r, sz, buf, sz);
	return r;
}


extern "C" _declspec(dllexport) void iocp_run(int port, int iocp_time_out, int thread_num) {

	io_service_pool* pool = new io_service_pool(thread_num);
	sv = new server(port, iocp_time_out, *pool);
	sv->m_acceptor.set_option(boost::asio::ip::tcp::acceptor::reuse_address(1));
	sv->m_acceptor.listen();
	sv->post_start_accept();
	pool->run();
}

extern "C" _declspec(dllexport) void iocp_send(int msg_type, int  case_id, int  handle_code,  const char* p_conn_id, int csz, const char* p_buf, int bsz) {
	std::string conn_id(p_conn_id, csz);
	std::string buf(p_buf, bsz);
	sv->post_send(conn_id, msg_type, case_id, handle_code, buf);
}



extern "C" _declspec(dllexport) int iocp_get_event(py_event* evt) {
	return que.pop(*evt);
}


extern "C" _declspec(dllexport) int iocp_gc() {
	//if (!sv)return 0;
	//sv->post_gc();
	return sv->on_line_count;
}

void dispose() {
	if (sv) {
		sv->m_all_conn.erase(sv->m_all_conn.begin(), sv->m_all_conn.end());
		sv->pool.stop();
		delete sv;
	}
}

BOOL APIENTRY DllMain(HMODULE hModule,
	DWORD  ul_reason_for_call,
	LPVOID lpReserved
)
{
	if (ul_reason_for_call == DLL_PROCESS_DETACH) {
		dispose();
	}
	return TRUE;
}

