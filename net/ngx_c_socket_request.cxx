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
	LogStderr(errno, "8888888888888888888.");
}

