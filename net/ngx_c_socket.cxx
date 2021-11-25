// 网络相关
#include "ngx_c_socket.h"
#include "ngx_c_conf.h"
#include "ngx_func.h"

#include <sys/socket.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <arpa/inet.h>
#include <fcntl.h>

/**
 * 功能：
	构造函数

 * 输入参数：
	无

 * 返回值：
	无

 * 调用了函数：
	
 * 其他说明：
	

 * 例子说明：

 */
CSocket::CSocket()
{
	m_lister_port_cnt = 1;
}

/**
 * 功能：
	析构函数，释放动态申请的资源

 * 输入参数：
	无

 * 返回值：
	无

 * 调用了函数：

 * 其他说明：


 * 例子说明：

 */
CSocket::~CSocket()
{
	// 释放必须的内存
	std::vector<gp_listening_t>::iterator iter;
	for (iter = m_vec_listen_socket.begin(); iter != m_vec_listen_socket.end(); ++iter)   // vector
	{
		delete (*iter); // 一定要把指针指向的内存干掉，不然内存泄漏
	}

	m_vec_listen_socket.clear();
}

/**
 * 功能：
	Socket初始化入口函数【fork()子进程之前干这个事】
	实质是调用私有 OpenListeningSockets() 函数实现开启监听的功能

 * 输入参数：
	无

 * 返回值：
	成功返回true，失败返回false

 * 调用了函数：
	OpenListeningSockets()

 * 其他说明：

 * 例子说明：

 */
bool CSocket::InitSocket()
{
	bool ret = OpenListeningSockets();
	return ret;
}

/**
 * 功能：
	开启socket监听端口【支持多个端口】
	在创建worker进程之前就要执行这个函数；

 * 输入参数：
	无

 * 返回值：
	无

 * 调用了函数：
	SetNonblocking()

 * 其他说明：
	此函数在类中是private权限的，仅供init调用，外部不可直接调用

 * 例子说明：

 */
bool CSocket::OpenListeningSockets()
{
	bool ret = true;

	CConfig *p_config = CConfig::GetInstance();
	m_lister_port_cnt = p_config->GetIntDefault("listen_port_cnt", m_lister_port_cnt);  // 取得要监听的端口数量

	struct sockaddr_in serv_addr;
	memset(&serv_addr, 0, sizeof(serv_addr));
	serv_addr.sin_family = AF_INET;                  // 选择协议族为IPV4
	serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);   // 监听本地所有的IP地址；INADDR_ANY表示的是一个服务器上所有的网卡
													 // （服务器可能不止一个网卡）多个本地ip地址都进行绑定端口号，进行侦听。

	int isock = -1;       // 套接字fd
	int iport = 0;        // 端口
	char arr_tmp[100];    // 临时字符串 

	for (int i = 0; i < m_lister_port_cnt; i++) //要监听这么多个端口
	{
		// 参数1：AF_INET：使用ipv4协议，一般就这么写
		// 参数2：SOCK_STREAM：使用TCP，表示可靠连接【相对还有一个UDP套接字，表示不可靠连接】
		// 参数3：给0，固定用法，就这么记
		isock = socket(AF_INET, SOCK_STREAM, 0); //系统函数，成功返回非负描述符，出错返回-1
		if (isock == -1)
		{
			LogStderr(errno, "CSocekt::Initialize()中socket()失败,i=%d.", i);
			// 其实这里直接退出，那如果以往有成功创建的socket呢？就没得到释放吧，当然走到这里表示程序不正常，应该整个退出，也没必要释放了 
			return false;
		}

		// setsockopt(): 设置一些套接字参数选项；
		// 参数2：是表示级别，和参数3配套使用，也就是说，参数3如果确定了，参数2就确定了;
		// 参数3：允许重用本地地址
		// 设置 SO_REUSEADDR，目的第五章第三节讲解的非常清楚：主要是解决TIME_WAIT这个状态导致bind()失败的问题
		int reuseaddr = 1;  // 1:打开对应的设置项
		if (setsockopt(isock, SOL_SOCKET, SO_REUSEADDR, (const void *)&reuseaddr, sizeof(reuseaddr)) == -1)
		{
			LogStderr(errno, "CSocekt::Initialize()中setsockopt(SO_REUSEADDR)失败,i=%d.", i);
			close(isock); // 无需理会是否正常执行了                                                  
			return false;
		}
		// 设置该socket为非阻塞
		if (SetNonblocking(isock) == false)
		{
			LogStderr(errno, "CSocekt::Initialize()中setnonblocking()失败,i=%d.", i);
			close(isock);
			return false;
		}

		// 设置本服务器要监听的地址和端口，这样客户端才能连接到该地址和端口并发送数据        
		arr_tmp[0] = 0;
		sprintf(arr_tmp, "listen_port%d", i);              // 格式化端口名字字符
		iport = p_config->GetIntDefault(arr_tmp, 10000);
		serv_addr.sin_port = htons((in_port_t)iport);      // in_port_t其实就是uint16_t

		// 绑定服务器地址结构体
		if (bind(isock, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) == -1)
		{
			LogStderr(errno, "CSocekt::Initialize()中bind()失败,i=%d.", i);
			close(isock);
			return false;
		}

		// 开始监听
		if (listen(isock, NGX_LISTEN_BACKLOG) == -1)
		{
			LogStderr(errno, "CSocekt::Initialize()中listen()失败,i=%d.", i);
			close(isock);
			return false;
		}

		// 可以，放到vector里来
		gp_listening_t p_listen_socket = new gs_listening;          // 千万不要写错，注意前边类型是指针，后边类型是一个结构体
		memset(p_listen_socket, 0, sizeof(gs_listening));           // 注意后边用的是 ngx_listening_t而不是lpngx_listening_t
		p_listen_socket->port = iport;                              // 记录下所监听的端口号
		p_listen_socket->fd = isock;                                // 套接字木柄保存下来   
		LogErrorCore(NGX_LOG_INFO, 0, "监听%d端口成功!", iport);    // 显示一些信息到日志中
		m_vec_listen_socket.push_back(p_listen_socket);             // 加入到队列中
	} 

	return ret;
}

/**
 * 功能：
	设置socket连接为非阻塞模式【这种函数的写法很固定】：
	非阻塞即不断调用，不断调用这种：拷贝数据的时候是阻塞的

 * 输入参数：(int sockfd)
	sockfd  socket套接字fd

 * 返回值：
	成功 true，失败 false

 * 调用了函数：

 * 其他说明：

 * 例子说明：

 */
bool CSocket::SetNonblocking(int sockfd)
{
	bool ret = true;
	int nb = 1; //0：清除，1：设置  
	if (ioctl(sockfd, FIONBIO, &nb) == -1)   // FIONBIO：设置/清除非阻塞I/O标记：0：清除，1：设置
	{
		return ret = false;
	}

	return ret = true;

	// 如下也是一种写法，跟上边这种写法其实是一样的，但上边的写法更简单
	/*
	// fcntl:file control【文件控制】相关函数，执行各种描述符控制操作
	// 参数1：所要设置的描述符，这里是套接字【也是描述符的一种】
	int opts = fcntl(sockfd, F_GETFL);  //用F_GETFL先获取描述符的一些标志信息
	if(opts < 0)
	{
		ngx_log_stderr(errno,"CSocekt::setnonblocking()中fcntl(F_GETFL)失败.");
		return false;
	}
	opts |= O_NONBLOCK; // 把非阻塞标记加到原来的标记上，标记这是个非阻塞套接字【如何关闭非阻塞呢？opts &= ~O_NONBLOCK,然后再F_SETFL一下即可】
	if(fcntl(sockfd, F_SETFL, opts) < 0)
	{
		ngx_log_stderr(errno,"CSocekt::setnonblocking()中fcntl(F_SETFL)失败.");
		return false;
	}
	return true;
	*/
}

/**
 * 功能：
	关闭socket，

 * 输入参数：
	无

 * 返回值：
	无

 * 调用了函数：

 * 其他说明：
	什么时候用，我们现在先不确定，先把这个函数预备在这里

 * 例子说明：

 */
void CSocket::CloseListeningSockets()
{
	for (int i = 0; i < m_lister_port_cnt; i++)     // 要关闭这么多个监听端口
	{
		close(m_vec_listen_socket[i]->fd);
		LogErrorCore(NGX_LOG_INFO, 0, "关闭监听端口%d!", m_vec_listen_socket[i]->port); // 显示一些信息到日志中
	}
}


	



