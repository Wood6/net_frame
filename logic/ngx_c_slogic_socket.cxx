
//和网络以及逻辑处理 有关的函数放这里

#include <arpa/inet.h>        // ntohs()
#include <pthread.h>

#include "ngx_c_memory.h"
#include "ngx_c_crc32.h"
#include "ngx_c_slogic_socket.h"  
#include "ngx_logiccomm.h" 
#include "ngx_func.h"
#include "ngx_macro.h"

// 定义成员函数指针
typedef bool (CLogicSocket::*handler)(gps_connection_t p_conn,       // 连接池中连接的指针
	gps_msg_header_t p_msg_header,                                   // 消息头指针
	char* p_pkg_body,                                                // 包体指针
	unsigned short len_body);                                        // 包体长度

// 用来保存 成员函数指针 的这么个数组
static const handler status_handler[] =
{
	// 数组前5个元素，保留，以备将来增加一些基本服务器功能
	NULL,                                                   // 【0】：下标从0开始
	NULL,                                                   // 【1】：下标从0开始
	NULL,                                                   // 【2】：下标从0开始
	NULL,                                                   // 【3】：下标从0开始
	NULL,                                                   // 【4】：下标从0开始

	// 开始处理具体的业务逻辑
	&CLogicSocket::_HandleRegister,                         // 【5】：实现具体的注册功能
	&CLogicSocket::_HandleLogIn,                            // 【6】：实现具体的登录功能
	// ......其他待扩展，比如实现攻击功能，实现加血功能等等；
};

// 整个命令有多少个，编译时即可知道
const int AUTH_TOTAL_COMMANDS              = sizeof(status_handler) / sizeof(handler); 

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
CLogicSocket::CLogicSocket()
{

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

 * 例子说明：

 */
CLogicSocket::~CLogicSocket()
{

}

/**
 * 功能：
	初始化函数,【fork()子进程之前干这个事】

 * 输入参数：
	无

 * 返回值：
	成功返回true，失败返回false

 * 调用了函数：

 * 其他说明：

 * 例子说明：

 */
bool CLogicSocket::InitSocket()
{
	// 做一些和本类相关的初始化工作
	// ....日后根据需要扩展
	return CSocket::InitSocket();  // 调用父类的同名函数
}

/**
 * 功能：
	处理收到的数据包

 * 输入参数：(char *p_msg_buf)
	p_msg_buf 消息头 + 包头 + 包体 ：自解释；

 * 返回值：
	无

 * 调用了函数：

 * 其他说明：

 * 例子说明：

 */
void CLogicSocket::ThreadRecvProcFunc(char *p_msg_buf)
{
    // 加个信息日志，方便调试
    LogErrorCore(NGX_LOG_INFO, 0, "线程[%ud]被激活正在处理从消息队列中取出最上面一个消息，CLogicSocket::ThreadRecvProcFunc()中消息队列中最上面一个消息表示[包头+包体]的长度len_pkg = %ud!",\
                                  pthread_self(), ntohs(((gps_comm_pkg_header_t)(p_msg_buf+sizeof(gs_msg_header_t)))->len_pkg ) );

	gps_msg_header_t p_msg_header = (gps_msg_header_t)p_msg_buf;                                 // 消息头
	gps_comm_pkg_header_t  p_pkg_header = (gps_comm_pkg_header_t)(p_msg_buf + m_len_msg_header); // 包头
	void  *p_pkg_body = NULL;                                                                    // 指向包体的指针
	unsigned short len_pkg = ntohs(p_pkg_header->len_pkg);                                       // 客户端指明的包宽度【包头+包体】

    // 加个信息日志，方便调试
    LogErrorCore(NGX_LOG_INFO, 0, "线程[%ud]即将开始处理程序提取出来的数据，CLogicSocket::ThreadRecvProcFunc()中提取出来的[包头+包体]的长度len_pkg = %ud!",\
                                  pthread_self(), len_pkg );

	if (m_len_pkg_header == len_pkg)
	{
		// 没有包体，只有包头
		if (p_pkg_header->crc32 != 0)        // 只有包头的crc值给0
		{
			return;                          // crc错,直接丢弃
		}
		p_pkg_body = NULL;
	}
	else
	{
		// 有包体，走到这里
		p_pkg_header->crc32 = ntohl(p_pkg_header->crc32);		                // 针对4字节的数据，网络序转主机序
		p_pkg_body = (void *)(p_msg_buf + m_len_msg_header + m_len_pkg_header); // 跳过消息头 以及 包头 ，指向包体

		// 计算crc值判断包的完整性        
		int calc_crc = CCRC32::GetInstance()->GetCRC((unsigned char *)p_pkg_body, len_pkg - m_len_pkg_header); // 计算纯包体的crc值
		if (calc_crc != p_pkg_header->crc32) // 服务器端根据包体计算crc值，和客户端传递过来的包头中的crc32信息比较
		{
			LogStderr(0, "CLogicSocket::threadRecvProcFunc()中CRC错误，丢弃数据!");    // 正式代码中可以干掉这个信息
			return;                                                                    // crc错,直接丢弃
		}
	}

	// 包crc校验OK才能走到这里    	
	unsigned short msg_type = ntohs(p_pkg_header->msg_type); // 消息类型拿出来
	gps_connection_t p_conn = p_msg_header->p_conn;          // 消息头中藏着连接池中连接的指针

	// 我们要做一些判断
	// (1)如果从收到客户端发送来的包，到服务器释放一个线程池中的线程处理该包的过程中，客户端断开了，那显然，这种收到的包我们就不必处理了；
	// 该连接池中连接以被其他tcp连接【其他socket】占用，这说明原来的 客户端和本服务器的连接断了，这种包直接丢弃不理
	if (p_conn->cnt_currse_quence != p_msg_header->cnt_currse_quence)   
	{
		return;                                              // 丢弃不理这种包了【客户端断开了】
	}

	// (2)判断消息码是正确的，防止客户端恶意侵害我们服务器，发送一个不在我们服务器处理范围内的消息码
	if (msg_type >= AUTH_TOTAL_COMMANDS)                     // 无符号数不可能<0
	{
		LogStderr(0, "CLogicSocket::threadRecvProcFunc()中msg_type=%d消息码不对!", msg_type); //这种有恶意倾向或者错误倾向的包，希望打印出来看看是谁干的
		return;                                              // 丢弃不理这种包【恶意包或者错误包】
	}

	// 能走到这里的，包没过期，不恶意，那好继续判断是否有相应的处理函数
	// (3)有对应的消息处理函数吗
	if (status_handler[msg_type] == NULL)                     // 这种用msg_type的方式可以使查找要执行的成员函数效率特别高
	{
        // 这种有恶意倾向或者错误倾向的包，希望打印出来看看是谁干的
		LogStderr(0, "CLogicSocket::threadRecvProcFunc()中msg_type=%d消息码找不到对应的处理函数!", msg_type); 
		return;                                               // 没有相关的处理函数
	}

	// 一切正确，可以放心大胆的处理了
	// (4)调用消息类型码对应的成员函数来处理
	(this->*status_handler[msg_type])(p_conn, p_msg_header, (char *)p_pkg_body, 
	                                  static_cast<unsigned short>(len_pkg - m_len_pkg_header));
    
	return;
}

// ----------------------------------------------------------------------------------------------------------
// 处理各种业务逻辑
/**
 * 功能：
	业务逻辑处理函数

 * 输入参数：(gps_connection_t p_conn, gps_msg_header_t p_msg_header, char* p_pkg_body, unsigned short len_body)
	p_conn         连接池中的连接
	p_msg_header   消息头
	p_pkg_body     包体
	len_body       包体长度

 * 返回值：
	无

 * 调用了函数：

 * 其他说明：

 * 例子说明：

 */
bool CLogicSocket::_HandleRegister(gps_connection_t p_conn, gps_msg_header_t p_msg_header, char* p_pkg_body, unsigned short len_body)
{
	LogStderr(0, "线程[%ud]执行了CLogicSocket::_HandleRegister()!", pthread_self());
    
	return true;
}

/**
 * 功能：
	业务逻辑处理函数

 * 输入参数：(gps_connection_t p_conn, gps_msg_header_t p_msg_header, char* p_pkg_body, unsigned short len_body)
	p_conn         连接池中的连接
	p_msg_header   消息头
	p_pkg_body     包体
	len_body       包体长度


 * 返回值：
	无

 * 调用了函数：

 * 其他说明：

 * 例子说明：

 */
bool CLogicSocket::_HandleLogIn(gps_connection_t p_conn, gps_msg_header_t p_msg_header, char* p_pkg_body, unsigned short len_body)
{
	LogStderr(0, "线程[%ud]执行了CLogicSocket::_HandleLogIn()!", pthread_self());
    
	return true;
}


