// pyiocptest.cpp : �������̨Ӧ�ó������ڵ㡣
//

#include "stdafx.h"
#include "pyiocp.h"

int main()
{
	pyiocp p;
	p.listen(8991,20000);
	
    return 0;
}

