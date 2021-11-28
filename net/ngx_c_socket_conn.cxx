#include "ngx_c_socket.h"
#include "ngx_func.h"
#include "ngx_c_memory.h"
#include "ngx_macro.h"

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
	uint64_t currse_quence = ret_p_c->cnt_currse_quence;  //  序号也暂存，后续用于恢复
	//....其他内容再增加

	// (2)把以往有用的数据搞出来后，清空并给适当值
	memset(ret_p_c, 0, sizeof(gs_connection_t));  // 注意，类型不要用成gps_connection_t，否则就出错了
	ret_p_c->fd = isock;                          // 套接字要保存起来，这东西具有唯一性 

    // 初始化收包相关
    ret_p_c->pkg_cur_state = PKG_HEAD_INIT;               // 收包状态处于 初始状态，准备接收数据包头【状态机】
    ret_p_c->p_recvbuf_pos = ret_p_c->arr_pkghead_info;   // 收包我要先收到这里来，因为我要先收包头，
                                                          // 所以收数据的buff直接就是dataHeadInfo
    ret_p_c->len_recv = sizeof(gs_comm_pkg_header_t);     // 这里指定收数据的长度，这里先要求收包头这么长字节的数据
    ret_p_c->is_new_recvmem = false;                      // 标记我们并没有new内存，所以不用释放	 
    ret_p_c->p_new_recvmem_pos = NULL;                    // 既然没new内存，那自然指向的内存地址先给NULL

	// (3)这个值有用，所以在上边(1)中被保留，没有被清空，这里又把这个值赋回来
	ret_p_c->instance = !instance;   // 抄自官方nginx，到底有啥用，以后再说
	                                 // 【分配内存时候，连接池里每个连接对象这个变量给的值都为1，所以这里取反应该是0【有效】；】
	ret_p_c->cnt_currse_quence = currse_quence;   
	++ret_p_c->cnt_currse_quence;    // 每次取用该值都增加1，这个值用处暂时不明白？？？？

	return ret_p_c;
}

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

	++p_conn->cnt_currse_quence;         // 回收后，该值就增加1,以用于判断某些网络事件是否过期
	                                     // 【一被释放就立即+1也是有必要的】
	                                     // 暂时不明白这意思？？？

	mp_free_connections = p_conn;        // 将空闲链头指针指向这个回收的节点
	++m_free_connections_n;              // 已经回收完成，空闲树+1
}

/**
 * 功能：
	用户连入，我们accept4()时，得到的socket在处理中产生失败，
	则资源用这个函数释放【因为这里涉及到好几个要释放的资源，所以写成函数】
	
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
	if (close(p_conn->fd) == -1 )
	{
		LogErrorCore(NGX_LOG_ALERT, errno, "CSocekt::CloseConnection()中close(%d)失败!", p_conn->fd);
	}

    p_conn->fd = -1;           // 官方nginx这么写，这么写有意义；
	FreeConnection(p_conn);    // 把释放代码放在最后边，更合适                       
}

