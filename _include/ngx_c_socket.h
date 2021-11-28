// 和网络相关
#ifndef __NGX_SOCKET_H__
#define __NGX_SOCKET_H__

#include <vector>
#include <list>

#include <sys/epoll.h>
#include <sys/socket.h>

#include "ngx_comm.h"

// 已完成连接队列，nginx给511，我们也先按照这个来：不懂这个数字的5.4
const int NGX_LISTEN_BACKLOG = 511;
// epoll_wait一次最多接收这么多个事件，nginx中缺省是512，我们这里固定给成512就行，没太大必要修改
const int NGX_MAX_EVENTS = 512;

typedef struct gs_listening              gs_listening_t,  *gps_listening_t;
typedef struct gs_connection             gs_connection_t, *gps_connection_t;
typedef struct _s_msg_header             gs_msg_header_t, *gps_msg_header_t;

typedef class CSocket                    CSocket;

typedef void (CSocket::*EpollEventHandlerPt)(gps_connection_t c);                 // 定义CSocket类的成员函数指针

// 一些专用结构定义放在这里，暂时不考虑放ngx_global.h里了
struct gs_listening
{
	int               port;            // 监听的端口号
	int               fd;              // 套接字句柄socket
	gps_connection_t  p_connection;    // 指向连接池中的一个连接，注意这是个指针
	                                   // 这个指针就将这个结构体与连接池结构体联系起来了
};

/* 以下三个结构是非常重要的三个结构，我们遵从官方nginx的写法 */
// (1)该结构表示一个TCP连接【客户端主动发起的、Nginx服务器被动接受的TCP连接】
struct gs_connection
{
	int                       fd;                // 套接字句柄socket
	gps_listening_t           p_listening;       // 如果这个链接被分配给了一个监听套接字，那么这个里边就指向监听套接字对应的
											     // 那个 gps_listening_t 的内存首地址,用此指针与上面监听套接字的结构体联系起来了

	// ------------------------------------	
	unsigned                  instance : 1;      // 【位域】失效标志位：0：有效，1：失效【这个是官方nginx提供，到底有什么用，ngx_epoll_process_events()中详解】  
	uint64_t                  cnt_currse_quence; // 我引入的一个序号，每次分配出去时+1，此法也有可能在一定程度上检测错包废包，具体怎么用，用到了再说
	struct sockaddr           s_sockaddr;        // 保存对方地址信息用的
	//char                    addr_text[100];    // 地址的文本信息，100足够，一般其实如果是ipv4地址，255.255.255.255，其实只需要20字节就够

	// 和读有关的标志-----------------------
	//uint8_t                   r_ready;         // 读准备好标记【暂时没闹明白官方要怎么用，所以先注释掉】
	uint8_t                    write_ready;      // 写准备好标记

	EpollEventHandlerPt        read_handler;     // 读事件的相关处理方法
	EpollEventHandlerPt        write_handler;    // 写事件的相关处理方法

    // 和收包有关
    unsigned char              pkg_cur_state;                          // 当前收包的状态
    char                       arr_pkghead_info[PKG_HEAD_BUFSIZE];     // 用于保存收到的数据的包头信息			
    char*                      p_recvbuf_pos;     // 接收数据的缓冲区的头指针，对收到不全的包非常有用，看具体应用的代码
    unsigned int               len_recv;          // 要收到多少数据，由这个变量指定，和precvbuf配套使用，看具体应用的代码

    bool                       is_new_recvmem;    // 如果我们成功的收到了包头，
                                                  // 那么我们就要分配内存开始保存 包头+消息头+包体内容，
                                                  // 这个标记用来标记是否我们new过内存，因为new过是需要释放的
    char*                      p_new_recvmem_pos; //  new出来的用于收包的内存首地址，和 is_new_recvmem  配对使用



	// --------------------------------------------------
	gps_connection_t            data;             // 这是个指针【等价于传统链表里的next成员：后继指针】，
	                                              // 指向下一个本类型对象，用于把空闲的连接池对象串起来
											      // 构成一个单向链表，方便取用
};

// 消息头，引入的目的是当收到数据包时，额外记录一些代码中需要用到的辅助信息，以备将来使用
typedef struct _s_msg_header
{
    gps_connection_t   p_conn;             //  记录对应的链接，注意这是个指针
    uint64_t          cnt_currse_quence;   //  收到数据包时记录对应连接的序号，将来能用于比较是否连接已经作废用
    // ......其他以后扩展	
}gs_msg_header_t, *gps_msg_header_t;


// socket 相关类
class CSocket
{
private:
	int                           m_lister_port_cnt;     // 默认监听端口数量
	std::vector<gps_listening_t>   m_vec_listen_socket;  // 存储套接字的数据vector结构

	// epoll 相关成员变量
	int                           m_handle_epoll;               // 系统函数 epoll_creat() 返回的句柄
	int                           m_worker_connections_n;       // epoll 连接的最大项数
	struct epoll_event            m_arr_events[NGX_MAX_EVENTS]; // 用于在epoll_wait()中承载返回的所发生的事件
	

	// 连接池相关
	gps_connection_t               mp_connections;       // 注意这是个指针，其实这是个连接池的首地址
	gps_connection_t               mp_free_connections;  // 空闲连接链表头，连接池中总是有某些连接被占用，
	                                                     // 为了快速在池中找到一个空闲的连接，我把空闲的
	                                                     // 连接专门用该成员记录;【串成一串，其实这里
	                                                     // 指向的都是m_pconnections连接池里的没有被使用的成员】
	int                           m_connection_n;        // 当前进程中所有连接对象的总数【连接池大小】
	int                           m_free_connections_n;  // 连接池中可用连接总数

    // 一些和网络通讯有关的成员变量
    size_t                        m_len_pkg_header;      // sizeof(COMM_PKG_HEADER);		
    size_t                        m_len_msg_header;      // sizeof(STRUC_MSG_HEADER);
    std::list<char *>             m_list_rece_msg_queue; // 接受数据消息队列

    // 
    int                           m_recv_msg_queue_n;     // 收消息队列大小
    pthread_mutex_t               m_recv_msg_queue_mutex; // 收消息队列互斥量 


private:
	void ReadConf();                           // 专门用于读各种配置项
	bool OpenListeningSockets();               // 监听必须的端口【支持多个端口】
	void CloseListeningSockets();              // 关闭监听套接字
	bool SetNonblocking(int sockfd);           // 设置非阻塞套接字

	gps_connection_t GetElementOfConnection(int isock);    // 从连接池中获取一个空闲连接
	void FreeConnection(gps_connection_t p_c);             // 将 p_conn 归还进连接池


	void EventAccept(gps_connection_t old_c);              // 建立新连接
	void WaitRequestHandler(gps_connection_t p_c);         // 设置数据来时的读处理函数
	void CloseConnection(gps_connection_t p_conn);         // 用户连入，我们accept4()时，得到的socket
	                                                       // 在处理中产生失败，则资源用这个函数释放
	                                                       // 【因为这里涉及到好几个要释放的资源，所以写成函数】


	// 获取对端相关，
	// 根据参数1给定的信息，获取地址端口字符串，返回这个字符串的长度
	size_t SocketNtop(struct sockaddr *sa, int port, u_char *text, size_t len);  

    void ClearMsgRecvQueue();         // 清理接受收据的消息队列

    ssize_t RecvProc(gps_connection_t p_conn,  char* p_buff, ssize_t len_buf);   // 接收从客户端来的数据专用函数
	void WaitRequestHandlerProcPart1(gps_connection_t p_conn);                   // 包头收完整后的处理                                                                   
	void WaitRequestHandlerProcLast(gps_connection_t p_conn);                    // 收到一个完整包后的处理
	void AddMsgRecvQueue(char* p_buf, int& ret_msgqueue_n);   // 收到一个完整消息后，入消息队列
	//void TmpOutMsgRecvQueue();                            // 临时清除对列中消息函数，测试用，将来会删除该函数

public:
	CSocket();
	virtual ~CSocket();

	virtual bool InitSocket();                       // 初始化函数

	/***** epoll 相关成员函数 ******/
	int InitEpoll();                                 // epoll 功能初始化

	int EpollAddEvent(int fd,
		int read_event, int write_event,
		uint32_t otherflag,
		uint32_t event_type,
		gps_connection_t p_conn
	);                                                // epoll 增加事件

	int EpollProcessEvents(int timer);                // epoll等待接收和处理事件

    // 
    char* OutMsgRecvQueue();                          // 将一个消息出消息队列
    virtual void ThreadRecvProcFunc(char* p_msgbuf);  // 处理客户端请求，这个将来会被设计为子类重写
    
};


#endif



