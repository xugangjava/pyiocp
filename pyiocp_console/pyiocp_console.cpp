#include "stdafx.h"
#include "pyiocp.h"

int main()
{
	pyiocp py;
	py.listen(8989,10);
	return 0;
}