#include "ngx_c_socket.h"
#include "ngx_func.h"

/**
 * 功能：
	来数据时候的处理，当连接上有数据来的时候，
	本函数会被EpollProcessEvents()所调用  ,官方的类似函数为ngx_http_wait_request_handler();

 * 输入参数：(gp_connection_t p_c)
	p_c

 * 返回值：
	无

 * 调用了函数：
	主要调用自定义函数：

 * 其他说明：

 * 例子说明：

 */
void CSocket::WaitRequestHandler(gp_connection_t p_c)
{
	//LogStderr(errno, "8888888888888888888.");

	// ET(边缘模式，一次实践系统只给一次信号)测试代码
	/*
	unsigned char buf[10] = { 0 };
	memset(buf, 0, sizeof(buf));
	do
	{
		int n = recv(p_c->fd, buf, 2, 0);   // 每次只收两个字节
		if (n == -1 && errno == EAGAIN)
			break;                          // 数据收完了
		else if (n == 0)
			break;
		LogStderr(0, "OK，收到的字节数为%d,内容为%s", n, buf);
	} while (1);
	*/
	
	//LT测试代码
	unsigned char buf[10]={0};
	memset(buf, 0, sizeof(buf));
	int n = recv(p_c->fd, buf, 2, 0);
	if(n  == 0)
	{
		//连接关闭
		FreeConnection(p_c);
		close(p_c->fd);
		p_c->fd = -1;
	}
	LogStderr(0,"OK，收到的字节数为%d,内容为%s",n,buf);
}

