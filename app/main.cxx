// 主文件

#include "ngx_global.h"
#include "ngx_c_conf.h"
#include "ngx_macro.h"
#include "ngx_func.h"
#include "ngx_c_memory.h"
#include "ngx_c_threadpool.h"
#include "ngx_c_crc32.h"              // 和crc32校验算法有关 
#include "ngx_c_slogic_socket.h"      // 和socket通讯相关


using namespace std;

// global variable
size_t g_argv_need_mem  = 0;
size_t g_env_need_mem   = 0;
int    g_os_argc        = 0;
char** g_os_argv        = NULL;
char*  gp_envmem        = NULL;    // 指向自己分配的env环境变量的内存，在InitSetProcTitle()函数中会被分配内存

int    g_is_daemon      = 0;       // 是否开启守护进程模式，0未启用，1启用

// 和进程本身有关的全局变量
pid_t  g_pid;                      // 当前进程的pid
pid_t  g_ppid;                     // 父进程pid
int    g_process_type;             // 进程类型，用来标识是master进程还是worker进程
bool   g_is_stop_programe;         // 标志程序退出,false不退出,true退出

// 标记子进程状态变化[一般是子进程发来SIGCHLD信号表示退出],
// sig_atomic_t:系统定义的类型：访问或改变这些变量需要在计算机的一条指令内完成
// 一般等价于int【通常情况下，int类型的变量通常是原子访问的，也可以认为 sig_atomic_t就是int类型的数据】
sig_atomic_t g_flag_workproc_change;

// socket相关
//CSocket g_socket;                // socket全局对象
CLogicSocket g_socket;             // socket全局对象

// 线程池相关
CThreadPool g_threadpool;          // 线程池全局对象

// 

// 专门在程序执行末尾释放资源的函数【一系列的main返回前的释放动作函数】
static void FreeResource();

// 程序入口函数 -----------------------------------------------------------
int main(int argc, char **argv)
{
	int exit_code = 0;
    //CMemory* p_memory;
    // (0)先初始化的变量
    g_is_stop_programe = false;            // 标记程序是否退出，0不退出   

	// 第一部分：无伤大雅也不需要释放的放最上面
	g_pid = getpid();

	g_argv_need_mem = 0;
	for (int i = 0; i < argc; ++i)
	{
		g_argv_need_mem += strlen(argv[i]) + 1;
	}
	g_env_need_mem = 0;
	for (int i = 0; environ[i]; ++i)
	{
		g_env_need_mem += strlen(environ[i]) + 1;
	}
	g_os_argc = argc;
	g_os_argv = argv;

	// 全局变量有必要初始化的，在这第一部分后面给其先初始化以供后用
	gs_log.fd = -1;                             // -1：表示日志文件尚未打开；因为后边LogStderr要用所以这里先给-1
	g_process_type = NGX_PROCESS_IS_MASTER;     // 先标记本进程是master进程
	g_flag_workproc_change = 0;                 // 标记子进程没有发生变化

	// 第二部分：初始化失败就要直接退出的
	CConfig * p_config = CConfig::GetInstance();
	if (false == p_config->Load(CONFIG_FILE_PATH))
	{
		LogStderr(0, "配置文件[%s]载入失败，退出!", "nginx.conf");
		exit_code = 2;   // 标记找不到文件退出
		goto lblexit;
	}

    // (2.1)内存单例类可以在这里初始化，返回值不用保存
    //CMemory::GetInstance();
	// (2.2)crc32校验算法单例类可以在这里初始化，返回值不用保存
	CCRC32::GetInstance();

	// 第三部分：一些必须事先准备好的资源，先初始化
	LogInit();

	// 第四部分：一些初始化函数，准备放这里
	if (InitSignals() == false)          // 信号注册
	{
		exit_code = 1;
		goto lblexit;
	}
	 
	if (g_socket.InitSocket() == false)  // 初始化socket
	{
		exit_code = 1;
		goto lblexit;
	}

	// 第五部分：一些不好归类的其他类别代码，准备放这里
	InitSetProcTitle();                   // 把环境变量搬家

	// 第六部分：创建守护进程
	if (p_config->GetIntDefault("Daemon", 0) == 1)
	{
		int ret_creat_monitor_proc = CreatDaemon();
		if (-1 == ret_creat_monitor_proc)
		{
			exit_code = -1;
			goto lblexit;
		}
		if (1 == ret_creat_monitor_proc)
		{
			// 父进程在这里就退出历史舞台了。。。
			FreeResource();
			return exit_code = 0;
		}

		g_is_daemon = 1;       // 守护进程标记，标记是否启用了守护进程模式，0：未启用，1：启用了
	}
	
	// 第7部分开始正式的主工作流程，主流程一致在下边这个函数里循环，暂时不会走下来，资源释放啥的日后再慢慢完善和考虑 
	MasterProcessCycle();

lblexit:
	LogStderr(0, "程序退出，再见了！");
	FreeResource();
	return exit_code;
}

static void FreeResource()
{
	// 关闭日志文件
	if (gs_log.fd != STDERR_FILENO && gs_log.fd != -1)
	{
		close(gs_log.fd);   // 不用判断结果了
		gs_log.fd = -1;     // 标记下，防止被再次close吧
	}
}

