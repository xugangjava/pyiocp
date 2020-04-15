// dllmain.cpp : 定义 DLL 应用程序的入口点。
#include "pch.h"
#include <Python.h>
static PyMethodDef PodMethods[] = {
 {NULL, NULL, 0, NULL}    /* Sentinel */ };

PyMODINIT_FUNC initpodpb(void)
{
	PyObject* m;
	m = Py_InitModule("prokerpb", PodMethods); if (m == NULL)  return;
}

BOOL APIENTRY DllMain( HMODULE hModule,
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

