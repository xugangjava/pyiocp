#pragma once
#include "pch.h"
#include <string>

enum { head_length = 4, packet_length = 8192, uuid_length = 40 };
enum { EVT_CONNECT = 1, EVT_DISCONNECT = 4, EVT_ON_MESSAGE = 2, EVT_SEND_MESSAGE = 3, EVT_ON_TIMER = 5 };



typedef		char				CHAR;
typedef		unsigned char       BYTE;

typedef		unsigned short      WORD;

typedef		short               SHORT;

typedef		int					INT;
typedef		unsigned int		UINT;

typedef		long long			LLONG;
typedef		long long			INT64;
typedef		double              DOUBLE;
typedef		float               FLOAT;
typedef		unsigned long		DWORD;
typedef struct 
{
	UINT						uMessageSize;			// 数据包大小
	UINT						bMainID;				// 处理主类型
	UINT						bAssistantID;			// 辅助处理类型 ID
	UINT						bHandleCode;			// 数据包处理代码
	UINT						bReserve;				// 保留字段
}  MSG_HEAD;

typedef struct
{
	DWORD						uMessageSize;			// 数据包大小
	DWORD						bMainID;				// 处理主类型
	DWORD						bAssistantID;			// 辅助处理类型 ID
	DWORD						bHandleCode;			// 数据包处理代码
	DWORD						bReserve;				// 保留字段
}  MSG_FOR_SEND;

typedef struct
{
	UINT								uRoomVer;						//大厅版本
	CHAR								szName[64];						//登陆名字
	CHAR								TML_SN[128];
	CHAR								szMD5Pass[52];					//登陆密码
	CHAR								szMathineCode[64];				//本机机器码 锁定机器
	CHAR                                szCPUID[24];					//CPU的ID
	CHAR                                szHardID[24];					//硬盘的ID
	CHAR								szIDcardNo[64];					//证件号
	CHAR								szMobileVCode[8];				//手机验证码
	INT									gsqPs;
	INT									iUserID;						//用户ID登录，如果ID>0用ID登录
} MSG_GP_S_LogonByNameStruct;


typedef struct 
{
	UINT								uNameID;				//游戏 ID
	INT									dwUserID;				//用户 ID
	UINT								uRoomVer;				//大厅版本
	UINT								uGameVer;				//游戏版本
	CHAR								szMD5Pass[50];			//加密密码
} MSG_GR_S_RoomLogon;

typedef struct 
{
	BYTE						bMaxVer;							///最新版本号码
	BYTE						bLessVer;							///最低版本号码
	BYTE						bReserve[2];						///保留字段
	__int64						i64CheckCode;						///加密后的校验码，由客户端解密在包头中返回
} MSG_S_ConnectSuccess;

struct py_event {
	int event_type;
	int connid_length;
	int body_length;
	char body[packet_length];
	char conn_id[uuid_length];
	MSG_HEAD head;

	py_event() {
		body_length=connid_length = 0;
		event_type = 0;
		ZeroMemory(&head, sizeof(head));
	}

	void set_conn_id(std::string c) {
		connid_length = c.size();
		strncpy_s(conn_id, c.c_str(), connid_length);
	}

	void set_buf(std::string b) {
		body_length = b.size();
		head.uMessageSize = body_length + sizeof(head);
		strncpy_s(body, b.c_str(), body_length);
	}
};


