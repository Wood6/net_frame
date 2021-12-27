// 和网络 中 时间 有关的函数放这里

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>    // uintptr_t
#include <stdarg.h>    // va_start....
#include <unistd.h>    // STDERR_FILENO等
#include <sys/time.h>  // gettimeofday
#include <time.h>      // localtime_r
#include <fcntl.h>     // open
#include <errno.h>     // errno
#include <sys/ioctl.h> // ioctl
#include <arpa/inet.h>

#include "ngx_c_conf.h"
#include "ngx_macro.h"
#include "ngx_global.h"
#include "ngx_func.h"
#include "ngx_c_socket.h"
#include "ngx_c_memory.h"
#include "ngx_c_lockmutex.h"

/**
 * 功能：
	设置踢出时钟(向multimap表中增加内容)，用户三次握手成功连入，
	然后我们开启了踢人开关【 enable_socket_wait_time = 1 】，那么本函数被调用

 * 输入参数：(gps_connection_t p_conn) 
	p_conn 对这个连接加上心跳包到期时间，这个到期时间作为键，
	       这个连接的消息头作为值，插入到时间队列(multimap)里面去

 * 返回值：
	无

 * 调用了函数：
	time_t CSocket::GetEarliestTime()

 * 其他说明：

 * 例子说明：

 */
void CSocket::AddToTimerMultimap(gps_connection_t p_conn)  
{
    LogErrorCoreAddPrintAddr(NGX_LOG_INFO, 0, "参数：p_conn = %p", p_conn);
    
    CMemory* p_memory = CMemory::GetInstance();

    time_t future_time = time(NULL) + m_ping_wait_time;  // 设定的要接收到心跳包的时间点

    CLock lock(&m_mutex_ping_timer);                     // 互斥，因为要操作m_timeQueuemap了
    gps_msg_header_t tmp_msg_header = (gps_msg_header_t)p_memory->AllocMemory(m_len_msg_header, false);
    tmp_msg_header->p_conn = p_conn;
    tmp_msg_header->currse_quence_n = p_conn->currse_quence_n;
    m_multimap_timer.insert(std::make_pair(future_time, tmp_msg_header)); // 按键 自动排序 小->大
    m_multimap_timer_size++;                                              // 计时队列尺寸+1
    m_multimap_timer_front_value = GetEarliestTime();                     // 计时队列头部时间值保存到 m_multimap_timer_front_value 里  
}

/**
 * 功能：
	从multimap中取得最早的时间返回去，调用者负责互斥，
	所以本函数不用互斥，调用者确保 m_multimap_timer 中一定不为空

 * 输入参数：
	无

 * 返回值：m_multimap_timer.begin()->first
    m_multimap_timer.begin()->first  m_multimap_timer队列中最早的那个时间值

 * 调用了函数：

 * 其他说明：

 * 例子说明：

 */
time_t CSocket::GetEarliestTime()
{
    LogErrorCoreAddPrintAddr(NGX_LOG_INFO, 0, "");
    
    std::multimap<time_t, gps_msg_header_t>::iterator pos;	
	pos = m_multimap_timer.begin();	
    
	return pos->first;	
}


/**
 * 功能：
	从m_multimap_timer移除最早的时间，并把最早这个时间所在的项的值所对应的指针 返回
	调用者负责互斥，所以本函数不用互斥

 * 输入参数：
	无

 * 返回值：gps_msg_header_t p_msg_hesader
	p_msg_hesader 时间队列中最早这个时间所在的项的值所对应的指针

 * 调用了函数：

 * 其他说明：

 * 例子说明：

 */
gps_msg_header_t CSocket::RemoveFirstTimer()
{
    LogErrorCoreAddPrintAddr(NGX_LOG_INFO, 0, "");

	if(m_multimap_timer_size <= 0)
	{
		return NULL;
	}

    std::multimap<time_t, gps_msg_header_t>::iterator pos;	
	pos = m_multimap_timer.begin();   // 调用者负责互斥的，这里直接操作没问题的
	gps_msg_header_t p_msg_hesader = pos->second;
	m_multimap_timer.erase(pos);
	--m_multimap_timer_size;
    
	return p_msg_hesader;
}


/**
 * 功能：
	根据给的当前时间，从m_multimap_timer找到比这个时间更老（更早）的节点【1个】返回去，
	这些节点都是时间超过了，要处理的节点
    调用者负责互斥，所以本函数不用互斥

 * 输入参数：(time_t cur_time)
	cur_time 当前时间，调用这个函数时的时间，用来与与时间队列中要接受心跳包最早的那个时间对比用

 * 返回值：gps_msg_header_t p_msg_header / NULL
	返回值为NULL时  ：时间队列是空的，即此时没有任何连接  / 当前时间点还没有任何连接超时
	返回值不为NULL时： 根据给的当前时间，从m_multimap_timer找到比这个时间更老（更早）的节点p_msg_header【1个】返回去

 * 调用了函数：

 * 其他说明：

 * 例子说明：

 */
gps_msg_header_t CSocket::GetOverTimeTimer(time_t cur_time)
{	
    LogErrorCoreAddPrintAddr(NGX_LOG_INFO, 0, "参数: cur_time = %ud", cur_time);

	if (m_multimap_timer_size == 0 || m_multimap_timer.empty())
		return NULL; // 队列为空

	time_t earliesttime = GetEarliestTime(); // 到multimap中去查询
	if (earliesttime <= cur_time)
	{
		// 这回确实是有到时间的了【超时的节点】
		gps_msg_header_t p_msg_header = RemoveFirstTimer();    // 把这个超时的节点从 m_multimap_timer 删掉，并把这个节点的第二项返回来；
		time_t new_future_time = cur_time + (m_ping_wait_time);// 因为下次超时的时间我们也依然要判断，所以还要把这个节点加回来 

		CMemory* p_memory = CMemory::GetInstance();
		gps_msg_header_t p_tmp_msg_header = (gps_msg_header_t)p_memory->AllocMemory(sizeof(gps_msg_header_t), false);
		p_tmp_msg_header->p_conn = p_msg_header->p_conn;
		p_tmp_msg_header->currse_quence_n = p_msg_header->currse_quence_n;			
		m_multimap_timer.insert(std::make_pair(new_future_time, p_tmp_msg_header)); // 自动排序 小->大			
		m_multimap_timer_size++;       

		if(m_multimap_timer_size > 0)    // 这个判断条件必要，因为以后我们可能在这里扩充别的代码
		{
			m_multimap_timer_front_value = GetEarliestTime(); // 计时队列头部时间值保存到m_multimap_timer_front_value里
		}
        
		return p_msg_header;
	}
    
	return NULL;
}

/**
 * 功能：
	把指定用户tcp连接从timer表中抠出去

 * 输入参数：(gps_connection_t p_conn)
	p_conn 要删除的连接

 * 返回值：
	无

 * 调用了函数：

 * 其他说明：

 * 例子说明：

 */
void CSocket::DeleteFromTimerMultimap(gps_connection_t p_conn)
{
    LogErrorCoreAddPrintAddr(NGX_LOG_INFO, 0, "参数: p_conn = %p", p_conn);

    std::multimap<time_t, gps_msg_header_t>::iterator pos, posend;
	CMemory *p_memory = CMemory::GetInstance();

    CLock lock(&m_mutex_ping_timer);

    // 因为实际情况可能比较复杂，将来可能还扩充代码等等，所以如下我们遍历整个队列找一圈，而不是找到一次就拉倒，以免出现什么遗漏
lblMTQM:
	pos    = m_multimap_timer.begin();
	posend = m_multimap_timer.end();
	for(; pos != posend; ++pos)	
	{
        // 直到一次完整的for循环都找不到有p_conn才会不再跳转lblMTQM
		if(pos->second->p_conn == p_conn)
		{			
			p_memory->FreeMemory(pos->second);  // 释放内存
			m_multimap_timer.erase(pos);
			--m_multimap_timer_size;            // 减去一个元素，必然要把尺寸减少1个		
			
			goto lblMTQM;
		}		
	}

    // 删除的连接可能是队列中原来的第一个，所以要更新下  m_multimap_timer_front_value
	if(m_multimap_timer_size > 0)
	{
		m_multimap_timer_front_value = GetEarliestTime();
	} 
}

/**
 * 功能：
	清理时间队列中所有内容

 * 输入参数：
	无

 * 返回值：
	无

 * 调用了函数：

 * 其他说明：

 * 例子说明：

 */
void CSocket::ClearAllFromTimerMultimap()
{
    LogErrorCoreAddPrintAddr(NGX_LOG_INFO, 0, "");
    
	std::multimap<time_t, gps_msg_header_t>::iterator pos, posend;

	CMemory *p_memory = CMemory::GetInstance();	
	pos    = m_multimap_timer.begin();
	posend = m_multimap_timer.end();    
	for(; pos != posend; ++pos)	
	{
		p_memory->FreeMemory(pos->second);		
		--m_multimap_timer_size; 		
	}
    
	m_multimap_timer.clear();
}

/**
 * 功能：
	时间队列监视和处理线程，处理到期不发心跳包的用户踢出的线程

 * 输入参数：(void* threadData)
	threadData

 * 返回值：
	无

 * 调用了函数：

 * 其他说明：

 * 例子说明：

 */
void* CSocket::ServerTimerQueueMonitorThread(void* threadData)
{
    LogErrorCoreAddPrintAddr(NGX_LOG_INFO, 0, "参数: 时间队列监视和处理线程[%ud], 该线程的线程参数threadData = %p", pthread_self(), threadData);
    
	_thread_item *pThread = static_cast<_thread_item*>(threadData);
    CSocket *p_socket_obj = pThread->_pThis;

	time_t absolute_time, cur_time;
	int err = -1;
    while(0 == g_is_stop_programe) // 不退出
    {
        // 这里没互斥判断，所以只是个初级判断，目的至少是队列为空时避免系统损耗		
		if(p_socket_obj->m_multimap_timer_size > 0)  // 队列不为空，有内容
        {
			// 时间队列中最近发生事情的时间放到 absolute_time 里；
            absolute_time = p_socket_obj->m_multimap_timer_front_value; //这个可是省了个互斥，十分划算
            cur_time = time(NULL);
            if(absolute_time < cur_time)
            {
                // 时间到了，可以处理了
                std::list<gps_msg_header_t> m_idle_list; // 保存要处理的内容
                gps_msg_header_t p_msg_header;

                err = pthread_mutex_lock(&p_socket_obj->m_mutex_ping_timer);  
                if(err != 0) 
					LogErrorCoreAddPrintAddr(NGX_LOG_ERR, 0, "pthread_mutex_lock()失败，返回的错误码err = %d", err); //  有问题，要及时报告

                while ((p_msg_header = p_socket_obj->GetOverTimeTimer(cur_time)) != NULL)  // 一次性的把所有超时节点都拿过来
				{
					m_idle_list.push_back(p_msg_header); 
				}

                err = pthread_mutex_unlock(&p_socket_obj->m_mutex_ping_timer); 
                if(err != 0)  
					LogErrorCoreAddPrintAddr(NGX_LOG_ERR, 0, "pthread_mutex_unlock()失败，返回的错误码err = %d", err); // 有问题，要及时报告   

                // 对超时结点进行处理
                while(!m_idle_list.empty())
                {
					gps_msg_header_t p_tmp_msg_header = m_idle_list.front();
					m_idle_list.pop_front(); 
                    p_socket_obj->PingTimeOutChecking(p_tmp_msg_header, cur_time);  // 这里需要检查心跳超时问题
                } 
            }
        } 
        
        usleep(500 * 1000); // 为简化问题，我们直接每次休息500毫秒
    } 

    return (void*)0;
}

/**
 * 功能：
	心跳包检测时间到，该去检测心跳包是否超时的事宜，
	本函数只是把内存释放，子类应该重新事先该函数以实现具体的判断动作

 * 输入参数：(gps_msg_header_t tmpmsg,time_t cur_time)
	tmpmsg
	cur_time

 * 返回值：
	无

 * 调用了函数：

 * 其他说明：

 * 例子说明：

 */
void CSocket::PingTimeOutChecking(gps_msg_header_t p_msg_header, time_t cur_time)
{
    LogErrorCoreAddPrintAddr(NGX_LOG_INFO, 0, "参数: p_msg = %p, cur_time = %ud", p_msg_header, cur_time);

	CMemory *p_memory = CMemory::GetInstance();
	p_memory->FreeMemory(p_msg_header);    
}



