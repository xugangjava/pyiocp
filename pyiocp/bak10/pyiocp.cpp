#include "stdafx.h"
#include "pyiocp.h"




BOOST_PYTHON_MODULE(pyiocp)
{
	class_<pyiocp>("pyiocp", init<int>())
		.def("send", &pyiocp::send)
		.def("run", &pyiocp::run)
		.def("call_latter", &pyiocp::call_latter)
		.def_readonly("on_line_count", &pyiocp::on_line_count)
		;

	class_<py_event>("py_event")
		.def_readonly("conn_id", &py_event::conn_id)
		.def_readonly("event_type", &py_event::event_type)
		.def_readonly("msg_type", &py_event::msg_type)
		.def_readonly("buf", &py_event::buf)
		;
}

