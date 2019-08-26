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
	UINT						uMessageSize;			// ���ݰ���С
	UINT						bMainID;				// ����������
	UINT						bAssistantID;			// ������������ ID
	UINT						bHandleCode;			// ���ݰ��������
	UINT						bReserve;				// �����ֶ�
}  MSG_HEAD;

typedef struct
{
	DWORD						uMessageSize;			// ���ݰ���С
	DWORD						bMainID;				// ����������
	DWORD						bAssistantID;			// ������������ ID
	DWORD						bHandleCode;			// ���ݰ��������
	DWORD						bReserve;				// �����ֶ�
}  MSG_FOR_SEND;

typedef struct
{
	UINT								uRoomVer;						//�����汾
	CHAR								szName[64];						//��½����
	CHAR								TML_SN[128];
	CHAR								szMD5Pass[52];					//��½����
	CHAR								szMathineCode[64];				//���������� ��������
	CHAR                                szCPUID[24];					//CPU��ID
	CHAR                                szHardID[24];					//Ӳ�̵�ID
	CHAR								szIDcardNo[64];					//֤����
	CHAR								szMobileVCode[8];				//�ֻ���֤��
	INT									gsqPs;
	INT									iUserID;						//�û�ID��¼�����ID>0��ID��¼
} MSG_GP_S_LogonByNameStruct;


typedef struct 
{
	UINT								uNameID;				//��Ϸ ID
	INT									dwUserID;				//�û� ID
	UINT								uRoomVer;				//�����汾
	UINT								uGameVer;				//��Ϸ�汾
	CHAR								szMD5Pass[50];			//��������
} MSG_GR_S_RoomLogon;

typedef struct 
{
	BYTE						bMaxVer;							///���°汾����
	BYTE						bLessVer;							///��Ͱ汾����
	BYTE						bReserve[2];						///�����ֶ�
	__int64						i64CheckCode;						///���ܺ��У���룬�ɿͻ��˽����ڰ�ͷ�з���
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


