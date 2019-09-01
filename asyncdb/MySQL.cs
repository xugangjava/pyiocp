using MySql.Data.MySqlClient;
using System;
using System.Collections.Generic;
using System.Data.SqlClient;
using System.Linq;
using System.Text;

namespace asyncdb
{
    public class MySQL
    {

        public void connect(string connect_string)
        {
            MySqlConnection conn = new MySqlConnection();
            conn.OpenAsync()
        }


        public void query_callbck()
        {

        }

        public void query(string sql)
        {
    
        }

        public void exec(string sql)
        {

        }

        public void close()
        {

        }

    }
}
