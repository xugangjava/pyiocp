﻿using ProtoBuf;
using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.Data;
using System.Diagnostics;
using System.Drawing;
using System.IO;
using System.Linq;
using System.Net;
using System.Net.Sockets;
using System.Text;
using System.Threading;
using System.Threading.Tasks;
using System.Windows.Forms;

namespace client
{

    public partial class Form1 : Form
    {

        public class WorkThread
        {
            public static IPAddress IP;
            public static int PORT;
            public static IPEndPoint END_POINT;

            public static int socket_count;
            public static long socket_message_count;
            public static long socket_delay;
            public static long socket_total_kb;

            public static Stopwatch Stopwatch= new Stopwatch();
            public static bool RUNING;
            public static TextBox CONSOLE;
            public static byte[] BUF;


            public static void Run()
            {
                Socket client = new Socket(AddressFamily.InterNetwork, SocketType.Stream, ProtocolType.Tcp);
              
                client.Connect(END_POINT);
         
                byte[] recv_buffer = new byte[1024 * 4];


                try
                {
                    socket_count++;
                    while (RUNING)
                    {
                        Stopwatch sw = new Stopwatch();
                        sw.Start();
                        client.Send(BUF);
                        var n=client.Receive(recv_buffer);
                        sw.Stop();
                        socket_delay = sw.ElapsedMilliseconds;
                        socket_message_count++;
                        socket_total_kb += n;
                    }
                }
                finally
                {
                    client.Close();
                    socket_message_count--;
                }

            }

            delegate void SetTextCallback(string text);
            public static void Watch()
            {
                while (RUNING)
                {
                    var sec = Stopwatch.Elapsed.TotalSeconds;
                    var txt = string.Format("当前连接客户端:{0}\r\n 每秒处理消息:{1}\r\n 平均延迟:{2}ms\r\n 速度{3}m/s\r\n",
                        socket_count,
                        socket_message_count / (sec + 1),
                        socket_delay,
                        socket_total_kb/1024 / 1024/(sec + 1)
                        );
                    SetText(txt);
                    Thread.Sleep(500);
                }
            }

            private static void SetText(string text)
            {
                if (CONSOLE.InvokeRequired)
                {
                    while (!CONSOLE.IsHandleCreated)
                    {
                        //解决窗体关闭时出现“访问已释放句柄“的异常
                        if (CONSOLE.Disposing || CONSOLE.IsDisposed)
                            return;
                    }
                    SetTextCallback d = new SetTextCallback(SetText);
                    CONSOLE.Invoke(d, new object[] { text });
                }
                else
                {
                    CONSOLE.Text = text;
                }
            }

        }

  

      
        public Form1()
        {
            InitializeComponent();
        }

        private void BtnStart_Click(object sender, EventArgs e)
        {
            if (WorkThread.RUNING) return;
            var thread_num = Convert.ToInt32(txtThread.Text);
            WorkThread.PORT = Convert.ToInt32(txtPort.Text);
            WorkThread.END_POINT = new IPEndPoint(IPAddress.Parse(txtIP.Text), WorkThread.PORT);
            WorkThread.Stopwatch.Start();
            WorkThread.RUNING = true;
            WorkThread.socket_count = 0;
            WorkThread.socket_message_count = 0;
            WorkThread.socket_delay = 0;
            WorkThread.CONSOLE = txtConsole;

            var ping = new protobuf.FLCSPing
            {
                fTimeStamp = 0.0f,
                dwPingCount = 1,
                dwServerTick = 1,
                iActorID = 1,
            };
            byte[] body;
            using (MemoryStream ms = new MemoryStream())
            {
                Serializer.Serialize(ms, ping);
                body = ms.ToArray();
            }

            var total = 4 + body.Length;
            var length = total - 2;
            byte[] head = new byte[4];
            head[0] = (byte)((length >> 0) & 0xFF);
            head[1] = (byte)((length >> 8) & 0xFF);
            head[2] = (byte)(((int)protobuf.MsgID.MSGID_PING >> 0) & 0xFF);
            head[3] = (byte)(((int)protobuf.MsgID.MSGID_PING >> 8) & 0xFF);
            WorkThread.BUF = head.Concat(body).ToArray();


            for (int i = 0; i < thread_num; i++)
            {
                var tr = new Thread(new ThreadStart(WorkThread.Run))
                {
                    IsBackground = true
                };
                tr.Start();
            }
            new Thread(new ThreadStart(WorkThread.Watch))
            {
                IsBackground = true
            }.Start();
        }

        private void BtnStop_Click(object sender, EventArgs e)
        {
            WorkThread.RUNING = false;

        }

         

    }
}
