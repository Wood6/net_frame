// 和网络  中 客户端发送来数据/服务器端收包 有关的代码


#include "ngx_c_socket.h"
#include "ngx_func.h"
#include "ngx_c_memory.h"
#include "ngx_c_lockmutex.h"  // 自动释放互斥量的一个类
#include "ngx_global.h"       // 调用全局线程池变量 g_threadpool

#include <arpa/inet.h>        // ntohs()
#include <pthread.h>          // pthread多线程相关

/**
 * 功能：
	来数据时候的处理，当连接上有数据来的时候，
	本函数会被EpollProcessEvents()所调用  ,官方的类似函数为ngx_http_wait_request_handler();

 * 输入参数：(gps_connection_t p_conn)
	p_conn

 * 返回值：
	无

 * 调用了函数：
	主要调用自定义函数：

 * 其他说明：

 * 例子说明：

 */
void CSocket::WaitRequestHandler(gps_connection_t p_conn)
{
    // 收包，注意我们用的第二个和第三个参数，我们用的始终是这两个参数，
    // 因此我们必须保证 p_conn->p_recvbuf_pos 指向正确的收包位置，保证 p_conn->len_recv 指向正确的收包宽度
    ssize_t recv_cnt = RecvProc(p_conn, p_conn->p_recvbuf_pos, p_conn->len_recv); 
    if(recv_cnt <= 0)  
    {
        return;    // 该处理的上边这个recvproc()函数处理过了，这里<=0是直接return        
    }

    // 走到这里，说明成功收到了一些字节（>0），就要开始判断收到了多少数据了
    // 连接建立起来时肯定是这个状态，因为在GetElementOfConnection()中已经把pkg_cur_state成员赋值成        PKG_HEAD_INIT
    if(p_conn->pkg_cur_state == PKG_HEAD_INIT)  
    {        
        if(recv_cnt == static_cast<ssize_t>(m_len_pkg_header))  // 正好收到完整包头，这里拆解包头
        {   
            WaitRequestHandlerProcPart1(p_conn); //那就调用专门针对包头处理完整的函数去处理把。
        }
        else
		{
			// 收到的包头不完整--我们不能预料每个包的长度，也不能预料各种拆包/粘包情况，
			// 所以收到不完整包头【也算是缺包】是很可能的；
            p_conn->pkg_cur_state  = PKG_HEAD_RECV_ING;   // 接收包头中，包头不完整，继续接收包头中	
            p_conn->p_recvbuf_pos += recv_cnt;            // 注意收后续包的内存往后走
            p_conn->len_recv      -= recv_cnt;            // 要收的内容当然要减少，以确保只收到完整的包头先
        } 
    } 
    else if(p_conn->pkg_cur_state == PKG_HEAD_RECV_ING)   // 接收包头中，包头不完整，继续接收中，这个条件才会成立
    {
        if(static_cast<ssize_t>(p_conn->len_recv) == recv_cnt)                  // 要求收到的宽度和我实际收到的宽度相等
        {
            // 包头收完整了
            WaitRequestHandlerProcPart1(p_conn);          // 那就调用专门针对包头处理完整的函数去处理把。
        }
        else
		{
			// 包头还是没收完整，继续收包头
            //p_conn->pkg_cur_state  = PKG_HEAD_RECV_ING; // 没必要
            p_conn->p_recvbuf_pos += recv_cnt;            // 注意收后续包的内存往后走
            p_conn->len_recv      -= recv_cnt;            // 要收的内容当然要减少，以确保只收到完整的包头先
        }
    }
    else if(p_conn->pkg_cur_state == PKG_BODY_INIT) 
    {
        //  包头刚好收完，准备接收包体
        if(static_cast<ssize_t>(p_conn->len_recv) == recv_cnt)
        {
            // 收到的宽度等于要收的宽度，包体也收完整了
            WaitRequestHandlerProcLast(p_conn);
        }
        else
		{
			// 收到的宽度小于要收的宽度
			p_conn->pkg_cur_state = PKG_BODY_RECV_ING;					
			p_conn->p_recvbuf_pos += recv_cnt;            // 注意收后续包的内存往后走
            p_conn->len_recv      -= recv_cnt;            // 要收的内容当然要减少，以确保只收到完整的包头先
		}
    }
    else if(p_conn->pkg_cur_state == PKG_BODY_RECV_ING) 
    {
        // 接收包体中，包体不完整，继续接收中
        if(static_cast<ssize_t>(p_conn->len_recv) == recv_cnt)
        {
            // 包体收完整了
            WaitRequestHandlerProcLast(p_conn);
        }
        else
        {
            // 包体没收完整，继续收
            p_conn->p_recvbuf_pos += recv_cnt;            // 注意收后续包的内存往后走
            p_conn->len_recv      -= recv_cnt;            // 要收的内容当然要减少，以确保只收到完整的包头先
        }
    } 
    
    return;
}

//

//返回值：返回-1，则是有问题发生并且在这里把问题处理完毕了，调用本函数的调用者一般是可以直接return
//        返回>0，则是表示实际收到的字节数
/**
 * 功能：
    接收数据专用函数--引入这个函数是为了方便，如果断线，错误之类的，
    这里直接 释放连接池中连接，然后直接关闭socket，以免在其他函数中
    还要重复的干这些事
    
 * 输入参数：(gps_connection_t p_conn,  char* p_buff, ssize_t len_buf) 
    p_conn 连接池中相关连接
    p_buff 接收数据的缓冲区
    len_buf  要接收的数据大小

 * 返回值：
    返回-1，则是有问题发生并且在这里把问题处理完毕了，调用本函数的调用者一般是可以直接return
    返回>0，则是表示实际收到的字节数

 * 调用了函数：
    系统函数：recv()

 * 其他说明：
    ssize_t是有符号整型，在32位机器上等同与int，在64位机器上等同与long int，size_t就是无符号型的ssize_t

 * 例子说明：

 */
ssize_t CSocket::RecvProc(gps_connection_t p_conn,  char* p_buff, ssize_t len_buf) 
{
    ssize_t recv_cnt = 0;
    recv_cnt = recv(p_conn->fd, p_buff, len_buf, 0);   // recv()系统函数， 最后一个参数flag，一般为0； 
    
    if(recv_cnt == 0)
    {
        // 客户端关闭【应该是正常完成了4次挥手】，我这边就直接回收连接连接，关闭socket即可 
        // LogStderr(0,"连接被客户端正常关闭[4路挥手关闭]！");
        CloseConnection(p_conn);
        return -1;
    }
    // 客户端没断，走这里 
    if(recv_cnt < 0) // 这被认为有错误发生
    {
        // EAGAIN和EWOULDBLOCK[【这个应该常用在hp上】应该是一样的值，表示没收到数据，
        // 一般来讲，在ET模式下会出现这个错误，因为ET模式下是不停的recv肯定有
        // 一个时刻收到这个errno，但LT模式下一般是来事件才收，所以不该出现这个返回值
        if(errno == EAGAIN || errno == EWOULDBLOCK)
        {
            // 我认为LT模式不该出现这个errno，而且这个其实也不是错误，所以不当做错误处理
            // epoll为LT模式不应该出现这个返回值，所以直接打印出来瞧瞧
            LogStderr(errno,"CSocekt::recvproc()中errno == EAGAIN || errno == EWOULDBLOCK成立，出乎我意料！");
            return -1;     // 不当做错误处理，只是简单返回
        }
        // EINTR错误的产生：当阻塞于某个慢系统调用的一个进程捕获某个信号
        // 且相应信号处理函数返回时，该系统调用可能返回一个EINTR错误。
        // 例如：在socket服务器端，设置了信号捕获机制，有子进程，
        // 当在父进程阻塞于慢系统调用时由父进程捕获到了一个有效信号时，
        // 内核会致使accept返回一个EINTR错误(被中断的系统调用)。
        if(errno == EINTR)   // 这个不算错误，是我参考官方nginx，官方nginx这个就不算错误；
        {
            // 我认为LT模式不该出现这个errno，而且这个其实也不是错误，所以不当做错误处理
            // epoll为LT模式不应该出现这个返回值，所以直接打印出来瞧瞧
            LogStderr(errno,"CSocekt::recvproc()中errno == EINTR成立，出乎我意料！");
            return -1;       // 不当做错误处理，只是简单返回
        }

        // 所有从这里走下来的错误，都认为异常：意味着我们要关闭客户端套接字要回收连接池中连接；

        // errno参考：http://dhfapiran1.360drm.com        
        if(errno == ECONNRESET)  // #define ECONNRESET 104 /* Connection reset by peer */
        {
            // 如果客户端没有正常关闭socket连接，却关闭了整个运行程序【真是够粗暴无理的，
            // 应该是直接给服务器发送rst包而不是4次挥手包完成连接断开】，那么会产生这个错误            
            // 10054(WSAECONNRESET)--远程程序正在连接的时候关闭会产生这个错误--远程主机强迫关闭了
            // 一个现有的连接算常规错误吧【普通信息型】，日志都不用打印，没啥意思，太普通的错误
            // do nothing

            // ....一些大家遇到的很普通的错误信息，也可以往这里增加各种，代码要慢慢完善，
            // 一步到位，不可能，很多服务器程序经过很多年的完善才比较圆满；
        }
        else
        {
            // 能走到这里的，都表示错误，我打印一下日志，希望知道一下是啥错误，我准备打印到屏幕上
            // 正式运营时可以考虑这些日志打印去掉
            LogStderr(errno,"CSocekt::recvproc()中发生错误，我打印出来看看是啥错误！"); 
        } 
        
        //LogStderr(0,"连接被客户端 非 正常关闭！");

        //这种真正的错误就要，直接关闭套接字，释放连接池中连接了
        CloseConnection(p_conn);
        
        return -1;
    }

    // 能走到这里的，就认为收到了有效数据
    
    return recv_cnt;  //  返回收到的字节数
}


//
/**
 * 功能：
    包头收完整后的处理，我们称为包处理阶段1：写成函数，方便复用
    
 * 输入参数：(gps_connection_t p_conn)
 	p_conn 指针，指向连接池中的一个连接

 * 返回值：
	无

 * 调用了函数：

 * 其他说明：

 * 例子说明：

 */
void CSocket::WaitRequestHandlerProcPart1(gps_connection_t p_conn)
{
    CMemory *p_memory = CMemory::GetInstance();		

    gps_comm_pkg_header_t p_pkg_header;
    // 正好收到包头时，包头信息肯定是在arr_pkghead_info里；
    p_pkg_header = (gps_comm_pkg_header_t)p_conn->arr_pkghead_info; 

    unsigned short len_pkg; 
    len_pkg = ntohs(p_pkg_header->len_pkg);   // 注意这里网络序转本机序，所有传输到网络上的2字节数据，
                                              // 都要用htons()转成网络序，所有从网络上收到的2字节数据，
                                              // 都要用ntohs()转成本机序
                                              // ntohs/htons的目的就是保证不同操作系统数据之间收发的正确性，
                                              // 【不管客户端/服务器是什么操作系统，发送的数字是多少，收到的就是多少】
                                              // 直接百度搜索"网络字节序" "主机字节序" "p_conn++ 大端" "p_conn++ 小端"

    LogErrorCore(NGX_LOG_INFO, 0, "包头收完整了，CSocket::WaitRequestHandlerProcPart1()中包头结构体中表示[包头+包体]的长度len_pkg = %ud!", len_pkg);

    // 恶意包或者错误包的判断
    if(len_pkg < m_len_pkg_header) 
    {
        // 伪造包/或者包错误，否则整个包长怎么可能比包头还小？
        // （整个包长是包头+包体，就算包体为0字节，那么至少len_pkg == m_len_pkg_header）
        // 报文总长度 < 包头长度，认定非法用户，废包
        // 状态和接收位置都复原，这些值都有必要，因为有可能在其他状态比如PKG_HEAD_RECV_ING状态调用这个函数；
        p_conn->pkg_cur_state = PKG_HEAD_INIT;      
        p_conn->p_recvbuf_pos = p_conn->arr_pkghead_info;
        p_conn->len_recv = m_len_pkg_header;
    }
    else if(len_pkg > (PKG_MAX_LEN - END_SPACE_LEN))   // 客户端发来包居然说包长度 > 29000?肯定是恶意包
    {
        // 恶意包，太大，认定非法用户，废包【包头中说这个包总长度这么大，这不行】
        // 状态和接收位置都复原，这些值都有必要，因为有可能在其他状态比如PKG_HEAD_RECV_ING状态调用这个函数；
        p_conn->pkg_cur_state = PKG_HEAD_INIT;      
        p_conn->p_recvbuf_pos = p_conn->arr_pkghead_info;
        p_conn->len_recv = m_len_pkg_header;
    }
    else
    {
        // 合法的包头，继续处理
        // 我现在要分配内存开始收包体，因为包体长度并不是固定的，所以内存肯定要new出来；
        // 分配内存【长度是 消息头长度  + 包头长度 + 包体长度】，最后参数先给false，表示内存不需要memset;
        char *p_tmpbuff  = (char *)p_memory->AllocMemory(m_len_msg_header + len_pkg, false); 
        p_conn->is_new_recvmem   = true;        // 标记我们new了内存，将来在FreeConnection()要回收的
        p_conn->p_new_recvmem_pos = p_tmpbuff;  // 内存开始指针

        // a)先填写消息头内容
        gps_msg_header_t p_tmp_msgheader = (gps_msg_header_t)p_tmpbuff;
        p_tmp_msgheader->p_conn = p_conn;
        // 收到包时的连接池中连接序号记录到消息头里来，以备将来用；
        p_tmp_msgheader->cnt_currse_quence = p_conn->cnt_currse_quence; 
        
        // b)再填写包头内容
        p_tmpbuff += m_len_msg_header;                     // 往后跳，跳过消息头，指向包头
        memcpy(p_tmpbuff, p_pkg_header, m_len_pkg_header); // 直接把收到的包头拷贝进来
        if(len_pkg == m_len_pkg_header)
        {
            // 该报文只有包头无包体【我们允许一个包只有包头，没有包体】
            // 这相当于收完整了，则直接入消息队列待后续业务逻辑线程去处理吧
            WaitRequestHandlerProcLast(p_conn);
        } 
        else
        {
            // 开始收包体，注意我的写法
            p_conn->pkg_cur_state = PKG_BODY_INIT;                // 当前状态发生改变，包头刚好收完，准备接收包体	    
            p_conn->p_recvbuf_pos = p_tmpbuff + m_len_pkg_header; // p_tmpbuff指向包头，这里 + m_iLenPkgHeader后指向包体 weizhi
            p_conn->len_recv = len_pkg - m_len_pkg_header;        // len_pkg是整个包【包头+包体】大小，-m_iLenPkgHeader【包头】  = 包体
        }                       
    } 
}

/**
 * 功能：
    收到一个完整包后的处理【Last表示最后阶段】，放到一个函数中，方便调用

 * 输入参数：(gps_connection_t p_conn)
 	p_conn 指针，指向连接池中的一个连接

 * 返回值：
	无

 * 调用了函数：
    AddMsgRecvQueue()
 * 其他说明：

 * 例子说明：

 */
void CSocket::WaitRequestHandlerProcLast(gps_connection_t p_conn)
{
    //int msgqueue_n = 0;     // 消息队列当前信息数量
    // 把这段内存放到消息队列中来；
    //AddMsgRecvQueue(p_conn->p_new_recvmem_pos, msgqueue_n);
    // ......这里可能考虑触发业务逻辑，怎么触发业务逻辑，这个代码以后再考虑扩充。。。。。。


    // 上面拿到消息数，下面就知道有多少消息需要处理，
    // 从而好直接告诉线程需要激活多个个线程来处理消息对应的业务逻辑了
    
    // 通过全局线程变量直接调用线程来处理了
    //g_threadpool.Call(msgqueue_n);                           // 激发线程池中的某个线程来处理业务逻辑     

    // 加个信息日志，方便调试
    LogErrorCore(NGX_LOG_INFO, 0, "包体收完整了，CSocket::WaitRequestHandlerProcLast()中包体结构体中表示[包头+包体]的长度len_pkg = %ud!",\
                                  ntohs( ((gps_comm_pkg_header_t)p_conn->arr_pkghead_info)->len_pkg ));     
    
    g_threadpool.AddMsgRecvQueueAndSignal(p_conn->p_new_recvmem_pos);  // 入消息队列并触发线程处理消息
    
    p_conn->is_new_recvmem     = false;                      // 内存不再需要释放，因为你收完整了包，这个包被上边
	                                                         // 调用InMsgRecvQueue()移入消息队列，那么释放内存就
	                                                         // 属于业务逻辑去干，不需要回收连接到连接池中干了
    p_conn->p_new_recvmem_pos  = NULL;
    p_conn->pkg_cur_state      = PKG_HEAD_INIT;              // 收包状态机的状态恢复为原始态，为收下一个包做准备                    
    p_conn->p_recvbuf_pos      = p_conn->arr_pkghead_info;   // 设置好收包的位置
    p_conn->len_recv           = m_len_pkg_header;           // 设置好要接收数据的大小
    return;
}

// 这些消息队列数据的处理全部移到线程相关类中去了，这里就注释掉
#if 0
/**
 * 功能：
    当收到一个完整包之后，将完整包入消息队列，这个包在服务器端应该是 消息头+包头+包体 格式

 * 输入参数：(char* p_buf, int& ret_msgqueue_n) 
 	p_buf 指针，指向一段内存=消息头 + 包头 + 包体
    ret_msgqueue_n 引用，用来返回消息队列中消息的数量给调用者
    
 * 返回值：
	无

 * 调用了函数：

 * 其他说明：

 * 例子说明：

 */
void CSocket::AddMsgRecvQueue(char* p_buf, int& ret_msgqueue_n) 
{
    // 利用CLock这个类的封装加解锁很方便，这里加锁，
    // 函数退出时会自动调用析构函数解锁，就不需要手工去解锁了
    CLock lock(&m_recv_msg_queue_mutex);    
    
    m_list_rece_msg_queue.push_back(p_buf); // 入消息队列
    ++m_recv_msg_queue_n;                   // 收消息队列数字+1，用变量更方便一点，比 m_list_rece_msg_queue.size()高效
    ret_msgqueue_n = m_recv_msg_queue_n;    // 接收消息队列当前消息数量保存到ret_msgqueue_n，给调用者

    // ....其他功能待扩充，这里要记住一点，这里的内存都是要释放的，否则。。。。。。。。。。日后增加释放这些内存的代码
    // ...而且逻辑处理应该要引入多线程，所以这里要考虑临界问题
    // ....

    // 临时在这里调用一下该函数，以防止接收消息队列过大
    //TmpOutMsgRecvQueue();   // .....临时，后续会取消这行代码

    // 为了测试方便，因为本函数意味着收到了一个完整的数据包，所以这里打印一个信息
    LogStderr(0,"非常好，收到了一个完整的数据包【包头+包体】！");  
}

/**
 * 功能：
    从消息队列中把一个包提取出来以备后续处理
    
 * 输入参数：
 	无

 * 返回值：
	消息队列中一个消息，实质是一段内存首地址

 * 调用了函数：

 * 其他说明：

 * 例子说明：

 */
char* CSocket::OutMsgRecvQueue()
{
    CLock lock(&m_recv_msg_queue_mutex);

    if(m_list_rece_msg_queue.empty())
    {
        return NULL;  //  也许会存在这种情形： 消息本该有，但被干掉了，这里可能为NULL的？        
    }
    
    char* p_retbuf = m_list_rece_msg_queue.front();  // 返回第一个元素但不检查元素存在与否,上面已经检测
    m_list_rece_msg_queue.pop_front();               // 移除第一个元素但不返回	
    --m_recv_msg_queue_n;                            // 消息队列数量-1
    
    return p_retbuf;
}
#endif

     
/**
 * 功能：
    消息处理线程主函数，专门处理各种接收到的TCP消息
    
 * 输入参数：(char *p_msgbuf)
 	pMsgBuf 发送过来的消息缓冲区，消息本身是自解释的，通过包头可以计算整个包长

 * 返回值：
	消息队列中一个消息，实质是一段内存首地址

 * 调用了函数：

 * 其他说明：
    消息本身格式【消息头+包头+包体】 

 * 例子说明：

 */
void CSocket::ThreadRecvProcFunc(char *p_msgbuf)
{
	// 后面实现。。。

    return;
}

// 这个函数不用了
#if 0
/**
 * 功能：
    临时函数，用于将Msg中消息干掉
 * 输入参数：
 	无

 * 返回值：
	无

 * 调用了函数：

 * 其他说明：

 * 例子说明：

 */
void CSocket::TmpOutMsgRecvQueue()
{
    // 日后可能引入OutMsgRecvQueue()，这个函数可能需要临界......
    if(m_list_rece_msg_queue.empty())  // 没有消息直接退出
    {
        return;
    }
    
    int size = m_list_rece_msg_queue.size();
    if(size < 1000) // 消息不超过1000条就不处理先
    {
        return; 
    }
    
    // 消息达到1000条
    CMemory *p_memory = CMemory::GetInstance();		
    int cha = size - 500;
    for(int i = 0; i < cha; ++i)
    {
        // 一次干掉一堆
        char *p_tmp = m_list_rece_msg_queue.front();    // 返回第一个元素但不检查元素存在与否
        m_list_rece_msg_queue.pop_front();              // 移除第一个元素但不返回	
        p_memory->FreeMemory(p_tmp);                    // 先释放掉把；
    }  
    
    return;
}
#endif

