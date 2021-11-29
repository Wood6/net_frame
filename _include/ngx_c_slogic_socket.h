
#ifndef __NGX_C_SLOGIC_SOCKET_H__
#define __NGX_C_SLOGIC_SOCKET_H__

#include <sys/socket.h>

#include "ngx_c_socket.h"

//处理逻辑和通讯的子类
class CLogicSocket : public CSocket   // 继承自父类CScoekt
{
public:
	CLogicSocket();                           // 构造函数
	virtual ~CLogicSocket();                  // 释放函数
	virtual bool InitSocket();                // 初始化函数

public:
	// 各种业务逻辑相关函数都在这类
	bool _HandleRegister(gps_connection_t p_conn, gps_msg_header_t p_msg_header, char* p_pkg_body, unsigned short len_body);
	bool _HandleLogIn(gps_connection_t p_conn, gps_msg_header_t p_msg_header, char* p_pkg_body, unsigned short len_body);

public:
	virtual void ThreadRecvProcFunc(char* p_msg_buf);
};

#endif

