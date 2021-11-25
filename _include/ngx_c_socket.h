// 和网络相关
#ifndef __NGX_SOCKET_H__
#define __NGX_SOCKET_H__

#include <vector>

// 已完成连接队列，nginx给511，我们也先按照这个来：不懂这个数字的5.4
const int NGX_LISTEN_BACKLOG = 511;

// 一些专用结构定义放在这里，暂时不考虑放ngx_global.h里了
typedef struct gs_listening
{
	int port;   // 监听的端口号
	int fd;     // 套接字句柄socket
}gs_listening_t, *gp_listening_t;

// socket 相关类
class CSocket
{
private:
	int m_lister_port_cnt;                            // 默认监听端口数量
	std::vector<gp_listening_t> m_vec_listen_socket;  // 存储套接字的数据vector结构

private:
	bool OpenListeningSockets();       // 监听必须的端口【支持多个端口】
	void CloseListeningSockets();      // 关闭监听套接字
	bool SetNonblocking(int sockfd);   // 设置非阻塞套接字

public:
	CSocket();
	virtual ~CSocket();

	virtual bool InitSocket();          // 初始化函数
};


#endif



