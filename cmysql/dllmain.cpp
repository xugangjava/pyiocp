// dllmain.cpp : 定义 DLL 应用程序的入口点。
#include "pch.h"
#include <boost/bind.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/enable_shared_from_this.hpp>
#include <boost/asio.hpp>
#include <iostream> 
#include <string>
#include <map>
#include <stdio.h>
#include <fstream>
#include <iomanip>
#include <boost/atomic.hpp> 
#include <windows.h>  
#include <memory>
#include <vector>
#include <boost/thread/thread.hpp>
#include <boost/uuid/uuid.hpp>
#include <boost/uuid/uuid_io.hpp>
#include <boost/uuid/uuid_generators.hpp>
#include <boost/thread/recursive_mutex.hpp>
#include <boost/thread/condition.hpp>
#include <boost/lockfree/queue.hpp>
#include <boost/unordered_map.hpp>
#include <boost/python.hpp>
#include <boost/python/module.hpp>
#include <boost/python/def.hpp>
#include <boost/python/list.hpp>
#include <mysql.h>
using namespace boost::python;
enum { DB_QUERY = 1, DB_EXEC = 2, DB_MANY = 3 };

struct cmysql_result {
	int num_fields;
	int affect_rows;
	int insert_id;
	int sql_type;
	int row_count;
	std::string error;
	boost::python::list columes;
	boost::python::list colume_types;
	boost::python::list rows;
};

class cmysql
{
public:

	static void config(int thread_num) {


	}

	static cmysql_result sql(std::string sql, int sql_type) {
		cmysql_result r;
		auto mysql = mysql_init(NULL);
		if (!mysql) {
			r.error = "mysql_init failed";
			return r;
		}
		if (mysql_real_connect(mysql, "localhost", "root", "bo2016@", "poker", 3306, NULL, 0) == NULL) {
			r.error = "mysql_real_connect failed";
			return r;
		}

		if (mysql_real_query(mysql, sql.c_str(), sql.size()) != 0) {
			r.error = "mysql_real_query failed";
			mysql_close(mysql);
			return r;
		}

		if (DB_QUERY == sql_type) {
			auto sql_res = mysql_store_result(mysql);
			if (sql_res == NULL) {
				r.error = "mysql_store_result no result";
				mysql_close(mysql);
				return r;
			}
			r.sql_type = sql_type;
			r.row_count = sql_res->row_count;
			auto fileds = mysql_fetch_fields(sql_res);
			r.num_fields = mysql_num_fields(sql_res);
			for (int i = 0; i < r.num_fields; i++)
			{
				r.columes.append(str(fileds[i].name, fileds[i].name_length));
				r.colume_types.append((int)fileds[i].type);
			}

			MYSQL_ROW sql_row;
			while (sql_row = mysql_fetch_row(sql_res))
			{
				auto sql_row_length = mysql_fetch_lengths(sql_res);
				list row;
				for (int i = 0; i < r.num_fields; i++)
				{
					row.append(str(sql_row[i], sql_row_length[i]));
				}
				r.rows.append(row);
			}
			mysql_free_result(sql_res);
		}
		if (DB_EXEC == sql_type){
			r.insert_id = mysql_insert_id(mysql);
			r.affect_rows = mysql_affected_rows(mysql);
		}	
		mysql_close(mysql);
		return r;
	}

	static void sql_async(std::string sql) {

	}
};

BOOST_PYTHON_MODULE(cmysql)
{
	class_<cmysql>("cmysql")
		.def("config", &cmysql::config)
		.def("sql", &cmysql::sql)
		.def("sql_async", &cmysql::sql_async)
		;


	class_<cmysql_result>("cmysql_result",no_init)
		.def_readonly("num_fields",&cmysql_result::num_fields)
		.def_readonly("affect_rows", &cmysql_result::affect_rows)
		.def_readonly("insert_id", &cmysql_result::insert_id)
		.def_readonly("sql_type", &cmysql_result::sql_type)
		.def_readonly("row_count", &cmysql_result::row_count)
		.def_readonly("error", &cmysql_result::error)
		.def_readonly("columes", &cmysql_result::columes)
		.def_readonly("colume_types", &cmysql_result::colume_types)
		.def_readonly("rows", &cmysql_result::rows)
		;
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

