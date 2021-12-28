// 网络相关
#include "ngx_global.h"
#include "ngx_c_socket.h"
#include "ngx_c_conf.h"
#include "ngx_func.h"
#include "ngx_c_memory.h"
#include "ngx_c_lockmutex.h"

#include <iostream>
#include <sys/socket.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <pthread.h>


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
    LogErrorCoreAddPrintAddr(NGX_LOG_INFO, 0, "");
    
	// 配置相关
	m_worker_connections_n = 1;         // epoll连接最大项数
	m_lister_port_cnt      = 1;         // 监听一个端口
	m_recy_connection_wait_time = 60;   // 等待这么些秒后才回收连接

	// epoll相关
	m_handle_epoll = -1;                // epoll返回的句柄

    // 一些和网络通讯有关的常用变量值，供后续频繁使用时提高效率
    m_len_pkg_header = sizeof(gs_pkg_header_t);    // 包头的sizeof值【占用的字节数】
    m_len_msg_header = sizeof(gs_msg_header_t);    // 消息头的sizeof值【占用的字节数】

    // 各种队列相关
    m_send_msg_list_n = 0;              // 发消息队列大小
    m_totol_recy_connection_n = 0;      // 待释放连接队列大小

	m_multimap_timer_size = 0;          // 当前计时队列尺寸
	m_multimap_timer_front_value = 0;   // 当前计时队列头部的时间值
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
    LogErrorCoreAddPrintAddr(NGX_LOG_INFO, 0, "");
    
	// 释放必须的内存
	// (1)监听端口相关内存的释放--------
	std::vector<gps_listening_t>::iterator iter;
	for (iter = m_vec_listen_socket.begin(); iter != m_vec_listen_socket.end(); ++iter)   // vector
	{
		delete (*iter); // 一定要把指针指向的内存干掉，不然内存泄漏
	}
    
	m_vec_listen_socket.clear();
}

/**
 * 功能：
 	关闭退出函数[子进程中执行]

 * 输入参数：
 	无

 * 返回值：
	无

 * 调用了函数：

 * 其他说明：

 * 例子说明：

 */
void CSocket::ShutdownSubproc()
{
    LogErrorCoreAddPrintAddr(NGX_LOG_INFO, 0, "");
    // (1)把干活的线程停止掉，注意 系统应该尝试通过设置 g_stopEvent = 1来 开始让整个项目停止
    //    用到信号量的，可能还需要调用一下sem_post
    if(sem_post(&m_sem_send_event)==-1)  //让ServerSendQueueThread()流程走下来干活
    {
         LogStderr(0,"CSocket::Shutdown_subproc()中sem_post(&m_sem_send_event)失败.");
    }
    
    std::vector<_thread_item*>::iterator iter;
	for(iter = m_vec_thread.begin(); iter != m_vec_thread.end(); iter++)
    {
        pthread_join((*iter)->_Handle, NULL); // 等待一个线程终止
    }
    // (2)释放一下new出来的_thread_item【线程池中的线程】    
	for(iter = m_vec_thread.begin(); iter != m_vec_thread.end(); iter++)
	{
		if(*iter)
			delete *iter;
	}
	m_vec_thread.clear();

    // (3)队列相关
    //clearMsgSendQueue();
	ClearConnectionPool();
	ClearAllFromTimerMultimap();
    
    // (4)多线程相关    
    pthread_mutex_destroy(&m_mutex_connection);          // 连接相关互斥量释放
    pthread_mutex_destroy(&m_mutex_send_msg);            // 发消息互斥量释放    
    pthread_mutex_destroy(&m_mutex_recyList_connection); // 连接回收队列相关的互斥量释放
    sem_destroy(&m_sem_send_event);                      // 发消息相关线程信号量释放
	pthread_mutex_destroy(&m_mutex_ping_timer);            // 时间处理队列相关的互斥量释放

    LogErrorCoreAddPrintAddr(NGX_LOG_INFO, 0, "Socket类回收程序 ShutdownSubproc() 成功执行完，Socket类相关程序全部正常结束");
}

/**
 * 功能：
    清理TCP发送消息队列（list数据结构）

 * 输入参数：
 	无

 * 返回值：
	无

 * 调用了函数：

 * 其他说明：

 * 例子说明：

 */
void CSocket::ClearSendMsgList()
{
    LogErrorCoreAddPrintAddr(NGX_LOG_INFO, 0, "");
    
	char * sTmpMempoint;
	CMemory *p_memory = CMemory::GetInstance();
	
	while(!m_list_send_msg.empty())
	{
		sTmpMempoint = m_list_send_msg.front();
		m_list_send_msg.pop_front(); 
		p_memory->FreeMemory(sTmpMempoint);
	}	
}

/**
 * 功能：
    主动关闭一个连接时的要做些善后的处理函数

 * 输入参数：(gps_connection_t p_conn)
 	p_conn 主动关闭的连接

 * 返回值：
	无

 * 调用了函数：

 * 其他说明：

 * 例子说明：

 */
void CSocket::ManualCloseSocketProc(gps_connection_t p_conn)
{
    LogErrorCoreAddPrintAddr(NGX_LOG_INFO, 0, "");
    
	if (true == m_is_enable_ping_timer)
	{
		DeleteFromTimerMultimap(p_conn); // 从时间队列中把连接干掉
	}
	if (p_conn->fd != -1)
	{
		close(p_conn->fd);  // 这个socket关闭，关闭后epoll就会被从红黑树中删除，所以这之后无法收到任何epoll事件
		p_conn->fd = -1;
	}

	if (p_conn->atomi_sendbuf_full_flag_n > 0)
		--p_conn->atomi_sendbuf_full_flag_n;   // 归0

	AddRecyConnectList(p_conn);  // 将此链接放入延迟回收队列
}

/**
 * 功能：
	专门用于读各种配置项

 * 输入参数：
	无

 * 返回值：
	无

 * 调用了函数：

 * 其他说明：


 * 例子说明：

 */
void CSocket::ReadConf()
{
    LogErrorCoreAddPrintAddr(NGX_LOG_INFO, 0, "");
    
	CConfig *p_config = CConfig::GetInstance();
	m_worker_connections_n = p_config->GetIntDefault("worker_connections", m_worker_connections_n);  // epoll连接的最大项数
	m_lister_port_cnt = p_config->GetIntDefault("listen_port_cnt", m_lister_port_cnt);               // 取得要监听的端口数量
    m_recy_connection_wait_time  = p_config->GetIntDefault("recy_connection_wait_time",m_recy_connection_wait_time); //等待这么些秒后才回收连接

	m_is_enable_ping_timer = p_config->GetIntDefault("enable_socket_wait_time", 0) ? true : false;    // 是否开启踢人时钟，true开启   false不开启
	m_ping_wait_time = p_config->GetIntDefault("socket_max_wait_time", m_ping_wait_time);        // 多少秒检测一次是否 心跳超时，只有当m_is_enable_ping_timer = true时，本项才有用	
	m_ping_wait_time = (m_ping_wait_time > 5) ? m_ping_wait_time : 5;                            // 不建议低于5秒钟，因为无需太频繁
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
    LogErrorCoreAddPrintAddr(NGX_LOG_INFO, 0, "");
    
	ReadConf();

    if(OpenListeningSockets() == false)
        return false;
    
	return true;
}

/**
 * 功能：
	子进程中才需要执行的初始化函数

 * 输入参数：
	无

 * 返回值：
	无

 * 调用了函数：

 * 其他说明：


 * 例子说明：

 */

bool CSocket::InitSubproc()
{ 
    LogErrorCoreAddPrintAddr(NGX_LOG_INFO, 0, "");
    
    int err = 0;
    
    // 发消息互斥量初始化
    err = pthread_mutex_init(&m_mutex_send_msg, NULL);
    if(err != 0)
    {        
        LogErrorCoreAddPrintAddr(NGX_LOG_ERR, 0, "pthread_mutex_init(&m_mutex_send_msg)创建失败，err = %d", err);
        return false;    
    }
    // 连接相关互斥量初始化
    err = pthread_mutex_init(&m_mutex_connection, NULL);
    if(err != 0)
    {
        LogErrorCoreAddPrintAddr(NGX_LOG_ERR, 0, "pthread_mutex_init(&m_mutex_connection)创建失败，err = %d", err);
        return false;    
    }    
    // 连接回收队列相关互斥量初始化
    err = pthread_mutex_init(&m_mutex_recyList_connection, NULL);
    if(err != 0)
    {
        LogErrorCoreAddPrintAddr(NGX_LOG_ERR, 0, "pthread_mutex_init(&m_mutex_recyList_connection)创建失败，err = %d", err);
        return false;    
    } 
	// 和时间处理队列有关的互斥量初始化
	err = pthread_mutex_init(&m_mutex_ping_timer, NULL);
	if (err != 0)
	{
		LogErrorCoreAddPrintAddr(NGX_LOG_ERR, 0, "pthread_mutex_init(&m_mutex_ping_timer)创建失败，err = %d", err);
		return false;
	}
   
    // 初始化发消息相关信号量，信号量用于进程/线程 之间的同步，虽然 
    // 互斥量[pthread_mutex_lock]和 条件变量[pthread_cond_wait]都是线程之间的同步手段，
    
    // 这里用信号量实现 则 更容易理解，更容易简化问题，使用书写的代码短小且清晰；
    // 第二个参数=0，表示信号量在线程之间共享，如果非0表示在进程之间共享
    // 第三个参数=0，表示信号量的初始值。 为0时调用sem_wait()就会卡在那里卡着
    if(sem_init(&m_sem_send_event, 0, 0) == -1)
    {
        LogErrorCoreAddPrintAddr(NGX_LOG_ERR, errno, "sem_init()初始化信号量失败");
        return false;
    }

    // 创建线程 ---------------------------
    _thread_item *p_send_list;               // 专门用来发送数据的线程
    p_send_list = new _thread_item(this);
    m_vec_thread.push_back(p_send_list);     // 创建 一个新线程对象 并入到容器中 
    // 创建线程绑定入口函数为ServerSendListThread，错误不设置到errno，一般返回错误码
    err = pthread_create(&p_send_list->_Handle, NULL, ServerSendListThread, p_send_list); 
    if(err != 0)
    {
        LogErrorCoreAddPrintAddr(NGX_LOG_ERR, 0, "pthread_create()创建发送数据的线程失败，err = %d", err);
        return false;
    }
    
    _thread_item* p_recy_conn;                // 专门用来回收连接的线程
    p_recy_conn = new _thread_item(this);
    m_vec_thread.push_back(p_recy_conn);      // 创建 一个新线程对象 并入到容器中 
    // 创建线程，错误不返回到errno，一般返回错误码
    err = pthread_create(&p_recy_conn->_Handle, NULL, ServerRecyConnectionThread, p_recy_conn); 
    if(err != 0)
    {
        LogErrorCoreAddPrintAddr(NGX_LOG_ERR, 0, "pthread_create()创建回收连接的线程失败，err = %d", err);
        return false;
    }

	if (m_is_enable_ping_timer)      // 是否开启踢人时钟，true开启，false不开启
	{
		_thread_item* p_time_monitor_thread;  // 专门用来处理到期不发心跳包的用户踢出的线程
		m_vec_thread.push_back(p_time_monitor_thread = new _thread_item(this));
		err = pthread_create(&p_time_monitor_thread->_Handle, NULL, ServerTimerQueueMonitorThread, p_time_monitor_thread);
		if (err != 0)
		{
			LogErrorCoreAddPrintAddr(NGX_LOG_ERR, 0, "pthread_create(ServerTimerQueueMonitorThread)失败, err = %d", err);
			return false;
		}
	}

    return true;
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
    LogErrorCoreAddPrintAddr(NGX_LOG_INFO, 0, "");
    
	bool ret = true;

	struct sockaddr_in serv_addr;
	memset(&serv_addr, 0, sizeof(serv_addr));
	serv_addr.sin_family = AF_INET;                  // 选择协议族为IPV4
	serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);   // 监听本地所有的IP地址；INADDR_ANY表示的是一个服务器上所有的网卡
													 // （服务器可能不止一个网卡）多个本地ip地址都进行绑定端口号，进行侦听。

	int isock = -1;       // 套接字fd
	int iport = 0;        // 端口
	char arr_tmp[100];    // 临时字符串 

	// 中途用到一些配置信息
	CConfig* p_config = CConfig::GetInstance();
	for (int i = 0; i < m_lister_port_cnt; i++) //要监听这么多个端口
	{
		// 参数1：AF_INET：使用ipv4协议，一般就这么写
		// 参数2：SOCK_STREAM：使用TCP，表示可靠连接【相对还有一个UDP套接字，表示不可靠连接】
		// 参数3：给0，固定用法，就这么记
		isock = socket(AF_INET, SOCK_STREAM, 0); // 系统函数，成功返回非负描述符，出错返回-1
		if (isock == -1)
		{
			LogStderrAddPrintAddr(errno, "CSocket::Initialize()中socket()失败,i=%d.", i);
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
			LogStderrAddPrintAddr(errno, "CSocket::Initialize()中setsockopt(SO_REUSEADDR)失败,i=%d.", i);
			close(isock); // 无需理会是否正常执行了                                                  
			return false;
		}
		// 设置该socket为非阻塞
		if (SetNonblocking(isock) == false)
		{
			LogStderrAddPrintAddr(errno, "CSocket::Initialize()中setnonblocking()失败,i=%d.", i);
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
			LogStderrAddPrintAddr(errno, "CSocket::Initialize()中bind()失败,i=%d.", i);
			close(isock);
			return false;
		}

		// 开始监听
		if (listen(isock, NGX_LISTEN_BACKLOG) == -1)
		{
			LogStderrAddPrintAddr(errno, "CSocket::Initialize()中listen()失败,i=%d.", i);
			close(isock);
			return false;
		}

		// 可以，放到vector里来
		gps_listening_t p_listen_socket = new _gs_listening;        // 千万不要写错，注意前边类型是指针，后边类型是一个结构体
		memset(p_listen_socket, 0, sizeof(_gs_listening));          // 注意后边用的是 ngx_listening_t而不是lpngx_listening_t
		p_listen_socket->port = iport;                              // 记录下所监听的端口号
		p_listen_socket->fd = isock;                                // 套接字句柄保存下来   
		LogErrorCoreAddPrintAddr(NGX_LOG_INFO, 0, "监听端口[%d]成功!", iport);          // 显示一些信息到日志中
		m_vec_listen_socket.push_back(p_listen_socket);             // 加入到队列中
	}  // enf for(int i = 0; i < m_lister_port_cnt; i++) 

	if (m_vec_listen_socket.size() <= 0)  // 不可能一个端口都不监听,若如此打上日志并返回false
	{
        LogErrorCoreAddPrintAddr(NGX_LOG_ALERT, 0, "执行完OpenListeningSockets()主体部分了，一个监听端口都没有成功打开，"
                                                   "m_vec_listen_socket.size() = %d", m_vec_listen_socket.size());
		return ret = false;
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
    LogErrorCoreAddPrintAddr(NGX_LOG_INFO, 0, "参数: sockfd = %d", sockfd);
    
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
		LogStderrAddPrintAddr(errno,"CSocket::setnonblocking()中fcntl(F_GETFL)失败.");
		return false;
	}
	opts |= O_NONBLOCK; // 把非阻塞标记加到原来的标记上，标记这是个非阻塞套接字【如何关闭非阻塞呢？opts &= ~O_NONBLOCK,然后再F_SETFL一下即可】
	if(fcntl(sockfd, F_SETFL, opts) < 0)
	{
		LogStderrAddPrintAddr(errno,"CSocket::setnonblocking()中fcntl(F_SETFL)失败.");
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
    LogErrorCoreAddPrintAddr(NGX_LOG_INFO, 0, "");
    
	for (int i = 0; i < m_lister_port_cnt; i++)     // 要关闭这么多个监听端口
	{
		close(m_vec_listen_socket[i]->fd);
		LogErrorCore(NGX_LOG_INFO, 0, "关闭监听端口%d!", m_vec_listen_socket[i]->port); // 显示一些信息到日志中
	}
}


/**
 * 功能：
	将一个待发送消息入到发消息队列中

 * 输入参数：(char *p_sendbuf) 
	p_sendbuf 指针，指向业务处理函数已经填充好
	          要回发给客户端的数据的一段内存

 * 返回值：
	无

 * 调用了函数：
    系统函数：sem_post()

 * 其他说明：
    业务处理逻辑函数，譬如_HandleRegister()处理完业务逻辑后，
    将要回发给客户端的信息填充到一段内存buf中，
    然后将其传递给这里这个函数进行发送

 * 例子说明：

 */
void CSocket::SendMsg(char* p_sendbuf) 
{
	LogErrorCoreAddPrintAddr(NGX_LOG_INFO, 0, "参数: p_sendbuf = %p", p_sendbuf);

    CLock lock(&m_mutex_send_msg);      // 互斥量
    m_list_send_msg.push_back(p_sendbuf);    
    ++m_send_msg_list_n;                // 原子操作

    // 将信号量的值+1,这样其他卡在sem_wait的就可以走下去
    if(sem_post(&m_sem_send_event)==-1)  // 让ServerSendQueueThread()流程走下来干活
    {
        LogErrorCoreAddPrintAddr(NGX_LOG_ERR, errno, "sem_post()信号量+1失败");    
    }
}


/**
 * 功能：
	epoll功能初始化

 * 输入参数：
	无

 * 返回值：
	成功返回1， 失败返回exit(2)

 * 调用了函数：
	主要系统调用：epoll_create(),
	主要调用自定义函数：ngx_get_connection((*pos)->fd),
						c->rhandler = &CSocket::ngx_event_accept,
						ngx_epoll_add_event((*pos)->fd,...);

 * 其他说明：
	子进程中进行 ，本函数被 InitWorkerProcess() 所调用

 * 例子说明：

 */
int CSocket::InitEpoll()
{
    LogErrorCoreAddPrintAddr(NGX_LOG_INFO, 0, "");
    
	// (1)创建一个epoll对象，这个对象中有一个红黑树指针，一个双向链表指针
	// 很多内核版本不处理epoll_create的参数，只要该参数>0即可
	m_handle_epoll = epoll_create(m_worker_connections_n);
	if (-1 == m_handle_epoll)
	{
		LogStderrAddPrintAddr(errno, "CSocket::InitEpoll()中epoll_create()失败.");
		exit(2);  // 这是致命问题了，直接退，资源由系统释放吧，这里不刻意释放了，比较麻烦
	}
	
	//（2）创建一个连接池
	InitConnectionPool();

	// （3）遍历所有监听socket【监听端口】，我们为每个监听socket增加一个 连接池中的连接
	// 【说白了就是让一个socket和一个内存绑定，以方便记录该sokcet相关的数据、状态等等】
	std::vector<gps_listening_t>::iterator iter;
	for (iter = m_vec_listen_socket.begin(); iter != m_vec_listen_socket.end(); ++iter)
	{
		//p_conn = GetElementOfConnection((*iter)->fd);
		gps_connection_t p_conn = GetConnectionFromCPool((*iter)->fd);
		if (NULL == p_conn)
		{
			// 这是致命问题，刚开始怎么可能连接池就为空呢？
			LogStderrAddPrintAddr(errno, "CSocket::ngx_epoll_init()中ngx_get_connection()失败.");
			exit(2); // 直接退，资源由系统释放吧，这里不刻意释放了，比较麻烦
		}
		p_conn->p_listening = (*iter);     // 连接对象 和监听对象关联，方便通过连接对象找监听对象
		(*iter)->p_connection = p_conn;    // 监听对象 和连接对象关联，方便通过监听对象找连接对象

		// 对监听端口的读事件设置处理方法，
		// 因为监听端口是用来等对方连接的发送三路握手的，所以监听端口关心的就是读事件
		p_conn->read_handler = &CSocket::EventAccept;    // 这个是监听套接字的读事件，注意与连接套接字上的读事件区分开来
		                                                 // 监听套接字读事件是用来建立连接的

		// 往监听socket上增加监听事件，从而开始让监听端口履行其职责
		// 【如果不加这行，虽然端口能连上，但不会触发ngx_epoll_process_events()里边的epoll_wait()往下走】
        if(EpollOperEvent((*iter)->fd,            // socekt句柄
                            EPOLL_CTL_ADD,        // 事件类型，这里是增加
                            EPOLLIN | EPOLLRDHUP, // 标志，这里代表要增加的标志,EPOLLIN可读，  
                                                  // EPOLLRDHUP，对端的TCP连接的远端关闭或者半关闭（2.6.17内核版本引入）
                            0,                    // 对于事件类型为增加的，不需要这个参数
							p_conn                // 连接池中的连接 
                            ) == -1) 
                {
                    exit(2); //有问题，直接退出，日志 已经写过了
                }

            
	} 

	return 1;
}

/**
 * 功能：
	对epoll事件的具体操作

 * 输入参数：(int fd, uint32_t event_type, uint32_t flag, int otherflag, gps_connection_t p_conn )
	fd          句柄，一个socket
	event_type  事件类型，一般是EPOLL_CTL_ADD，EPOLL_CTL_MOD，EPOLL_CTL_DEL ，说白了就是操作epoll红黑树的节点(增加，修改，删除)
	flag        标志，具体含义取决于event_type
	otherflag   补充动作，用于补充flag标记的不足  :  0增加   , 1去掉, 2完全覆盖, event_type是EPOLL_CTL_MOD时这个参数就有用
	p_conn      一个指针【其实是一个连接】，EPOLL_CTL_ADD时增加到红黑树中去，将来epoll_wait时能取出来用

 * 返回值：
	返回值：成功返回1，失败返回-1；

 * 调用了函数：
    主要系统调用：epoll_ctl()
    
 * 其他说明：
	取代原来的EpollAddEvent()

 * 例子说明：

 */
int CSocket::EpollOperEvent(
                        int                fd,               // 句柄，一个socket
                        uint32_t           event_type,       // 事件类型，一般是EPOLL_CTL_ADD，EPOLL_CTL_MOD，EPOLL_CTL_DEL ，
                                                             // 说白了就是操作epoll红黑树的节点(增加，修改，删除)
                        uint32_t           flag,             // 标志，具体含义取决于event_type
                        int                otherflag,        // 补充动作，用于补充flag标记的不足  :  0增加   , 1去掉, 2完全覆盖,event_type是EPOLL_CTL_MOD时这个参数就有用
                        gps_connection_t   p_conn            // p_conn：一个指针【其实是一个连接】，EPOLL_CTL_ADD时增加到红黑树中去，将来epoll_wait时能取出来用
                        )
{
    LogErrorCoreAddPrintAddr(NGX_LOG_INFO, 0, "参数: fd = %d, event_type = %ud, flag = %ud, otherflag = %d, p_conn = %p", fd, event_type, flag, otherflag, p_conn);
    
    struct epoll_event ev;    
    memset(&ev, 0, sizeof(ev));

    if(event_type == EPOLL_CTL_ADD)         // 往红黑树中增加节点；
    {
        // 红黑树从无到有增加节点
        //ev.data.ptr = (void *)p_conn;
        ev.events = flag;                   // 既然是增加节点，则不管原来是啥标记
        p_conn->epoll_events_type = flag;   // 这个连接本身也记录这个标记
    }
    else if(event_type == EPOLL_CTL_MOD)
	{
        // 节点已经在红黑树中，修改节点的事件信息
        ev.events = p_conn->epoll_events_type;  //先把标记恢复回来
        if(otherflag == 0)
        {
            //增加某个标记            
            ev.events |= flag;
        }
        else if(otherflag == 1)
        {
            //去掉某个标记
            ev.events &= ~flag;
        }
        else
        {
            //完全覆盖某个标记            
            ev.events = flag;      //完全覆盖            
        }
        p_conn->epoll_events_type = ev.events; //记录该标记
    }
    else
    {
        // 删除红黑树中节点，目前没这个需求【socket关闭这项会自动从红黑树移除】，所以将来再扩展
        return  1;  // 先直接返回1表示成功
    } 

    // 原来的理解中，绑定ptr这个事，只在EPOLL_CTL_ADD的时候做一次即可，
    // 但是发现EPOLL_CTL_MOD似乎会破坏掉.data.ptr，因此不管是EPOLL_CTL_ADD，还是EPOLL_CTL_MOD，都给进去
    // 找了下内核源码SYSCALL_DEFINE4(epoll_ctl, int, epfd, int, op, int, fd, struct epoll_event_user*, event), 看着真的会覆盖掉：
    // copy_from_user(&epds, event, sizeof(struct epoll_event)))，感觉这个内核处理这个事情太粗暴了
    ev.data.ptr = (void *)p_conn;

    if(epoll_ctl(m_handle_epoll, event_type, fd, &ev) == -1)
    {
        LogStderrAddPrintAddr(errno, "CSocket::ngx_epoll_oper_event()中epoll_ctl(%d,%ud,%ud,%d)失败.",fd,event_type,flag,otherflag);   
        return -1;
    }
    
    return 1;
}

#if 0
/**
 * 功能：
	epoll增加事件，可能被ngx_epoll_init()等函数调用

 * 输入参数：(int fd,int read_event, int write_event,uint32_t otherflag,
              uint32_t event_type, gps_connection_t p_conn)
	fd 句柄，一个socket
	read_event  1是个读事件，0不是
	write_event 1是个写事件，0不是
	otherflag 其他需要额外补充的标记，弄到这里
	event_type 事件类型  ，一般就是用系统的枚举值，增加，删除，修改等;
	p_conn 对应的连接池中的连接的指针

 * 返回值：
	成功返回1，失败返回-1；

 * 调用了函数：
	主要系统调用：epoll_ctl()
	主要调用自定义函数：

 * 其他说明：

 * 例子说明：

 */
int CSocket::EpollAddEvent(int fd,
	int read_event, int write_event,
	uint32_t otherflag,
	uint32_t event_type,
	gps_connection_t p_conn
)
{
	struct epoll_event ev;
	memset(&ev, 0, sizeof(ev));

	if (1 == read_event)   // 是个读事件
	{
		// 读事件，这里发现官方nginx没有使用EPOLLERR，因此我们也不用【有些范例中是使用EPOLLERR的】
		ev.events = EPOLLIN | EPOLLRDHUP;  // EPOLLIN读事件，也就是read ready
										   //【客户端三次握手连接进来，也属于一种可读事件】   EPOLLRDHUP 客户端关闭连接，断连
										   // 似乎不用加EPOLLERR，只用EPOLLRDHUP即可，EPOLLERR/EPOLLRDHUP 
		                                   // 实际上是通过触发读写事件进行读写操作recv write来检测连接异常

		//ev.events |= (ev.events | EPOLLET);  // 只支持非阻塞socket的高速模式【ET：边缘触发】，
		                                       // 就拿accetp来说，如果加这个EPOLLET，则客户端连入时，epoll_wait()只会返回一次该事件，
					                           // 如果用的是EPOLLLT【水平触发：低速模式】，则客户端连入时，
		                                       // epoll_wait()会被触发多次，一直到用accept()来处理；



		// https://blog.csdn.net/q576709166/article/details/8649911
		// 找下EPOLLERR的一些说法：
		// a)对端正常关闭（程序里close()，shell下kill或ctr+c），触发EPOLLIN和EPOLLRDHUP，但是不触发EPOLLERR 和EPOLLHUP。
		// b)EPOLLRDHUP    这个好像有些系统检测不到，可以使用EPOLLIN，
		//   read返回0，删除掉事件，关闭close(fd);如果有EPOLLRDHUP，检测它就可以直到是对方关闭；否则就用上面方法。
		// c)client 端close()联接,server 会报某个sockfd可读，即epollin来临,然后recv一下 ， 如果返回0再掉用epoll_ctl 中的EPOLL_CTL_DEL , 同时close(sockfd)。
		//   有些系统会收到一个EPOLLRDHUP，当然检测这个是最好不过了。只可惜是有些系统，上面的方法最保险；如果能加上对EPOLLRDHUP的处理那就是万能的了。
		// d)EPOLLERR      只有采取动作时，才能知道是否对方异常。即对方突然断掉，是不可能有此事件发生的。
		//   只有自己采取动作（当然自己此刻也不知道），read，write时，出EPOLLERR错，说明对方已经异常断开。
		// e)EPOLLERR 是服务器这边出错（自己出错当然能检测到，对方出错你咋能知道啊）
		// f)给已经关闭的socket写时，会发生EPOLLERR，也就是说，只有在采取行动
		//   （比如读一个已经关闭的socket，或者写一个已经关闭的socket）时候，才知道对方是否关闭了。
		// 这个时候，如果对方异常关闭了，则会出现EPOLLERR，出现Error把对方DEL掉，close就可以了。
	}
	else
	{
		// 其他事件类型待处理
		// .....
	}
	
	if (otherflag != 0)
	{
		ev.events |= otherflag;
	}

	// 以下这段代码抄自nginx官方,因为指针的最后一位【二进制位】肯定不是1，
	// 所以 和 c->instance做 |运算；到时候通过一些编码，既可以取得c的真实地址，又可以把此时此刻的c->instance值取到
	// 比如c是个地址，可能的值是 0x00af0578，对应的二进制是‭101011110000010101111000‬，而 | 1后是0x00af0579
	ev.data.ptr = (void *)((uintptr_t)p_conn | p_conn->instance);  // 把对象弄进去，后续来事件时，用epoll_wait()后，这个对象能取出来用 
														     // 但同时把一个 标志位【不是0就是1】弄进去

	if (epoll_ctl(m_handle_epoll, event_type, fd, &ev) == -1)
	{
		LogStderrAddPrintAddr(errno, "CSocket::EpollAddEvent()中epoll_ctl(%d,%d,%d,%ud,%ud)失败.", fd, read_event, write_event, otherflag, event_type);
		//exit(2); // 这是致命问题了，直接退，资源由系统释放吧，这里不刻意释放了，比较麻烦，后来发现不能直接退；
		return -1;
	}

	return 1;
}
#endif

/**
 * 功能：
	开始获取发生的事件消息

 * 输入参数：
	timer：epoll_wait()阻塞的时长，单位是毫秒；

 * 返回值：
	1：正常返回  ,
	0：有问题返回
	一般不管是正常还是问题返回，都应该保持进程继续运行

 * 调用了函数：
	主要系统调用：epoll_wait()
	主要调用自定义函数：

 * 其他说明：
	本函数被ProcessEventsAndTimers()调用，而ProcessEventsAndTimers()是在子进程的死循环中被反复调用

 * 例子说明：

 */
int CSocket::EpollProcessEvents(int timer)
{
    LogErrorCoreAddPrintAddr(NGX_LOG_INFO, 0, "");
	// 等待事件，事件会返回到m_events里，最多返回NGX_MAX_EVENTS个事件【因为我只提供了这些内存】；
	// 阻塞timer这么长时间除非：a)阻塞时间到达 b)阻塞期间收到事件会立刻返回c)调用时有事件也会立刻返回d)如果来个信号，比如你用kill -1 pid测试
	// 如果timer为-1则一直阻塞，如果timer为0则立即返回，即便没有任何事件
	// 返回值：有错误发生返回-1，错误在errno中，比如你发个信号过来，就返回-1，错误信息是(4: Interrupted system call)
	//       如果你等待的是一段时间，并且超时了，则返回0；
	//       如果返回>0则表示成功捕获到这么多个事件【返回值里】
	int epoll_event_n = epoll_wait(m_handle_epoll, m_arr_events, NGX_MAX_EVENTS, timer);

	if (-1 == epoll_event_n)
	{
		// 有错误发生，发送某个信号给本进程就可以导致这个条件成立，而且错误码根据观察是4；
		// #define EINTR  4，EINTR错误的产生：当阻塞于某个慢系统调用的一个进程捕获某个信号且相应信号处理函数返回时，该系统调用可能返回一个EINTR错误。
		// 例如：在socket服务器端，设置了信号捕获机制，有子进程，当在父进程阻塞于慢系统调用时由父进程捕获到了一个有效信号时，
		// 内核会致使accept返回一个EINTR错误(被中断的系统调用)。
		if (errno == EINTR)
		{
			// 信号所致，直接返回，一般认为这不是毛病，但还是打印下日志记录一下，因为一般也不会人为给worker进程发送消息
			LogErrorCoreAddPrintAddr(NGX_LOG_ERR, errno, "worker子进程收到了系统某个信号，epoll_wait()失败退出!");
			return 1;  // 正常返回
		}
		else
		{
			// 这被认为应该是有问题，记录日志，
            LogErrorCoreAddPrintAddr(NGX_LOG_ALERT, errno, "epoll_wait()失败返回-1，并且不是检测errno!=EINTR，不是信号所致!");
			return 0;   // 非正常返回 
		}
	}

	if (0 == epoll_event_n)    // 超时，但没事件来
	{
		if (timer != -1)
		{
			// 要求epoll_wait阻塞一定的时间而不是一直阻塞，这属于阻塞到时间了，则正常返回
			LogErrorCoreAddPrintAddr(NGX_LOG_INFO, errno, "epoll_wait()阻塞时间到达参数设置的时间，超时返回！")
			return 1;
		}
		// 无限等待【所以不存在超时】，但却没返回任何事件，这应该不正常有问题        
        LogErrorCoreAddPrintAddr(NGX_LOG_ALERT, errno, "epoll_wait()设置无限等待直到有事件才返回，现在返回了却没任何事件，不正常！");
		return 0;  // 非正常返回 
	}

	// 会惊群，一个telnet上来，4个worker进程都会被惊动，都执行下边这个
	//LogErrorCoreAddPrintAddr(NGX_LOG_DEBUG, errno,"惊群测试1:%d",events); 

	// 走到这里，就是属于有事件收到了
	gps_connection_t  p_conn           = NULL;
	uint32_t          epoll_event_type = 0;
	for (int i = 0; i < epoll_event_n; ++i)    // 遍历本次epoll_wait返回的所有事件，注意events才是返回的实际事件数量
	{
		p_conn = (gps_connection_t)(m_arr_events[i].data.ptr);      // EpollOperEvent()给进去的，这里能取出来

		// 能走到这里，我们认为这些事件都没过期，就正常开始处理
		epoll_event_type = m_arr_events[i].events;                  // 取出事件类型

        // 如果是读事件
		if (epoll_event_type & EPOLLIN)  
		{	         
			(this->* (p_conn->read_handler))(p_conn);  // 注意区分监听套接字与连接套接字挂接的读事件的函数是不一样的，即下面两种
			                                           // 如果是个新客户连入如果新连接进入，这里执行的应该是CSocket::EventAccept(p_conn)  
											           // 如果是已经连入，发送数据到这里，则这里执行的应该是 CSocket::ReadRequestHandler()        
		}

        // 如果是写事件【对方关闭连接也触发这个，再研究。。。。。。】
        // 注意上边的 if(epoll_event_type & (EPOLLERR|EPOLLHUP))  epoll_event_type |= EPOLLIN|EPOLLOUT; 读写标记都给加上了
		if (epoll_event_type & EPOLLOUT) 
		{
			// ....待扩展
			// 客户端关闭时，关闭的时候能够执行到这里，
			// 因为上边有if(epoll_event_type & (EPOLLERR|EPOLLHUP))  epoll_event_type |= EPOLLIN|EPOLLOUT; 代码
            if(epoll_event_type & (EPOLLERR | EPOLLHUP | EPOLLRDHUP)) // 客户端关闭，如果服务器端挂着一个写通知事件，则这里条件是可能成立的
            {
                // EPOLLERR：对应的连接发生错误                      8     = 1000 
                // EPOLLHUP：对应的连接被挂起                       16    = 0001 0000
                // EPOLLRDHUP：表示TCP连接的远端关闭或者半关闭连接                     8192   = 0010  0000   0000   0000
                // 我想打印一下日志看一下是否会出现这种情况
                // 8221 = ‭0010 0000 0001 1101‬  ：包括 EPOLLRDHUP ，EPOLLHUP， EPOLLERR
                // LogStderr(errno,"CSocket::ngx_epoll_process_events()中epoll_event_type&EPOLLOUT成立并且epoll_event_type & (EPOLLERR|EPOLLHUP|EPOLLRDHUP)成立,event=%ud。",epoll_event_type); 

                LogErrorCoreAddPrintAddr(NGX_LOG_WARN, errno, "epoll_wait()返回了可写事件，其类型还包含了EPOLLERR | EPOLLHUP | EPOLLRDHUP中一种，"
					                                          "epoll_event_type = %d，此情况有可能是对端这个时候又断开了！", epoll_event_type);
                // 那么走到此肯定是前面投递了写事件的，但对端这种情况又断开了，而投递了写事件意味着 atomi_sendbuf_full_flag_n标记肯定被+1了，
				// 这里断开了，已经没法正常处理写事件了，但也到这里也要当做处理了，避免信号量错误激活其他连接不该触发的写事件，所以这里我们减回
                --p_conn->atomi_sendbuf_full_flag_n;                 
            }
            else
            {
                (this->*(p_conn->write_handler))(p_conn);   // 有数据没有发送完毕，由系统驱动来发送，则这里执行的是 CSocket::WriteRequestHandler()
            }
		}
	} 

	return 1;
}

//--------------------------------------------------------------------
// 原准备用线程处理，但线程也有线程的问题，如果连接池中连接被主流程回收了，
// 很可能这里的代码执行过程中所操纵的连接是 已经被回收的，这会导致程序不稳定；
// 为简化问题，我们不用线程了
// 上面问题已经被重构后的连接延迟回收技术给解决了，现在可以用起来这个线程了。。。。
/**
 * 功能：
	处理发送消息队列的线程

 * 输入参数：(void* thread_data)
	timer：epoll_wait()阻塞的时长，单位是毫秒；

 * 返回值：
	返回值，1：正常返回  ,0：有问题返回
	一般不管是正常还是问题返回，都应该保持进程继续运行

 * 调用了函数：
	主要系统调用：epoll_wait()
	主要调用自定义函数：

 * 其他说明：
	本函数被ProcessEventsAndTimers()调用，而ProcessEventsAndTimers()是在子进程的死循环中被反复调用

 * 例子说明：

 */
void* CSocket::ServerSendListThread(void* thread_data)
{    
    LogErrorCoreAddPrintAddr(NGX_LOG_INFO, 0, "参数: thread_data = %p", thread_data);
    
    _thread_item *pThread = static_cast<_thread_item*>(thread_data);
    CSocket *p_socket_obj = pThread->_pThis;
    
	int err = 0;
    std::list <char *>::iterator  pos, pos2, posend;
    
    char *p_msg_buf                  = NULL;	
    gps_msg_header_t  p_msg_header   = NULL;
	gps_pkg_header_t  p_pkg_header   = NULL;
    gps_connection_t  p_conn         = NULL;
    unsigned short    itmp           = 0;
    ssize_t           send_size      = 0;  

    CMemory *p_memory = CMemory::GetInstance();
    
    while(g_is_stop_programe == 0) // 不退出
    {
        // 如果信号量值>0，则 -1(减1) 并走下去，否则卡这里卡着
        //【为了让信号量值+1，可以在其他线程调用sem_post达到，
        // 实际上在CSocket::SendMsg()调用sem_post就达到了让这里sem_wait走下去的目的】
        // ******如果被某个信号中断，sem_wait也可能过早的返回，错误为EINTR；
        // 整个程序退出之前，也要sem_post()一下，确保如果本线程卡在sem_wait()，也能走下去从而让本线程成功返回
        if(sem_wait(&p_socket_obj->m_sem_send_event) == -1)
        {
            // 失败？及时报告，其他的也不好干啥
            if(errno != EINTR) // 这个我就不算个错误了【当阻塞于某个慢系统调用的一个进程捕获某个信号且相应信号处理函数返回时，该系统调用可能返回一个EINTR错误。】
                LogErrorCoreAddPrintAddr(NGX_LOG_ALERT, errno, "sem_wait()失败！");
        }

        // 一般走到这里都表示需要处理数据收发了
        if(true == g_is_stop_programe)  // 要求整个进程退出
        {
            LogErrorCoreAddPrintAddr(NGX_LOG_INFO, 0, "g_is_stop_programe为ture了，要求整个进程退出了，发消息线程在此退出...");
            break;
        }
            

        if(p_socket_obj->m_send_msg_list_n > 0) // 原子的 
        {
            err = pthread_mutex_lock(&p_socket_obj->m_mutex_send_msg); // 因为我们要操作发送消息对列m_list_send_msg，所以这里要临界            
            if(err != 0) 
                LogErrorCoreAddPrintAddr(NGX_LOG_ALERT, 0, "pthread_mutex_lock()失败，err = %d", err);

            pos    = p_socket_obj->m_list_send_msg.begin();
			posend = p_socket_obj->m_list_send_msg.end();
            while(pos != posend)
            {
                p_msg_buf = (*pos);                          // 拿到的每个消息都是 消息头+包头+包体【但要注意，我们是不发送消息头给客户端的】
                p_msg_header = (gps_msg_header_t)p_msg_buf;  // 指向消息头
                p_pkg_header = (gps_pkg_header_t)(p_msg_buf+p_socket_obj->m_len_msg_header);	//指向包头
                p_conn = p_msg_header->p_conn;

                // 包过期，因为如果 这个连接被回收，比如在ngx_close_connection(),inRecyConnectQueue()中都会自增currse_quence_n
                // 而且这里有没必要针对 本连接 来用m_connectionMutex临界 ,只要下面条件成立，肯定是客户端连接已断，要发送的数据肯定不需要发送了
                if(p_conn->currse_quence_n != p_msg_header->currse_quence_n) 
                {
                    // 本包中保存的序列号与p_conn【连接池中连接】中实际的序列号已经不同，丢弃此消息，小心处理该消息的删除
                    pos2 = pos;
                    pos++;
                    p_socket_obj->m_list_send_msg.erase(pos2);
                    
                    --p_socket_obj->m_send_msg_list_n;       //  发送消息队列容量少1		
                    p_memory->FreeMemory(p_msg_buf);
                    
                    continue;
                } 

                if(p_conn->atomi_sendbuf_full_flag_n > 0) 
                {
                    // 靠系统驱动来发送消息，所以这里不能再发送
                    pos++;
                    continue;
                }
            
                // 走到这里，可以发送消息，一些必须的信息记录，要发送的东西也要从发送队列里干掉
                p_conn->p_sendbuf_array_mem_addr = p_msg_buf;  //  发送后释放用的，因为这段内存是new出来的
                pos2=pos;
				pos++;
                p_socket_obj->m_list_send_msg.erase(pos2);
                --p_socket_obj->m_send_msg_list_n;             // 发送消息队列容量少1	
                p_conn->p_sendbuf = (char *)p_pkg_header;      // 要发送的数据的缓冲区指针，因为发送数据不一定全部都能发送出去，
                                                               // 我们要记录数据发送到了哪里，需要知道下次数据从哪里开始发送
                itmp = ntohs(p_pkg_header->len_pkg);           // 包头+包体 长度 ，打包时用了htons【本机序转网络序】，
                                                               // 所以这里为了得到该数值，用了个ntohs【网络序转本机序】；
                p_conn->len_send = itmp;                       // 要发送多少数据，因为发送数据不一定全部都能发送出去，我们需要知道剩余有多少数据还没发送
                                
                // 这里是重点，我们采用 epoll水平触发的策略，能走到这里的，都应该是还没有投递 写事件 到epoll中
                // epoll水平触发发送数据的改进方案：
	            // 开始不把socket写事件通知加入到epoll,当我需要写数据的时候，直接调用write/send发送数据；
	            // 如果返回了EAGIN【发送缓冲区满了，需要等待可写事件才能继续往缓冲区里写数据】，此时，我再把写事件通知加入到epoll，
	            // 此时，就变成了在epoll驱动下写数据，全部数据发送完毕后，再把写事件通知从epoll中干掉；
	            // 优点：数据不多的时候，可以避免epoll的写事件的增加/删除，提高了程序的执行效率；                         
                //  (1)直接调用write或者send发送数据
                LogErrorCoreAddPrintAddr(NGX_LOG_INFO, errno, "即将发送的数据长度是[%ud]", p_conn->len_send);

                send_size = p_socket_obj->SendProc(p_conn,p_conn->p_sendbuf,p_conn->len_send); //  注意参数
                if(send_size > 0)
                {                    
                    if(send_size == static_cast<ssize_t>(p_conn->len_send)) // 成功发送出去了数据，一下就发送出去这很顺利
                    {
                        // 成功发送的和要求发送的数据相等，说明全部发送成功了 发送缓冲区去了【数据全部发完】
                        p_memory->FreeMemory(p_conn->p_sendbuf_array_mem_addr);  // 释放内存
                        p_conn->p_sendbuf_array_mem_addr = NULL;
                        p_conn->atomi_sendbuf_full_flag_n = 0;                   // 这行其实可以没有，因此此时此刻这东西就是=0的                        
                        LogStderrAddPrintAddr(0, "数据通过send()发送完毕，很好！"); // 做个提示吧，商用时可以干掉
                    }
                    else  // 没有全部发送完毕(EAGAIN)，数据只发出去了一部分，但肯定是因为 发送缓冲区满了,那么
                    {                        
                          // 发送到了哪里，剩余多少，记录下来，方便下次SendProc()时使用
                        p_conn->p_sendbuf = p_conn->p_sendbuf + send_size;
				        p_conn->len_send = p_conn->len_send - send_size;	
                        // 因为发送缓冲区满了，所以现在我要依赖系统通知来发送数据了
                        ++p_conn->atomi_sendbuf_full_flag_n;   // 标记发送缓冲区满了，需要通过epoll事件来驱动消息的继续发送
                                                               // 【原子+1，且不可写成p_conn->atomi_sendbuf_full_flag_n 
                                                               // = p_conn->atomi_sendbuf_full_flag_n +1 ，这种写法不是原子+1】
                        if(p_socket_obj->EpollOperEvent(
                                p_conn->fd,         // socket句柄
                                EPOLL_CTL_MOD,      // 事件类型，这里是增加【因为我们准备增加个写通知】
                                EPOLLOUT,           // 标志，这里代表要增加的标志,EPOLLOUT：可写【可写的时候通知我】
                                0,                  // 对于事件类型为增加的，EPOLL_CTL_MOD需要这个参数, 0增加，   1去掉，2完全覆盖
                                p_conn              // 连接池中的连接
                                ) == -1)
                        {
                            // 有这情况发生？这可比较麻烦，不过先do nothing
                            LogErrorCoreAddPrintAddr(NGX_LOG_ALERT, errno,"CSocket::ServerSendQueueThread()ngx_epoll_oper_event()失败.");
                        }

                        LogStderrAddPrintAddr(errno, "send()没有发完数据，整个要发送的长度为[%d]，实际发送了[%d]，"
                                                     "应该是发送缓冲区满了，已经成功注册epoll可写事件通知等待发送缓存区可写时再次来send()发送",
                                                                      p_conn->len_send, send_size);

                    } 
                    
                    continue;  // 继续处理其他消息                    
                }

                // 能走到这里，应该是有点问题的
                else if(send_size == 0)
                {
                    // 发送0个字节，首先因为我发送的内容不是0个字节的；
                    // 然后如果发送 缓冲区满则返回的应该是-1，而错误码应该是EAGAIN，
                    // 所以我综合认为，这种情况我就把这个发送的包丢弃了【按对端关闭了socket处理】
                    // 这个打印下日志，我还真想观察观察是否真有这种现象发生
                    // 如果对方关闭连接出现send=0，那么这个日志可能会常出现，商用时就 应该干掉
                    // 然后这个包干掉，不发送了
                    p_memory->FreeMemory(p_conn->p_sendbuf_array_mem_addr);  //  释放内存
                    p_conn->p_sendbuf_array_mem_addr = NULL;
                    p_conn->atomi_sendbuf_full_flag_n = 0;                   // 这行其实可以没有，因此此时此刻这东西就是=0的 

                    LogErrorCoreAddPrintAddr(NGX_LOG_WARN, errno, "SendProc()居然返回0？");
                    
                    continue;
                }
                // 能走到这里，继续处理问题
                else if(send_size == -1)
                {
                    // 发送缓冲区已经满了【一个字节都没发出去，说明发送 缓冲区当前正好是满的】
                    ++p_conn->atomi_sendbuf_full_flag_n; // 标记发送缓冲区满了，需要通过epoll事件来驱动消息的继续发送
                    if(p_socket_obj->EpollOperEvent(
                                p_conn->fd,         // socket句柄
                                EPOLL_CTL_MOD,      // 事件类型，这里是增加【因为我们准备增加个写通知】
                                EPOLLOUT,           // 标志，这里代表要增加的标志,EPOLLOUT：可写【可写的时候通知我】
                                0,                  // 对于事件类型为增加的，EPOLL_CTL_MOD需要这个参数, 0：增加   1：去掉 2：完全覆盖
                                p_conn              // 连接池中的连接
                                ) == -1)
                    {
                        //  有这情况发生？这可比较麻烦，不过先do nothing
                        LogErrorCoreAddPrintAddr(NGX_LOG_ALERT, errno, "EpollOperEvent()失败.");
                    }
                    continue;
                }

                else
                {
                    //  能走到这里的，应该就是返回值-2了，一般就认为对端断开了，等待recv()来做断开socket以及回收资源
                    LogErrorCoreAddPrintAddr(NGX_LOG_WARN, errno, "SendProc()返回值为[%d]，可能是对端断开了", send_size);
                    p_memory->FreeMemory(p_conn->p_sendbuf_array_mem_addr);  // 释放内存
                    p_conn->p_sendbuf_array_mem_addr = NULL;
                    p_conn->atomi_sendbuf_full_flag_n = 0;                   //  这行其实可以没有，因此此时此刻这东西就是=0的  
                    continue;
                }

            }

            err = pthread_mutex_unlock(&p_socket_obj->m_mutex_send_msg); 
            if(err != 0)  
                LogErrorCoreAddPrintAddr(NGX_LOG_ALERT, 0, "pthread_mutex_unlock()失败，返回的错误码err = %d", err);
            
        } 
    }
    
    return (void*)0;
}



