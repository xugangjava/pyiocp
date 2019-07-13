#pragma once
#include "stdafx.h"
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

	char body[packet_length];
	char head[head_length];
	char conn_id[uuid_length];
};
#pragma pack(pop)