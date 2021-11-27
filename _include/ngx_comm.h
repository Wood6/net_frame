// 定义网络数据包包头接头体的头文件

#ifndef __NGX_COMM_H__
#define __NGX_COMM_H__


// PKG = package 包单词的缩写

// 也可以用宏定义，但既然是写C++尽量用const取得，增加程序类型安全
// 每个包的最大长度【包头+包体】不超过这个数字，为了留出一些空间，
// 实际上编码是，包头+包体长度必须不大于 这个值-1000【29000】
// 即 PKG_MAX_LEN - END_SPACE_LEN = 29000
const unsigned int PKG_MAX_LEN           = 30000;
const unsigned int END_SPACE_LEN         = 1000;


// 通信 收包的状态定义，做一个收包状态机
const unsigned char PKG_HEAD_INIT          = 0;     // 初始状态，准备接收数据包头
const unsigned char PKG_HEAD_RECV_ING      = 1;     // 接收包头中，包头不完整，继续接收中
const unsigned char PKG_BODY_INIT          = 2;     // 包头刚好收完，准备接收包体
const unsigned char PKG_BODY_RECV_ING      = 3;     // 接收包体中，包体不完整，继续接收中，处理后直接回到 PKG_HEAD_INIT 状态
//const unsigned char _PKG_RECV_FINISHED    = 4;    // 完整包收完，这个状态似乎没什么用处

typedef struct _s_comm_pkg_head gs_comm_pkg_header_t, *gsp_comm_pkg_header_t;

// 一些和网络通讯相关的结构放在这里
// 包头结构,1字节对齐这个包头结构体长度是8字节
#pragma pack(1)       // 对齐方式,1字节对齐【结构之间成员不做任何字节对齐：紧密的排列在一起】
typedef struct _s_comm_pkg_head
{
    unsigned short pkg_len;      // 报文总长度【包头+包体】--2字节，2字节可以表示的最大数字为6万多，
                                 // 我们定义_PKG_MAX_LENGTH 30000，所以用pkgLen足够保存下
	                             // 包头中记录着整个包【包头—+包体】的长度
	                             
    unsigned short msg_type;     //  消息类型代码--2字节，用于区别每个不同的命令【不同的消息】
    
    int            crc32;        // CRC32效验--4字节，为了防止收发数据中出现收到内容
                                 // 和发送内容不一致的情况，引入这个字段做一个基本的校验用	
};
#pragma pack()


// 因为我要先收包头，我希望定义一个固定大小的数组专门用来收包头，
// 这个数字大小一定要 > sizeof(gs_comm_pkg_header_t) ,
// 所以我这里定义为   sizeof(gs_comm_pkg_header_t) + 10 总是比包头多10个字节，收包头绰绰有余，
// 如果日后COMM_PKG_HEADER大小变动，这个数字也会动态调整满足 > sizeof(COMM_PKG_HEADER) 的要求
const unsigned short PKG_HEAD_BUFSIZE = sizeof(gs_comm_pkg_header_t) + 10;

#endif


