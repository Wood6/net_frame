#include "ngx_c_socket.h"
#include "errno.h"
#include "ngx_macro.h"
#include "ngx_func.h"

/**
 * 功能：
	建立新连接专用函数 / 连接断开时也是触发这个函数执行

 * 输入参数：(gps_connection_t p_oldc)
	p_oldc 

 * 返回值：
	无

 * 调用了函数：
	主要系统调用：use_accept4()或者use_accept()
	主要调用自定义函数：ngx_get_connection(),
						ngx_epoll_add_event(),
						setnonblocking(),
						ngx_close_accepted_connection

 * 其他说明：
	当新连接进入时，本函数会被ngx_epoll_process_events()所调用

 * 例子说明：

 */
void CSocket::EventAccept(gps_connection_t p_oldc)
{
	// 因为listen套接字上用的不是ET【边缘触发】，而是LT【水平触发】，意味着客户端连入如果我要不处理，这个函数会被多次调用，
	// 所以，我这里可以不必多次accept()，可以只执行一次accept()
	// 这也可以避免本函数被卡太久，注意，本函数应该尽快返回，以免阻塞程序运行；
	struct sockaddr    my_sockaddr;        // 远端服务器的socket地址
	socklen_t          len_socket;
	int                err;
	int                level;
	int                fd_ret_accept;      // accept()返回的连接套接字fd
	static int         use_accept4 = 1;    // 我们先认为能够使用accept4()函数
	gps_connection_t   p_newc;             // 代表连接池中的一个连接【注意这是指针】

	// LogStderr(0,"这是几个\n"); 这里会惊群，也就是说，epoll技术本身有惊群的问题

	len_socket = sizeof(my_sockaddr);
	do   // 用do，跳到while后边去方便
	{
		if (use_accept4)
		{
			// 从内核获取一个用户端连接，最后一个参数SOCK_NONBLOCK表示返回一个非阻塞的socket，
			// 节省一次ioctl【设置为非阻塞】调用
			fd_ret_accept = accept4(p_oldc->fd, &my_sockaddr, &len_socket, SOCK_NONBLOCK);
		}
		else
		{
			fd_ret_accept = accept(p_oldc->fd, &my_sockaddr, &len_socket);
		}

		// 惊群，有时候不一定完全惊动所有4个worker进程，可能只惊动其中2个等等，
		// 其中一个成功其余的accept4()都会返回-1；错误 (11: Resource temporarily unavailable【资源暂时不可用】) 
		// 所以参考资料：https://blog.csdn.net/russell_tao/article/details/7204260
		// 其实，在linux2.6内核上，accept系统调用已经不存在惊群了（至少我在2.6.18内核版本上已经不存在）。
		// 大家可以写个简单的程序试下，在父进程中bind,listen，然后fork出子进程，
		// 所有的子进程都accept这个监听句柄。这样，当新连接过来时，大家会发现，
		// 仅有一个子进程返回新建的连接，其他子进程继续休眠在accept调用上，没有被唤醒。
		// LogStderr(0,"测试惊群问题，看惊动几个worker进程%d\n",fd_ret_accept); 
		// 【我的结论是：accept4可以认为基本解决惊群问题，但似乎并没有完全解决，有时候还会惊动其他的worker进程】

		if (fd_ret_accept == -1)
		{
			err = errno;

			// 对accept、send和recv而言，事件未发生时errno通常被设置成EAGAIN（意为“再来一次”）或者EWOULDBLOCK（意为“期待阻塞”）
			if (err == EAGAIN) //accept()没准备好，这个EAGAIN错误EWOULDBLOCK是一样的
			{
				// 除非你用一个循环不断的accept()取走所有的连接，不然一般不会有这个错误【我们这里只取一个连接】
				return;
			}
			level = NGX_LOG_ALERT;
			if (err == ECONNABORTED)  // ECONNRESET错误则发生在对方意外关闭套接字后
									  // 【您的主机中的软件放弃了一个已建立的连接--
									  // 由于超时或者其它失败而中止接连(用户插拔网线就可能有这个错误出现)】
			{
				// 该错误被描述为“software caused connection abort”，即“软件引起的连接中止”。原因在于当服务和客户进程在完成用于 TCP 连接的“三次握手”后，
				// 客户 TCP 却发送了一个 RST （复位）分节，在服务进程看来，就在该连接已由 TCP 排队，等着服务进程调用 accept 的时候 RST 却到达了。
				// POSIX 规定此时的 errno 值必须 ECONNABORTED。源自 Berkeley 的实现完全在内核中处理中止的连接，服务进程将永远不知道该中止的发生。
				// 服务器进程一般可以忽略该错误，直接再次调用accept。
				level = NGX_LOG_ERR;
			}
			else if (err == EMFILE || err == ENFILE)    // EMFILE:进程的fd已用尽【已达到系统所允许单一进程所能打开的文件/套接字总数】。
			// 可参考：https://blog.csdn.net/sdn_prc/article/details/28661661   以及 https://bbs.csdn.net/topics/390592927
			// ulimit -n ,看看文件描述符限制,如果是1024的话，需要改大;  打开的文件句柄数过多 ,把系统的fd软限制和硬限制都抬高.
			// ENFILE这个errno的存在，表明一定存在system-wide的resource limits
			// 而不仅仅有process-specific的resource limits。按照常识，process-specific的resource limits
			// 一定受限于system-wide的resource limits
			{
				level = NGX_LOG_CRIT;
			}
			LogErrorCoreAddPrintAddr(level, errno, "CSocket::ngx_event_accept()中accept4()失败!");

			if (use_accept4 && err == ENOSYS) //accept4()函数没实现，坑爹？
			{
				use_accept4 = 0;  // 标记不使用accept4()函数，改用accept()函数
				continue;         // 回去重新用accept()函数搞
			}

			if (err == ECONNABORTED)  //对方关闭套接字
			{
				// 这个错误因为可以忽略，所以不用干啥
				// do nothing
			}

			if (err == EMFILE || err == ENFILE)
			{
				// do nothing，这个官方做法是先把读事件从listen socket上移除，然后再弄个定时器，定时器到了则继续执行该函数，
				// 但是定时器到了有个标记，会把读事件增加到listen socket上去；
				// 我这里目前先不处理吧【因为上边已经写这个日志了】；
			}
			return;
		}  //end if(fd_ret_accept == -1)

		// 走到这里的，表示 accept4()/accept() 成功了       
        if(m_online_user_count >= m_worker_connections_n)  // 用户连接数过多，要关闭该用户socket，因为现在也没分配连接，所以直接关闭即可
        {
            LogErrorCoreAddPrintAddr(NGX_LOG_WARN, 0, "超出系统允许的最大连入用户数[%d]，关闭连入请求[%d]", m_worker_connections_n, fd_ret_accept);  
            close(fd_ret_accept);
            
            return ;
        }
        
		p_newc = GetConnectionFromCPool(fd_ret_accept);   // 这是针对新连入用户的连接，和监听套接字 所对应的连接是两个不同的东西，不要搞混
		if (p_newc == NULL)
		{
			//连接池中连接不够用，那么就得把这个socekt直接关闭并返回了，因为在ngx_get_connection()中已经写日志了，所以这里不需要写日志了
			if (close(fd_ret_accept) == -1)
			{
				LogErrorCoreAddPrintAddr(NGX_LOG_ALERT, errno, "CSocket::ngx_event_accept()中close(%d)失败!", fd_ret_accept);
			}
			return;
		}
		//...........将来这里会判断是否连接超过最大允许连接数，现在，这里可以不处理

		// 成功的拿到了连接池中的一个连接
		memcpy(&p_newc->s_sockaddr, &my_sockaddr, len_socket);  // 拷贝客户端地址到连接对象【要转成字符串ip地址参考函数ngx_sock_ntop()】

		if (!use_accept4)
		{
			// 如果不是用accept4()取得的socket，那么就要设置为非阻塞【因为用accept4()的已经被accept4()设置为非阻塞了】
			if (SetNonblocking(fd_ret_accept) == false)
			{
				// 设置非阻塞居然失败
				CloseConnection(p_newc); // 关闭socket,这种可以立即回收这个连接，无需延迟，
				                         // 因为其上还没有数据收发，谈不到业务逻辑因此无需延迟；
				return; // 直接返回
			}
		}

		p_newc->p_listening = p_oldc->p_listening;             // 连接对象 和监听对象关联，方便通过连接对象找监听对象【关联到监听端口】

        // 到这里，accept()成功与对端的connct()连接成功，accept返回的是连接套接字，注意与前面的监听套接字区分开来
        // 注意理解，当前这里是挂接连接套接字上的事件回调处理函数，要与监听套接字区分开发
		p_newc->read_handler = &CSocket::ReadRequestHandler;   // 设置数据来时的读处理函数，其实官方nginx中是ngx_http_wait_request_handler()
		p_newc->write_handler = &CSocket::WriteRequestHandler; // 设置数据发送时的写处理函数。
		// 客户端应该主动发送第一次的数据，这里将读事件加入epoll监控
        if(EpollOperEvent(fd_ret_accept,        // socekt句柄
                          EPOLL_CTL_ADD,        // 事件类型，这里是增加
                          EPOLLIN | EPOLLRDHUP, // 标志，这里代表要增加的标志,EPOLLIN可读，EPOLLRDHUP表示TCP连接的远端关闭或者半关闭，
                                                // 如果边缘触发模式可以增加，EPOLLET对于事件类型为增加的
                          0,                    // 不需要这个参数 
			              p_newc                // 连接池中的连接
                          ) == -1)         
               {
                   // 增加事件失败，失败日志在ngx_epoll_add_event中写过了，因此这里不多写啥；
                   CloseConnection(p_newc);      // 关闭socket,这种可以立即回收这个连接，无需延迟，因为其上还没有数据收发，谈不到业务逻辑因此无需延迟；

                   return;  // 直接返回
               }

		if (m_is_enable_ping_timer == 1)
		{
			AddToTimerMultimap(p_newc);
		}

        ++m_online_user_count;   // 连入用户数量+1

        break;                   // 一般就是循环一次就跳出去
	} while (1);

	return;
}

