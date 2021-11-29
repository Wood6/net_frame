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
#include "ngx_c_threadpool.h"
#include "ngx_c_slogic_socket.h"

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



// 外部全局变量声明 ---------------------------------------------
extern pid_t           g_pid;                    // 子进程pid
extern pid_t           g_ppid;                   // 父进程pid
extern int             g_process_type;
extern sig_atomic_t    g_flag_workproc_change;


extern size_t          g_env_need_mem;
extern size_t          g_argv_need_mem;
extern char*           gp_envmem;
extern int             g_os_argc;
extern char**          g_os_argv;


extern gs_log_t        gs_log;


//extern CSocket       g_socket;
extern CLogicSocket    g_socket;
extern CThreadPool     g_threadpool;


#endif 

