#include "stdafx.h"
#include "pyiocp.h"




BOOST_PYTHON_MODULE(pyiocp)
{
	class_<pyiocp>("pyiocp")
		.def("init", &pyiocp::init)
		.def("listen", &pyiocp::listen)
		.def("bind", &pyiocp::bind)
		.def("send", &pyiocp::send)
		.def("close", &pyiocp::close)
		.def("call_latter", &pyiocp::call_latter)
		;

	class_<pypacket>("pypacket")
		.def("recv", &pypacket::recv)
		.def_readonly("msg_type", &pypacket::msg_type)
		.def_readonly("packet", &pypacket::packet)
		;
}

