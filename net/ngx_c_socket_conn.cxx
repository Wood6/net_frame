#include "ngx_c_socket.h"
#include "ngx_func.h"
#include "ngx_c_memory.h"
#include "ngx_macro.h"
#include "ngx_c_lockmutex.h"
#include "ngx_global.h"

#include <pthread.h>


// 连接池结构体 _gs_connection 成员函数 -----------------------------------
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
_gs_connection::_gs_connection()
{
	currse_quence_n = 0;
	pthread_mutex_init(&mutex_logic_porc, NULL);  // 互斥量初始化
}

/**
 * 功能：
	析构函数

 * 输入参数：
	无

 * 返回值：
	无

 * 调用了函数：

 * 其他说明：
	与构造对应，构造初始化，析构就释放

 * 例子说明：

 */
_gs_connection::~_gs_connection()
{
	pthread_mutex_destroy(&mutex_logic_porc);     // 互斥量释放
}


/**
 * 功能：
	分配出去一个连接的时候初始化一些内容,原来内容放在 GetElementOfConnection()里，现在放在这里

 * 输入参数：
	无

 * 返回值：
	无

 * 调用了函数：

 * 其他说明：
	与构造对应，构造初始化，析构就释放

 * 例子说明：

 */
void _gs_connection::GetOneToUse()
{
	++currse_quence_n;

	pkg_cur_state = PKG_HEAD_INIT;           // 收包状态处于 初始状态，准备接收数据包头【状态机】
	p_recvbuf_pos = arr_pkghead_info;        // 收包我要先收到这里来，因为我要先收包头，所以收数据的buff直接就是arr_pkghead_info
	len_recv = sizeof(gs_pkg_header_t);      // 这里指定收数据的长度，这里先要求收包头这么长字节的数据

	p_new_recvmem_pos  = NULL;               // 既然没new内存，那自然指向的内存地址先给NULL
	is_full_sendbuf_atomic = false;          // 原子的
	p_sendbuf_mem = NULL;                    // 发送数据头指针记录
	epoll_events_type    = 0;                // epoll事件先给0 
}


/**
 * 功能：
	回收回来一个连接的时候做一些事

 * 输入参数：
	无

 * 返回值：
	无

 * 调用了函数：

 * 其他说明：
	与构造对应，构造初始化，析构就释放

 * 例子说明：

 */
void _gs_connection::PutOneToFree()
{
	++currse_quence_n;
	if (p_new_recvmem_pos != NULL)  // 我们曾经给这个连接分配过接收数据的内存，则要释放内存
	{
		CMemory::GetInstance()->FreeMemory(p_new_recvmem_pos);
		p_new_recvmem_pos = NULL;
	}  

    if(p_sendbuf_mem != NULL)       // 如果发送数据的缓冲区里有内容，则要释放内存
    {
        CMemory::GetInstance()->FreeMemory(p_sendbuf_mem);
        p_sendbuf_mem = NULL;
    }

    is_full_sendbuf_atomic = false;         // 设置不设置感觉都行  
}


// 类CSocke 成员函数 -----------------------------------
/**
 * 功能：
    初始化连接池

 * 输入参数：
 	无

 * 返回值：
	无

 * 调用了函数：

 * 其他说明：

 * 例子说明：

 */
void CSocket::InitConnectionPool()                                     
{
    gps_connection_t p_conn;
    CMemory *p_memory = CMemory::GetInstance();   

    int len_conn = sizeof(gs_connection_t);    
    for(int i = 0; i < m_worker_connections_n; ++i)     // 先创建这么多个连接，后续在使用get连接发现不够程序会动态申请增加
    {
	// 清理内存 , 因为这里分配内存new char，无法执行构造函数，所以如下：
        p_conn = (gps_connection_t)p_memory->AllocMemory(len_conn, true); 
        // 手工调用构造函数，因为AllocMemory里无法调用构造函数
	    // 定位new【不懂请百度】，释放则显式调用p_conn->~_gs_connection();
        p_conn = new(p_conn) _gs_connection(); 
        
        p_conn->GetOneToUse();
        
        m_list_connection.push_back(p_conn);            // 所有链接【不管是否空闲】都放在这个list
        m_list_free_connection.push_back(p_conn);       // 空闲连接会放在这个list
    } 
    
    m_free_connection_n = m_list_connection.size();     // 开始这两个列表一样大
	m_total_connection_n = m_list_connection.size();
}

// 类CSocke 成员函数 -----------------------------------
/**
 * 功能：
	回收连接池

 * 输入参数：
	无

 * 返回值：
	无

 * 调用了函数：

 * 其他说明：

 * 例子说明：

 */
void CSocket::ClearConnectionPool()                                    
{
    gps_connection_t p_conn;
	CMemory *p_memory = CMemory::GetInstance();
	
	while(!m_list_connection.empty())
	{
		p_conn = m_list_connection.front();
		m_list_connection.pop_front(); 
        p_conn->~_gs_connection();           // 定位new对应的必须要手工调用析构函数释放资源
		p_memory->FreeMemory(p_conn);
	}
}

/**
 * 功能：
	从连接池中获取一个空闲连接，将 fdsock 这个套接字保存进这个空闲连接中去
	【当一个客户端连接TCP进入，我希望把这个连接和我的 连接池中的 一个连接【对象】绑到一起，
	后续 我可以通过这个连接，把这个对象拿到，因为对象里边可以记录各种信息】

 * 输入参数：(int fdsock)  
	fdsock 要保存到连接池空闲元素中的套接字

 * 返回值：

 * 调用了函数：

 * 其他说明：
    取代原来GetElementOfConnection()

 * 例子说明：

 */
gps_connection_t CSocket::GetConnectionFromCPool(int fdsock)          
{
    LogErrorCoreAddPrintAddr(NGX_LOG_INFO, 0, "套接字[socket=%ud]来连接池取一个连接了...", fdsock);

    // 因为可能有其他线程要访问m_list_free_connection，m_list_connection
	// 【比如可能有专门的释放线程要释放/或者主线程要释放】之类的，所以应该临界一下
    CLock lock(&m_mutex_connection);  

    if(!m_list_free_connection.empty())
    {
        //有空闲的，自然是从空闲的中摘取
        gps_connection_t p_conn = m_list_free_connection.front();   // 返回第一个元素但不检查元素存在与否
        m_list_free_connection.pop_front();                         // 移除第一个元素但不返回	
        p_conn->GetOneToUse();
        --m_free_connection_n; 
        p_conn->fd = fdsock;

        LogErrorCoreAddPrintAddr(NGX_LOG_INFO, 0, "连接池中有空闲连接，从这些空闲连接中取到一个连接[%ud]", p_conn->fd);
        
        return p_conn;
    }

    // 走到这里，表示没空闲的连接了，那就考虑重新创建一个连接
    CMemory *p_memory = CMemory::GetInstance();
    gps_connection_t p_conn = (gps_connection_t)p_memory->AllocMemory(sizeof(_gs_connection),true);
    p_conn = new(p_conn) _gs_connection();
    p_conn->GetOneToUse();

    m_list_connection.push_back(p_conn);  // 入到总表中来，但不能入到空闲表中来，因为凡是调这个函数的，肯定是要用这个连接的

    ++m_total_connection_n;             
    p_conn->fd = fdsock;

    LogErrorCoreAddPrintAddr(NGX_LOG_INFO, 0, "连接池中没有空闲连接，成功动态了创建一个连接[%ud]", p_conn->fd);
    
    return p_conn;
}

#if 0
/**
 * 功能：
	从连接池中获取一个空闲连接，将 isock 这个套接字保存进这个空闲连接中去
	【当一个客户端连接TCP进入，我希望把这个连接和我的 连接池中的 一个连接【对象】绑到一起，
	后续 我可以通过这个连接，把这个对象拿到，因为对象里边可以记录各种信息】

 * 输入参数：(int isock)
	isock 要保存到连接池空闲元素中的套接字

 * 返回值：

 * 调用了函数：

 * 其他说明：

 * 例子说明：

 */
gps_connection_t CSocket::GetElementOfConnection(int isock)
{
	gps_connection_t ret_p_c = mp_free_connections;
	if (NULL == ret_p_c)
	{
		// 系统应该控制连接数量，防止空闲连接被耗尽，能走到这里，都不正常
		LogStderr(0, "CSocekt::ngx_get_connection()中空闲链表为空,这不应该!");
		return NULL;
	}

	mp_free_connections = ret_p_c->data;  // 指向连接池中下一个未用的节点
	--m_free_connections_n;               // 空闲连接少1

	// (1)注意这里的操作,先把ret_p_c指向的对象中有用的东西搞出来保存成变量，因为这些数据可能有用
	uintptr_t instance = ret_p_c->instance;               // 常规c->instance在刚构造连接池时这里是1【失效】
	uint64_t currse_quence = ret_p_c->currse_quence_n;  //  序号也暂存，后续用于恢复
	//....其他内容再增加

	// (2)把以往有用的数据搞出来后，清空并给适当值
	memset(ret_p_c, 0, sizeof(gs_connection_t));  // 注意，类型不要用成gps_connection_t，否则就出错了
	ret_p_c->fd = isock;                          // 套接字要保存起来，这东西具有唯一性 

    // 初始化收包相关
    ret_p_c->pkg_cur_state = PKG_HEAD_INIT;               // 收包状态处于 初始状态，准备接收数据包头【状态机】
    ret_p_c->p_recvbuf_pos = ret_p_c->arr_pkghead_info;   // 收包我要先收到这里来，因为我要先收包头，
                                                          // 所以收数据的buff直接就是dataHeadInfo
    ret_p_c->len_recv = sizeof(gs_pkg_header_t);     // 这里指定收数据的长度，这里先要求收包头这么长字节的数据
    ret_p_c->is_new_recvmem = false;                      // 标记我们并没有new内存，所以不用释放	 
    ret_p_c->p_new_recvmem_pos = NULL;                    // 既然没new内存，那自然指向的内存地址先给NULL

	// (3)这个值有用，所以在上边(1)中被保留，没有被清空，这里又把这个值赋回来
	ret_p_c->instance = !instance;   // 抄自官方nginx，到底有啥用，以后再说
	                                 // 【分配内存时候，连接池里每个连接对象这个变量给的值都为1，所以这里取反应该是0【有效】；】
	ret_p_c->currse_quence_n = currse_quence;   
	++ret_p_c->currse_quence_n;    // 每次取用该值都增加1，这个值用处暂时不明白？？？？

	return ret_p_c;
}
#endif

/**
 * 功能：
	归还参数p_conn所代表的连接到到连接池中，注意参数类型是指针

 * 输入参数：(gps_connection_t p_conn)   
	p_conn 指针，指向要归还的连接池元素内存地址

 * 返回值：
	无

 * 调用了函数：

 * 其他说明：

 * 例子说明：

 */
void CSocket::FreeConnectionToCPool(gps_connection_t p_conn)          
{   
    // 因为有线程可能要动连接池中连接，所以合理互斥也是必要的
    CLock lock(&m_mutex_connection);  
    
    //  首先明确一点，连接，所有连接全部都在m_list_connection里；
    p_conn->PutOneToFree();

    //  扔到空闲连接列表里
    m_list_free_connection.push_back(p_conn);

    // 空闲连接数+1
    ++m_free_connection_n;

    LogErrorCoreAddPrintAddr(NGX_LOG_INFO, 0, "归还连接[%ud]到连接池中完成，此时连接池中空闲连接有[m_list_free_connection.size()=%ud]",\
                                              p_conn->fd, m_list_free_connection.size());
}


#if 0
/**
 * 功能：
	归还参数p_c所代表的连接到到连接池中，注意参数类型是指针

 * 输入参数：(gps_connection_t p_c)
	p_c 指针，指向要归还的连接池元素内存地址

 * 返回值：
	无

 * 调用了函数：

 * 其他说明：

 * 例子说明：

 */
void CSocket::FreeConnection(gps_connection_t p_conn)
{
	if(p_conn->is_new_recvmem == true)
    {
        // 我们曾经给这个连接分配过内存，则要释放内存        
        CMemory::GetInstance()->FreeMemory(p_conn->p_new_recvmem_pos);
        p_conn->p_new_recvmem_pos = NULL;
        p_conn->is_new_recvmem = false;  // 这行有用
    }
    
	p_conn->data = mp_free_connections;  // 回收的节点指向原来串起来的空闲链的链头

	++p_conn->currse_quence_n;         // 回收后，该值就增加1,以用于判断某些网络事件是否过期
	                                     // 【一被释放就立即+1也是有必要的】
	                                     // 暂时不明白这意思？？？

	mp_free_connections = p_conn;        // 将空闲链头指针指向这个回收的节点
	++m_free_connections_n;              // 已经回收完成，空闲树+1
}
#endif


/**
 * 功能：
	将要回收的连接放到一个队列(list)中来

 * 输入参数：(gps_connection_t p_conn) 
	p_conn 指针，指向要回收的连接内存地址

 * 返回值：
	无

 * 调用了函数：

 * 其他说明：

 * 例子说明：

 */
void CSocket::AddRecyConnectList(gps_connection_t p_conn)              
{
    LogErrorCoreAddPrintAddr(NGX_LOG_INFO, 0, "连接加到延迟回收队列(list)中，连接[%d]在[%ud]秒后将被回收", p_conn->fd, m_recy_connection_wait_time);
    
    //LogStderr(0,"CSocekt::inRecyConnectQueue()执行，连接入到回收队列中.");  
    // 针对连接回收列表的互斥量，因为线程ServerRecyConnectionThread()也有要用到这个回收列表；
    CLock lock(&m_mutex_recyList_connection); 
    
    p_conn->add_recyList_time = time(NULL);     // 记录回收时间
    ++p_conn->currse_quence_n;
    
    m_list_recy_connection.push_back(p_conn);   // 等待ServerRecyConnectionThread线程自会处理 
    ++m_totol_recy_connection_n;                // 待释放连接队列大小+1
}

/**
 * 功能：
	处理连接回收的线程

 * 输入参数：(void* p_thread_data)
	p_thread_data 指针，

 * 返回值：
	无

 * 调用了函数：

 * 其他说明：

 * 例子说明：

 */
void* CSocket::ServerRecyConnectionThread(void* p_thread_data)
{
    LogErrorCoreAddPrintAddr(NGX_LOG_INFO, 0, "进入到连接回收线程[%ud]中...", pthread_self());
    
    _thread_item* p_thread = static_cast<_thread_item*>(p_thread_data);
    CSocket* p_socket_obj = p_thread->_pThis;
    
    time_t currtime;
	int err = -1;
    std::list<gps_connection_t>::iterator pos, posend;
    gps_connection_t p_conn;
    
    while(1)
    {
        //这个线程创建后这里一直有执行到，打印日志太多，注释掉
        //LogErrorCoreAddPrintAddr(NGX_LOG_DEBUG, 0, "当前待回收连接数m_totol_recy_connection_n[ %d ]",int(p_socket_obj->m_totol_recy_connection_n) );
        
        // 为简化问题，我们直接每次休息200毫秒  
        usleep(200 * 1000);  // 单位是微妙,又因为1毫秒=1000微妙，所以 200 *1000 = 200毫秒

        // 不管啥情况，先把这个条件成立时该做的动作做了
        if(p_socket_obj->m_totol_recy_connection_n > 0)
        {
            currtime = time(NULL);
            err = pthread_mutex_lock(&p_socket_obj->m_mutex_recyList_connection);
            if(err != 0) 
				LogStderr(err,"CSocekt::ServerRecyConnectionThread()中pthread_mutex_lock()失败，返回的错误码为%d!",err);
			
lblRRTD:
            pos    = p_socket_obj->m_list_recy_connection.begin();
			posend = p_socket_obj->m_list_recy_connection.end();
            for(; pos != posend; ++pos)
            {
                p_conn = (*pos);
                if(
                    ( (p_conn->add_recyList_time + p_socket_obj->m_recy_connection_wait_time) > currtime)  && (false == g_is_stop_programe) //如果不是要整个系统退出，你可以continue，否则就得要强制释放
                    )
                {
                    continue; //没到释放的时间
                }    
                // 到释放的时间了: 
                // ......这将来可能还要做一些是否能释放的判断[在我们写完发送数据代码之后吧]，先预留位置
                // ....

                LogErrorCoreAddPrintAddr(NGX_LOG_INFO, 0, "已经延时[%ud]秒了，此时待回收连接数有[%ud]个",\
                                         p_socket_obj->m_recy_connection_wait_time, int(p_socket_obj->m_totol_recy_connection_n) );
                LogErrorCoreAddPrintAddr(NGX_LOG_INFO, 0, "现在对连接[%d]开始走回收流程...", p_conn->fd);

                // 流程走到这里，表示可以释放，那我们就开始释放
                --p_socket_obj->m_totol_recy_connection_n;          // 待释放连接队列大小-1
                p_socket_obj->m_list_recy_connection.erase(pos);    // 迭代器已经失效，但pos所指内容在p_conn里保存着呢

                // ngx_log_stderr(0,"CSocekt::ServerRecyConnectionThread()执行，连接%d被归还.",p_conn->fd);

                p_socket_obj->FreeConnectionToCPool(p_conn);	    // 归还参数p_conn所代表的连接到到连接池中

                LogErrorCoreAddPrintAddr(NGX_LOG_INFO, 0, "连接[%ud]回收完成!", p_conn->fd);
                
                goto lblRRTD; 
            } 
            err = pthread_mutex_unlock(&p_socket_obj->m_mutex_recyList_connection);
            if(err != 0)  LogStderr(err,"CSocekt::ServerRecyConnectionThread()pthread_mutex_unlock()失败，返回的错误码为%d!",err);
        } 

        if(true == g_is_stop_programe) // 要退出整个程序，那么肯定要先退出这个循环
        {
            LogErrorCoreAddPrintAddr(NGX_LOG_INFO, 0, "整个程序要退出了，开始进行资源回收...");
        
            if(p_socket_obj->m_totol_recy_connection_n > 0)
            {
                //因为要退出，所以就得硬释放了【不管到没到时间，不管有没有其他不 允许释放的需求，都得硬释放】
                err = pthread_mutex_lock(&p_socket_obj->m_mutex_recyList_connection);
                if(err != 0) 
					LogStderr(err,"CSocekt::ServerRecyConnectionThread()中pthread_mutex_lock2()失败，返回的错误码为%d!",err);

        lblRRTD2:
                pos    = p_socket_obj->m_list_recy_connection.begin();
			    posend = p_socket_obj->m_list_recy_connection.end();
                for(; pos != posend; ++pos)
                {
                    p_conn = (*pos);
                    --p_socket_obj->m_totol_recy_connection_n;        //待释放连接队列大小-1
                    p_socket_obj->m_list_recy_connection.erase(pos);   //迭代器已经失效，但pos所指内容在p_conn里保存着呢
                    p_socket_obj->FreeConnectionToCPool(p_conn);	   //归还参数pConn所代表的连接到到连接池中

                    LogErrorCoreAddPrintAddr(NGX_LOG_INFO, 0, "连接[%ud]回收完成!", p_conn->fd);
                    
                    goto lblRRTD2; 
                } 
                
                err = pthread_mutex_unlock(&p_socket_obj->m_mutex_recyList_connection);
                if(err != 0)  
                    LogStderr(err,"CSocekt::ServerRecyConnectionThread()pthread_mutex_unlock2()失败，返回的错误码为%d!",err);
            } 
            
            break; // 整个程序要退出了，所以break;
        } 
    } 
    
    return (void*)0;
} 



/**
 * 功能：
	用户连入，我们accept4()时，得到的socket在处理中产生失败，则资源用这个函数释放
	【因为这里涉及到好几个要释放的资源，所以写成函数】
	
	把 CloseAcceptedConnection() 函数改名为让名字CloseConnection()更通用，
	并从文件ngx_socket_accept.cxx迁移到本文件中，并改造其中代码，注意顺序

 * 输入参数：(gps_connection_t p_conn)
	p_conn 指针，指向连接池中的一个连接

 * 返回值：
	无

 * 调用了函数：
	主要调用自定义函数：FreeConnection(p_c);

 * 其他说明：

 * 例子说明：

 */
void CSocket::CloseConnection(gps_connection_t p_conn)
{
#if 0
    p_conn->fd = -1;           // 官方nginx这么写，这么写有意义；
                               // 不要这个东西，回收时不要轻易东连接里边的内容
#endif
	FreeConnectionToCPool(p_conn);    // 把释放代码放在最后边，更合适  
	if (close(p_conn->fd) == -1)
	{
		LogErrorCore(NGX_LOG_ALERT, errno, "CSocekt::CloseConnection()中close(%d)失败!", p_conn->fd);
	}

}

