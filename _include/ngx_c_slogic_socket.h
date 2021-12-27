
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
	// 通用收发数据相关函数
	void  SendNoBodyPkgToClient(gps_msg_header_t p_msg_header, unsigned short msg_type);

	// 各种业务逻辑相关函数都在这类
	bool _HandleRegister(gps_connection_t p_conn, gps_msg_header_t p_msg_header, char* p_pkg_body, unsigned short len_body);
	bool _HandleLogIn(gps_connection_t p_conn, gps_msg_header_t p_msg_header, char* p_pkg_body, unsigned short len_body);
	bool _HandlePing(gps_connection_t pConn, gps_msg_header_t p_msg_header, char *p_pkg_body, unsigned short len_body);

	virtual void PingTimeOutChecking(gps_msg_header_t p_msg_header, time_t cur_time);      // 心跳包检测时间到，该去检测心跳包是否超时的事宜，本函数只是把内存释放，子类应该重新事先该函数以实现具体的判断动作

public:
	virtual void ThreadRecvProcFunc(char* p_msg_buf);
};

#endif

