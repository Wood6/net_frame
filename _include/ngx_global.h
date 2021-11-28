// 通用的系统头文件
// 一些比较通用的定义放此头文件，比如typedef定义
// 一些全局变量的外部声明也放这里

#ifndef __NGX_GLOBAL_H__
#define __NGX_GLOBAL_H__

#include <stdint.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h> 
#include <fcntl.h>
#include <errno.h>
#include <sys/stat.h>
#include <signal.h>

#include "ngx_macro.h"
#include "ngx_c_socket.h"
#include "ngx_c_threadpool.h"

// 调试宏开关,
// 1只打开调试中LIYAO_DEBUG_ING的日志输出，
// 0全调试模式，调试中ING和调试通过PASS的全部输出日志
#define LIYAO_DEBUG		1

#if LIYAO_DEBUG
#define LIYAO_DEBUG_ING    1            // 调试中
#else
#define LIYAO_DEBUG_ING    1            // 调试中
#define LIYAO_DEBUG_PASS   1            // 调试通过
#endif


// 配置文件相关结构定义
typedef struct
{
	char c_arr_item_name[51];
	char c_arr_iter_content[501];
}gs_conf_iterm_t, *gps_stru_conf_item_t;


// 和运行日志相关
typedef struct
{
	int log_level;      // 日志级别
	int fd;             // 日志文件描述符
}gs_log_t;

/*

// 特殊数字相关，如类型边界数字等
extern const uint32_t MAX_UINT32_VALUE;      // 最大的32位无符号数：十进制是 ‭4294967295‬

extern const int MAX_ERROR_STR;   //显示的错误信息最大数组长度

//日志相关--------------------
//我们把日志一共分成八个等级【级别从高到低，数字最小的级别最高，数字大的级别最低】，以方便管理、显示、过滤等等
extern const int NGX_LOG_STDERR;    // 控制台错误【stderr】：最高级别日志，日志的内容不再写入log参数指定的文件，而是会直接将日志输出到标准错误设备比如控制台屏幕
extern const int NGX_LOG_EMERG;     // 紧急 【emerg】
extern const int NGX_LOG_ALERT;     // 警戒 【alert】
extern const int NGX_LOG_CRIT;      // 严重 【crit】
extern const int NGX_LOG_ERR;       // 错误 【error】：属于常用级别
extern const int NGX_LOG_WARN;      // 警告 【warn】：属于常用级别
extern const int NGX_LOG_NOTICE;    // 注意 【notice】
extern const int NGX_LOG_INFO;      // 信息 【info】
extern const int NGX_LOG_DEBUG;     // 调试 【debug】：最低级别

// 缺省日志文件路径
extern const char* NGX_ERROR_LOG_PATH;

*/

// 外部全局变量声明
extern pid_t g_pid;
extern pid_t g_ppid;   // 父进程pid
extern int g_process_type;
extern sig_atomic_t g_flag_workproc_change;

extern gs_log_t gs_log;

extern size_t g_env_need_mem;
extern size_t g_argv_need_mem;
extern char* gp_envmem;

extern int g_os_argc;
extern char** g_os_argv;

extern gs_log_t gs_log;

extern CSocket g_socket;
extern CThreadPool g_threadpool;


#endif 

