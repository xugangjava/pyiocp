// pyiocptest.cpp : 定义控制台应用程序的入口点。
//

#include "stdafx.h"
#include "pyiocp.h"

int main()
{
	pyiocp p;
	p.listen(8991,20000);
	
    return 0;
}

