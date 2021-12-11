
//和网络以及逻辑处理 有关的函数放这里

#include <arpa/inet.h>        // ntohs()
#include <pthread.h>

#include "ngx_c_memory.h"
#include "ngx_c_crc32.h"
#include "ngx_c_slogic_socket.h"  
#include "ngx_logiccomm.h" 
#include "ngx_func.h"
#include "ngx_macro.h"
#include "ngx_c_lockmutex.h"

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
    LogErrorCoreAddPrintAddr(NGX_LOG_INFO, 0, "");
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
    LogErrorCoreAddPrintAddr(NGX_LOG_INFO, 0, "参数: p_msg_buf = %p", p_msg_buf);

	gps_msg_header_t p_msg_header = (gps_msg_header_t)p_msg_buf;                       // 消息头
	gps_pkg_header_t  p_pkg_header = (gps_pkg_header_t)(p_msg_buf + m_len_msg_header); // 包头
	void  *p_pkg_body = NULL;                                                          // 指向包体的指针
	unsigned short len_pkg = ntohs(p_pkg_header->len_pkg);                             // 客户端指明的包宽度【包头+包体】

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
		int calc_crc = CCRC32::GetInstance()->GetCRC((unsigned char *)p_pkg_body, len_pkg - m_len_pkg_header);  // 计算纯包体的crc值
		if (calc_crc != p_pkg_header->crc32) // 服务器端根据包体计算crc值，和客户端传递过来的包头中的crc32信息比较
		{
			LogErrorCoreAddPrintAddr(NGX_LOG_ALERT, 0, "CRC错误，丢弃数据");            // 正式代码中可以干掉这个信息
			return;                                                              // crc错,直接丢弃
		}
	}

	// 包crc校验OK才能走到这里    	
	unsigned short msg_type = ntohs(p_pkg_header->msg_type); // 消息类型拿出来
	gps_connection_t p_conn = p_msg_header->p_conn;          // 消息头中藏着连接池中连接的指针

	// 我们要做一些判断
	// (1)如果从收到客户端发送来的包，到服务器释放一个连接池中的线程处理该包的过程中，客户端断开了，那显然，这种收到的包我们就不必处理了；
	// 该连接池中连接以被其他tcp连接【其他socket】占用，这说明原来的 客户端和本服务器的连接断了，这种包直接丢弃不理
	if (p_conn->currse_quence_n != p_msg_header->currse_quence_n)   
	{
		return;                                              // 丢弃不理这种包了【客户端断开了】
	}

	// (2)判断消息码是正确的，防止客户端恶意侵害我们服务器，发送一个不在我们服务器处理范围内的消息码
	if (msg_type >= AUTH_TOTAL_COMMANDS)                     // 无符号数不可能<0
	{
		LogErrorCoreAddPrintAddr(NGX_LOG_ALERT, 0, "msg_type = %d, 消息码不对!", msg_type); //这种有恶意倾向或者错误倾向的包，希望打印出来看看是谁干的
		return;                                              // 丢弃不理这种包【恶意包或者错误包】
	}

	// 能走到这里的，包没过期，不恶意，那好继续判断是否有相应的处理函数
	// (3)有对应的消息处理函数吗
	if (status_handler[msg_type] == NULL)                     // 这种用msg_type的方式可以使查找要执行的成员函数效率特别高
	{
        // 这种有恶意倾向或者错误倾向的包，希望打印出来看看是谁干的
        LogErrorCoreAddPrintAddr(NGX_LOG_ALERT, 0, "msg_type = %d, 消息码找不到对应的处理函数，有可能是被篡改了", msg_type);
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
    LogErrorCoreAddPrintAddr(NGX_LOG_INFO, 0, "参数: p_conn = %p, p_msg_header = %p, p_pkg_body = %p, len_body = %ud",\
                                              p_conn, p_msg_header, p_pkg_body, len_body);

	// (1)首先判断包体的合法性
	if (NULL == p_pkg_body)     // 具体看客户端服务器约定，如果约定这个命令[msg_type]必须带包体，那么如果不带包体，就认为是恶意包直接不处理    
	{
        LogErrorCoreAddPrintAddr(NGX_LOG_INFO, 0, "收到的数据包没有包体，视为恶意包丢弃，程序对此包不作处理！");
		return false;
	}

	int len_recved = sizeof(gs_register_t);
	if (len_recved != len_body) // 发送过来的结构大小不对，认为是恶意包，直接不处理
	{
        LogErrorCoreAddPrintAddr(NGX_LOG_INFO, 0, "收到的数据包长度与约定不符，视为恶意包丢弃，程序对此包不作处理！");
		return false;
	}

	// (2)对于同一个用户，可能同时发送来多个请求过来，造成多个线程同时为该 用户服务，
	//  比如以网游为例，用户要在商店中买A物品，又买B物品，而用户的钱 只够买A或者B，不够同时买A和B呢？
	//  那如果用户发送购买命令过来买了一次A，又买了一次B，如果是两个线程来执行同一个用户的这两次不同的购买命令，
	//  很可能造成这个用户购买成功了 A，又购买成功了 B
	//  所以，为了稳妥起见，针对某个用户的命令，我们一般都要互斥,我们需要增加临界的变量于ngx_connection_s结构中
	CLock lock(&p_conn->mutex_logic_porc); // 凡是和本用户有关的访问都互斥

	// (3)取得了整个发送过来的数据
	//gps_register_t p_RecvInfo = (gps_register_t)p_pkg_body;

	// (4)这里可能要考虑 根据业务逻辑，进一步判断收到的数据的合法性，
	//  当前该玩家的状态是否适合收到这个数据等等【比如如果用户没登陆，它就不适合购买物品等等】
	//  这里大家自己发挥，自己根据业务需要来扩充代码，这里就不扩充了。。。。。。。。。。。。
	//  。。。。。。。。

	// (5)给客户端返回数据时，一般也是返回一个结构，这个结构内容具体由客户端/服务器协商，
	// 这里我们就以给客户端也返回同样的 gs_register_t 结构来举例    
	// gps_register_t pFromPkgHeader =  (gps_register_t)(((char *)pMsgHeader)+m_len_msg_header);
	
	gps_pkg_header_t p_pkg_header = NULL;                           // 指向收到的包的包头，其中数据后续可能要用到
	CMemory  *p_memory = CMemory::GetInstance();
	CCRC32   *p_crc32 = CCRC32::GetInstance();
	int len_sending = sizeof(gs_register_t);
    
	// a)分配要发送出去的包的内存
	//len_sending = 65000;                                            // unsigned short最大65535也就差不多是这个值
	len_sending = 25000;

	// 准备发送的格式，这里是 消息头+包头+包体
	char *p_sendbuf = (char *)p_memory->AllocMemory(m_len_msg_header + m_len_pkg_header + len_sending, false);

    //  b)填充消息头
	memcpy(p_sendbuf, p_msg_header, m_len_msg_header);                // 消息头直接拷贝到这里来

    // c)填充包头
	p_pkg_header = (gps_pkg_header_t)(p_sendbuf + m_len_msg_header);  // 指向包头
	p_pkg_header->msg_type = _CMD_REGISTER;	                          // 消息代码，可以统一在ngx_logiccomm.h中定义
	p_pkg_header->msg_type = htons(p_pkg_header->msg_type);	          // htons主机序转网络序 
	// 这里每次收到客户端"register"请求的业务包后，这里就给填充个65008字节(约65M)的数据包，准备会发给客户端
	// 也就是将协定的数据包中包头的len_pkg信息设置为65008，
	// 这里是做发包测试目的，所以等会测试时测试工具客服端没有对来自服务的包长做检测，会收下这个65008的包作显示
	p_pkg_header->len_pkg = htons(static_cast<unsigned short>(m_len_pkg_header + len_sending));    // 整个包的尺寸【包头+包体尺寸】 

    // d)填充包体
	gps_register_t p_send_info = (gps_register_t)(p_sendbuf + m_len_msg_header + m_len_pkg_header);	// 跳过消息头，跳过包头，就是包体了
	//。。。。。这里根据需要，填充要发回给客户端的内容,int类型要使用htonl()转，short类型要使用htons()转；

	// e)包体内容全部确定好后，计算包体的crc32值
	p_pkg_header->crc32 = p_crc32->GetCRC((unsigned char *)p_send_info, len_sending);
	p_pkg_header->crc32 = htonl(p_pkg_header->crc32);

	// f)发送数据包
	SendMsg(p_sendbuf);
    
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


