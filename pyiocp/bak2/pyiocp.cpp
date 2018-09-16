#include "stdafx.h"
#include "pyiocp.h"




BOOST_PYTHON_MODULE(pyiocp)
{
	class_<pyiocp>("pyiocp")
		.def("listen", &pyiocp::listen)
		.def("bind", &pyiocp::bind)
		.def("send", &pyiocp::send)
		;

	//class_<conn>("pyconn", no_init)
	//	.def("send", &conn::send)
	//	.def("close", &conn::close)
	//	.add_property("id", &conn::get_id);
	//	;
}

