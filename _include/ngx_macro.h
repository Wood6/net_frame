// 各种 宏定义/const全局常量 相关的放此文件


#ifndef __NGX_MACRO_H__
#define __NGX_MACRO_H__


#define NGX_MAX_ERROR_STR   2048   //显示的错误信息最大数组长度

// 简单功能函数--------------------
// 类似memcpy，但常规memcpy返回的是指向目标dst的指针
// 而这个ngx_cpymem返回的是目标【拷贝数据后】的终点后一个位置，连续复制多段数据时方便
#define ngx_cpymem(dst, src, n)     ( ((u_char*)memcpy(dst, src, n)) + n )

#define MAX_UINT32_VALUE   (uint32_t) 0xffffffff              //最大的32位无符号数：十进制是‭4294967295‬
#define INT64_LEN          (sizeof("-9223372036854775808") - 1)  

#define MAX_ERROR_STR   2048   //显示的错误信息最大数组长度


//日志相关--------------------
//我们把日志一共分成八个等级【级别从高到低，数字最小的级别最高，数字大的级别最低】，以方便管理、显示、过滤等等
#define NGX_LOG_STDERR            0    //控制台错误【stderr】：最高级别日志，日志的内容不再写入log参数指定的文件，而是会直接将日志输出到标准错误设备比如控制台屏幕
#define NGX_LOG_EMERG             1    //紧急 【emerg】
#define NGX_LOG_ALERT             2    //警戒 【alert】
#define NGX_LOG_CRIT              3    //严重 【crit】
#define NGX_LOG_ERR               4    //错误 【error】：属于常用级别
#define NGX_LOG_WARN              5    //警告 【warn】：属于常用级别
#define NGX_LOG_NOTICE            6    //注意 【notice】
#define NGX_LOG_INFO              7    //信息 【info】
#define NGX_LOG_DEBUG             8    //调试 【debug】：最低级别


// 缺省日志文件路径
#define NGX_ERROR_LOG_PATH       "error.log"   //定义日志存放的路径和文件名 


//进程相关----------------------
//标记当前进程类型
#define NGX_PROCESS_IS_MASTER     0  // master 进程，管理进程
#define NGX_PROCESS_IS_WORKER     1  // worker 进程，工作进程


#endif

