// dllmain.cpp : 定义 DLL 应用程序的入口点。
#include "pch.h"
#include <string>
#include <vector>
#include <mysql.h>
#include <rapidjson/document.h>
#include <rapidjson/prettywriter.h>
using namespace rapidjson;
static std::string mysql_host;
static std::string mysql_usr;
static std::string mysql_pwd;
static std::string mysql_db;
struct query_info
{
	char columes[256][48];
	int columes_type[64];
	int columes_count;
};


extern "C" _declspec(dllexport) void cmysql_init(char* host, char* user, char* passwd, char* database) {
	mysql_host = std::string(host);
	mysql_usr = std::string(user);
	mysql_pwd = std::string(passwd);
	mysql_db = std::string(database);
}

extern "C" _declspec(dllexport) void* cmysql_new() {
	return mysql_init(NULL);
}

//暂时只支持一个结果集
extern "C" _declspec(dllexport) void* cmysql_new_res() {
	return new MYSQL_RES[2];
}


extern "C" _declspec(dllexport) int cmysql_connect_async(void* mysql) {
	return mysql_real_connect_nonblocking((MYSQL*)mysql, "localhost", "root", "bo2016@", "poker", 3306, NULL, 0);
}

extern "C" _declspec(dllexport) int cmysql_query_async(void* mysql,char* sql,int sql_sz) {
	return mysql_real_query_nonblocking((MYSQL*)mysql, sql, sql_sz);
}

extern "C" _declspec(dllexport) int cmysql_store_result_async(void* mysql,void* sql_res) {
	return  mysql_store_result_nonblocking((MYSQL*)mysql, (MYSQL_RES**)sql_res);
}

extern "C" _declspec(dllexport) MYSQL_ROW cmysql_fetch_row(void* sql_res) {
	return  mysql_fetch_row(((MYSQL_RES * *)sql_res)[0]);
}


extern "C" _declspec(dllexport) int cmysql_num_fields(void* sql_res) {
	return  mysql_num_fields(((MYSQL_RES * *)sql_res)[0]);
}

extern "C" _declspec(dllexport) MYSQL_FIELD* cmysql_fetch_fields(void* sql_res) {
	return  mysql_fetch_fields(((MYSQL_RES * *)sql_res)[0]);
}

extern "C" _declspec(dllexport) int cmysql_affected_rows(void* mysql) {
	return  mysql_affected_rows((MYSQL*)mysql);
}

extern "C" _declspec(dllexport) int cmysql_insert_id(void* mysql) {
	return  mysql_insert_id((MYSQL*)mysql);
}


extern "C" _declspec(dllexport) void cmysql_free_result(void* sql_res) {
	mysql_free_result(((MYSQL_RES * *)sql_res)[0]);
	delete[] sql_res;
}

extern "C" _declspec(dllexport) void cmysql_free_conn(void* mysql) {
	mysql_close((MYSQL*)mysql);
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

