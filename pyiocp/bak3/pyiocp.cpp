#include "stdafx.h"
#include "pyiocp.h"




BOOST_PYTHON_MODULE(pyiocp)
{
	class_<pyiocp>("pyiocp")
		.def("init", &pyiocp::init)
		.def("listen", &pyiocp::listen)
		.def("bind", &pyiocp::bind)
		.def("send", &pyiocp::send)
		.def("call_latter", &pyiocp::call_latter)
		;
}

