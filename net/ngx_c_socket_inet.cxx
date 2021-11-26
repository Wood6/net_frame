// 
#include <arpa/inet.h>

#include "ngx_c_socket.h"
#include "ngx_func.h"

//

/**
 * 功能：
	将socket绑定的地址转换为文本格式【根据参数1给定的信息，获取地址端口字符串，返回这个字符串的长度】

 * 输入参数：(struct sockaddr *sa, int port, u_char *text, size_t len)
	sa 客户端的ip地址信息一般在这里。
	port 为1，则表示要把端口信息也放到组合成的字符串里，为0，则不包含端口信息
	text 文本写到这里
	len 文本的宽度在这里记录

 * 返回值：

 * 调用了函数：

 * 其他说明：

 * 例子说明：

 */
size_t CSocket::SocketNtop(struct sockaddr *sa, int port, u_char *text, size_t len)
{
	struct sockaddr_in* sin;
	u_char* p;

	switch (sa->sa_family)
	{
	case AF_INET:
		sin = (struct sockaddr_in *) sa;
		p = (u_char *)&sin->sin_addr;
		if (port)                                   // 端口信息也组合到字符串里
		{
			p = SnPrintf(text, len, "%ud.%ud.%ud.%ud:%d", p[0], p[1], p[2], p[3], ntohs(sin->sin_port));  // 返回的是新的可写地址
		}
		else           // 不需要组合端口信息到字符串中
		{
			p = SnPrintf(text, len, "%ud.%ud.%ud.%ud", p[0], p[1], p[2], p[3]);
		}
		return (p - text);
		break;

	default:
		return 0;
		break;
	}

	return 0;
}
