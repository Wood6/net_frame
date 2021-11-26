// 一些公开的函数声明头文件

#ifndef __NGX_FUNC_H__
#define __NGX_FUNC_H__

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


// 字符串相关函数
void LeftTrim(char * s);
void RightTrim(char * s);

// 和日志，打印输出有关
void LogInit();
void LogStderr(int err, const char* fmt, ...);
u_char* LogErrno(u_char* p_buf, u_char* p_last, int err);
void LogErrorCore(int level, int err, const char* fmt, ...);

// 日志字符串格式化处理相关
u_char* SlPrintf(u_char* p_buf, u_char* p_last, const char* fmt, ...);
u_char * SnPrintf(u_char *p_buf, size_t max, const char *fmt, ...);
u_char* VslPrintf(u_char* p_buf, u_char* p_last, const char* fmt, va_list args);

// 信号相关代码
bool InitSignals();


// 设备进程标题相关
void InitSetProcTitle();
void SetProcTitle(const char *title);

// 创建出守护进程
int CreatDaemon();

// 和主流程相关
void MasterProcessCycle();
void ProcessEventsAndTimers();

#endif
