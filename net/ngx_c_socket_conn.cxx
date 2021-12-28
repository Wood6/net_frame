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

	fd = -1;                                 // 开始先给-1
	pkg_cur_state = PKG_HEAD_INIT;           // 收包状态处于 初始状态，准备接收数据包头【状态机】
	p_recvbuf_pos = arr_pkghead_info;        // 收包我要先收到这里来，因为我要先收包头，所以收数据的buff直接就是arr_pkghead_info
	len_recv = sizeof(gs_pkg_header_t);      // 这里指定收数据的长度，这里先要求收包头这么长字节的数据

	p_recvbuf_array_mem_addr  = NULL;        // 既然没new内存，那自然指向的内存地址先给NULL
	atomi_sendbuf_full_flag_n = false;       // 原子的
	p_sendbuf_mem = NULL;                    // 发送数据头指针记录
	epoll_events_type    = 0;                // epoll事件先给0
	last_ping_time = time(NULL);             // 上次ping的时间

    flood_attacked_last_time = 0;                   // Flood攻击上次收到包的时间
	flood_attacked_n  = 0;	                 // Flood攻击在该时间内收到包的次数统计
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
	if (p_recvbuf_array_mem_addr != NULL)  // 我们曾经给这个连接分配过接收数据的内存，则要释放内存
	{
		CMemory::GetInstance()->FreeMemory(p_recvbuf_array_mem_addr);
		p_recvbuf_array_mem_addr = NULL;
	}  

    if(p_sendbuf_mem != NULL)                  // 如果发送数据的缓冲区里有内容，则要释放内存
    {
        CMemory::GetInstance()->FreeMemory(p_sendbuf_mem);
        p_sendbuf_mem = NULL;
    }

    atomi_sendbuf_full_flag_n = 0;            // 设置不设置感觉都行  
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
    for(int i = 0; i < m_worker_connections_n; ++i)     // 先创建这么多个连接，后续在使用get连接发现不够程序会动态自己申请堆内存扩容增加
    {                                                   // 在main()中初始化CSocket::InitSocket()函数时就从读取到配置中的这个m_worker_connections_n值
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

        LogErrorCoreAddPrintAddr(NGX_LOG_INFO, 0, "连接池中有空闲连接，从这些空闲连接中取到一个连接[%Xp]", p_conn);
        
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

    LogErrorCoreAddPrintAddr(NGX_LOG_INFO, 0, "连接池中没有空闲连接，成功动态了创建一个连接[%Xp]", p_conn);
    
    return p_conn;
}

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

/**
 * 功能：
	将要回收的连接放到一个（延迟回收）队列(list)中来

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
    LogErrorCoreAddPrintAddr(NGX_LOG_INFO, 0, "连接加到延迟回收队列(list)中，连接[%Xp]在[%ud]秒后将被回收", p_conn, m_recy_connection_wait_time);
    
	std::list<gps_connection_t>::iterator pos;
	bool is_existed = false;

    //LogStderr(0,"CSocket::inRecyConnectQueue()执行，连接入到回收队列中.");  
    // 针对连接回收列表的互斥量，因为线程ServerRecyConnectionThread()也有要用到这个回收列表；
    CLock lock(&m_mutex_recyList_connection); 
    
	// 如下判断防止连接被多次扔到回收站中来
	for (pos = m_list_recy_connection.begin(); pos != m_list_recy_connection.end(); ++pos)
	{
		if ((*pos) == p_conn)
		{
			is_existed = true;
			break;
		}
	}
	if (is_existed == true) // 找到了，不必再入了
	{
		// 我有义务保证这个只入一次嘛
		return;
	}

    p_conn->add_recyList_time = time(NULL);     // 记录回收时间
    ++p_conn->currse_quence_n;
    
    m_list_recy_connection.push_back(p_conn);   // 等待ServerRecyConnectionThread线程自会处理 
    ++m_totol_recy_connection_n;                // 待释放连接队列大小+1
    --m_online_user_count;                      // 连入用户数量-1
    
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
    _thread_item* p_thread = static_cast<_thread_item*>(p_thread_data);
    CSocket* p_socket_obj = p_thread->_pThis;
    
    time_t currtime;
	int err = -1;
    std::list<gps_connection_t>::iterator pos, posend;
    gps_connection_t p_conn;
    
    while(1)
    {
        // 这个线程创建后这里一直有执行到，打印日志太多，注释掉
        // LogErrorCoreAddPrintAddr(NGX_LOG_DEBUG, 0, "当前待回收连接数m_totol_recy_connection_n[ %d ]",int(p_socket_obj->m_totol_recy_connection_n) );
        
        // 为简化问题，我们直接每次休息200毫秒  
        usleep(200 * 1000);  // 单位是微妙,又因为1毫秒=1000微妙，所以 200 *1000 = 200毫秒

        // 不管啥情况，先把这个条件成立时该做的动作做了
        if(p_socket_obj->m_totol_recy_connection_n > 0)
        {
            currtime = time(NULL);
            err = pthread_mutex_lock(&p_socket_obj->m_mutex_recyList_connection);
            if(err != 0) 
				LogStderr(err,"CSocket::ServerRecyConnectionThread()中pthread_mutex_lock()失败，返回的错误码为%d!",err);
			
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
                // 我认为，凡是到释放时间的，atomi_sendbuf_full_flag_n 都应该为0；这里我们加点日志判断下
                //if(p_conn->atomi_sendbuf_full_flag_n != 0)
				if(p_conn->atomi_sendbuf_full_flag_n > 0)
                {
                    // 这确实不应该，打印个日志吧；
                    LogErrorCoreAddPrintAddr(NGX_LOG_WARN, 0, "连接都到释放时间却发现p_conn.atomi_sendbuf_full_flag_n!=0，这个不该发生");
                    // 其他先暂时啥也不干，路程继续往下走，继续去释放吧。
                }

                LogErrorCoreAddPrintAddr(NGX_LOG_INFO, 0, "已经延时[%ud]秒了，此时待回收连接数有[%ud]个",\
                                         p_socket_obj->m_recy_connection_wait_time, int(p_socket_obj->m_totol_recy_connection_n) );
                LogErrorCoreAddPrintAddr(NGX_LOG_INFO, 0, "现在对连接[%Xp]开始走回收流程...", p_conn);

                // 流程走到这里，表示可以释放，那我们就开始释放
                --p_socket_obj->m_totol_recy_connection_n;          // 待释放连接队列大小-1
                p_socket_obj->m_list_recy_connection.erase(pos);    // 迭代器已经失效，但pos所指内容在p_conn里保存着呢

                // LogStderr(0,"CSocket::ServerRecyConnectionThread()执行，连接%d被归还.",p_conn->fd);

                p_socket_obj->FreeConnectionToCPool(p_conn);	    // 归还参数p_conn所代表的连接到到连接池中

                LogErrorCoreAddPrintAddr(NGX_LOG_INFO, 0, "连接[%Xp]回收完成!", p_conn);
                
                goto lblRRTD; 
            } 
            err = pthread_mutex_unlock(&p_socket_obj->m_mutex_recyList_connection);
            if(err != 0)  LogStderr(err,"CSocket::ServerRecyConnectionThread()pthread_mutex_unlock()失败，返回的错误码为%d!",err);
        } 

        if(true == g_is_stop_programe) // 要退出整个程序，那么肯定要先退出这个循环
        {
            LogErrorCoreAddPrintAddr(NGX_LOG_INFO, 0, "g_is_stop_programe为true了，要求整个程序要退出了，开始回收连接池资源...");
        
            if(p_socket_obj->m_totol_recy_connection_n > 0)
            {
                //  因为要退出，所以就得硬释放了【不管到没到时间，不管有没有其他不 允许释放的需求，都得硬释放】
                err = pthread_mutex_lock(&p_socket_obj->m_mutex_recyList_connection);
                if(err != 0) 
                    LogErrorCoreAddPrintAddr(NGX_LOG_ERR, 0, "pthread_mutex_lock()失败，返回的错误码err = %d", err);

        lblRRTD2:
                pos    = p_socket_obj->m_list_recy_connection.begin();
			    posend = p_socket_obj->m_list_recy_connection.end();
                for(; pos != posend; ++pos)
                {
                    p_conn = (*pos);
                    --p_socket_obj->m_totol_recy_connection_n;         // 待释放连接队列大小-1
                    p_socket_obj->m_list_recy_connection.erase(pos);   // 迭代器已经失效，但pos所指内容在p_conn里保存着呢
                    p_socket_obj->FreeConnectionToCPool(p_conn);	   // 归还参数p_conn所代表的连接到到连接池中

                    LogErrorCoreAddPrintAddr(NGX_LOG_INFO, 0, "连接[%Xp]回收完成!", p_conn);
                    
                    goto lblRRTD2; 
                } 
                
                err = pthread_mutex_unlock(&p_socket_obj->m_mutex_recyList_connection);
                if(err != 0)  
                    LogErrorCoreAddPrintAddr(NGX_LOG_ERR, 0, "pthread_mutex_lock()失败，返回的错误码err = %d", err);
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
	FreeConnectionToCPool(p_conn);    // 把释放代码放在最后边，更合适  

	if (p_conn->fd != -1)
	{
		close(p_conn->fd);
		p_conn->fd = -1;
	}

}

