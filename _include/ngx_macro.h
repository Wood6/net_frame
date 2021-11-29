// 各种 宏定义/const全局常量 相关的放此文件

#ifndef __NGX_MACRO_H__
#define __NGX_MACRO_H__

// 简单功能函数--------------------
// 类似memcpy，但常规memcpy返回的是指向目标dst的指针
// 而这个ngx_cpymem返回的是目标【拷贝数据后】的终点后一个位置，连续复制多段数据时方便
#define ngx_cpymem(dst, src, n)     ( ((u_char*)memcpy(dst, src, n)) + n )

#define MAX_UINT32_VALUE            (uint32_t) 0xffffffff              // 最大的32位无符号数：十进制是‭4294967295‬
#define INT64_LEN                   (sizeof("-9223372036854775808") - 1)  


const unsigned int NGX_MAX_ERROR_STR  = 2048; // 显示的错误信息最大数组长度

// 日志相关--------------------
// 我们把日志一共分成八个等级【级别从高到低，数字最小的级别最高，数字大的级别最低】，以方便管理、显示、过滤等等
const unsigned char NGX_LOG_STDERR    = 0;    // 控制台错误【stderr】：最高级别日志，日志的内容不再写入log参数指定的文件，
                                              // 而是会直接将日志输出到标准错误设备比如控制台屏幕
const unsigned char NGX_LOG_EMERG     = 1;    // 紧急 【emerg】
const unsigned char NGX_LOG_ALERT     = 2;    // 警戒 【alert】
const unsigned char NGX_LOG_CRIT      = 3;    // 严重 【crit】
const unsigned char NGX_LOG_ERR       = 4;    // 错误 【error】：属于常用级别
const unsigned char NGX_LOG_WARN      = 5;    // 警告 【warn】：属于常用级别
const unsigned char NGX_LOG_NOTICE    = 6;    // 注意 【notice】
const unsigned char NGX_LOG_INFO      = 7;    // 信息 【info】
const unsigned char NGX_LOG_DEBUG     = 8;    // 调试 【debug】：最低级别

// 进程相关----------------------
// 标记当前进程类型
const unsigned char NGX_PROCESS_IS_MASTER  = 0;        // master 进程，管理进程
const unsigned char NGX_PROCESS_IS_WORKER  = 1;        // worker 进程，工作进程


// 常量指针常量，是个常量
// const全局常量，定义配置文件名，比用宏定义要好
const char* const CONFIG_FILE_PATH = "nginx.conf";

// 缺省日志文件路径
const char* const NGX_ERROR_LOG_PATH   = "error.log";  // 定义日志存放的路径和文件名 

// 线程池所需创建线程的数量在配置文件中配置项名字
// 这个会报错，常量指针，是个指针值，不是常量(常量是修饰词，主语是指针)
//const char* CONFING_ITEMNAME_CREAT_THREAD_N  = "proc_recvmsg_work_thread_count";     // 编译报错，重复定义
// 这个编译正确，常量指针常量，是个常量（前面常量是修饰词，主语是指针常量，主语是个常量）
const char* const CONFING_ITEMNAME_CREAT_THREAD_N  = "proc_recvmsg_work_thread_count"; // 编译正确
// 关于const定义的常量可以放在头文件中这里解释一下
// 是因为const将这个全局变量的作用域改变为文件作用域了，所以即使这个头文件有被多次包含，
// 在其包含处过去后也是只在那个文件中作用，这样也是不会相互影响，所以编译不会报多次定义的错误
// 这个编译也是可以通过，指针常量，是个常量
//char* const CONFING_ITEMNAME_CREAT_THREAD_N  = "proc_recvmsg_work_thread_count";     // 编译正确

#endif

