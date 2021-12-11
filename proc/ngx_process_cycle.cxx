
#include <signal.h>

#include "ngx_global.h"
#include "ngx_func.h"
#include "ngx_c_conf.h"
#include "ngx_macro.h"


// 静态变量定义worker进程名字
static u_char  arr_master_process_name[] = "master process";

static void StartCreatWorkerProc(int cnt_workprocess);
static int CreatWorkerProc(int inum, const char* p_procname);
static void InitWorkerProcess(int inum);
static void WorkerProcessCycle(int inum, const char* p_procname);

/**
 * 功能：
	创建worker子进程

 * 输入参数：
	无

 * 返回值：
	无

 * 调用了函数：

 * 其他说明：

 * 例子说明：

 */
void MasterProcessCycle()
{
    LogErrorCoreAddPrintAddr(NGX_LOG_INFO, 0, "");
    
	sigset_t set;
	sigemptyset(&set);

	sigaddset(&set, SIGCHLD);
	// 下列这些信号在执行本函数期间不希望收到【考虑到官方nginx中有这些信号，都搬过来了】（保护不希望由信号中断的代码临界区）
	// 建议fork()子进程时学习这种写法，防止信号的干扰；
	sigaddset(&set, SIGCHLD);     // 子进程状态改变
	sigaddset(&set, SIGALRM);     // 定时器超时
	sigaddset(&set, SIGIO);       // 异步I/O
	sigaddset(&set, SIGINT);      // 终端中断符
	sigaddset(&set, SIGHUP);      // 连接断开
	sigaddset(&set, SIGUSR1);     // 用户定义信号
	sigaddset(&set, SIGUSR2);     // 用户定义信号
	sigaddset(&set, SIGWINCH);    // 终端窗口大小改变
	sigaddset(&set, SIGTERM);     // 终止
	sigaddset(&set, SIGQUIT);     // 终端退出符
	//.........可以根据开发的实际需要往其中添加其他要屏蔽的信号......

	// 设置，此时无法接受的信号；
	// 阻塞期间，你发过来的上述信号，多个会被合并为一个，暂存着，等你放开信号屏蔽后才能收到这些信号。。。
	if (sigprocmask(SIG_BLOCK, &set, NULL) == -1)
	{
		LogErrorCoreAddPrintAddr(NGX_LOG_ALERT, errno, "sigprocmask()失败！");
		// 即便sigprocmask失败，程序流程 也继续往下走
	}

	// 给进程另外设置指定名字
	size_t size = sizeof(arr_master_process_name);
	size += g_argv_need_mem;
	if (size < 1000)
	{
		char title[1000] = { 0 };
		strcpy(title, (const char *)arr_master_process_name); // "master process"
		strcat(title, " ");                                   // 跟一个空格分开一些，清晰    //"master process "
		for (int i = 0; i < g_os_argc; i++)                   // "master process ./net_frame"
		{
			strcat(title, g_os_argv[i]);
		}

		SetProcTitle(title);             // 设置标题

		// 设置标题时顺便记录下来进程名，进程id等信息到日志
		LogErrorCoreAddPrintAddr(NGX_LOG_INFO, 0, "%s %P 【master进程】启动并开始运行...", title, g_pid);
	}
	// 设置主进程标题结束

	/******* 要开始创建工作子进程了 **********/
	CConfig *p_config = CConfig::GetInstance();                               // 单例类
	int cnt_workprocess = p_config->GetIntDefault("workprocess_conut", 1);    // 从配置文件中得到要创建的worker进程数量
	StartCreatWorkerProc(cnt_workprocess);                                    // 这里要创建worker子进程


	// 创建子进程后，父进程的执行流程会返回到这里，子进程不会走进来    
	sigemptyset(&set);              // 信号屏蔽字为空，表示不屏蔽任何信号


	/* 经上面处理work进程去这里WorkerProcessCycle()循环了，执行流正常能到当前这个位置的是master进程 */
    // 当 g_is_stop_programe 被置位true要求程序优雅退出时，那worke进程会从上面函数中一路返回可能会开始进入到这里
    // 所以加个条件 NGX_PROCESS_IS_MASTER == g_process_type 限制在worker子进程返回退出整个程序的过程中也不会走到这面去
	for (;NGX_PROCESS_IS_MASTER == g_process_type; )
	{
		// ngx_log_error_core(0,0,"haha--这是父进程，pid为%P",ngx_pid);

        // sigsuspend(const sigset_t *mask))用于在接收到某个信号之前, 临时用mask替换进程的信号掩码, 并暂停进程执行，直到收到信号为止。
        // sigsuspend 返回后将恢复调用之前的信号掩码。信号处理函数完成后，进程将继续执行。该系统调用始终返回-1，并将errno设置为EINTR。
        // sigsuspend是一个原子操作，包含4个步骤：
		// a)根据给定的参数设置新的mask 并 阻塞当前进程【因为是个空集，所以不阻塞任何信号】
		// b)此时，一旦收到信号，便恢复原先的信号屏蔽【我们原来调用sigprocmask()的mask在上边设置的，阻塞了多达10个信号，从而保证我下边的执行流程不会再次被其他信号截断】
		// c)调用该信号对应的信号处理函数
		// d)信号处理函数返回后，sigsuspend返回，使程序流程继续往下走
		// printf("for进来了！\n"); 
		// 发现，如果print不加\n，无法及时显示到屏幕上，是行缓存问题，以往没注意；可参考https://blog.csdn.net/qq_26093511/article/details/53255970

		sigsuspend(&set); // 阻塞在这里，等待一个信号，此时进程是挂起的，不占用cpu时间，只有收到信号才会被唤醒（返回）；
						  // 此时master进程完全靠信号驱动干活 

		sleep(1);         // 休息1秒  

        if(true == g_is_stop_programe)
        {
            LogErrorCoreAddPrintAddr(NGX_LOG_INFO, 0, "g_is_stop_programe为ture了，要求整个进程退出了，master进程退出死循环...");
            break;
        }

		// 以后扩充.......
	}

}

/**
 * 功能：
	根据给定的参数创建指定数量的子进程，
	因为以后可能要扩展功能，增加参数，所以单独写成一个函数

 * 输入参数：
	无

 * 返回值：
	无

 * 调用了函数：

 * 其他说明：

 * 例子说明：

 */
static void StartCreatWorkerProc(int cnt_workprocess)
{
    LogErrorCoreAddPrintAddr(NGX_LOG_INFO, 0, "");
    
	for (int i = 0; i < cnt_workprocess; ++i)
	{
		CreatWorkerProc(i, "worker process");
	}

    return;
}


/**
 * 功能：
	创建一个worker子进程

 * 输入参数：(int inum, const char* p_procname)
	inum 子进程编号
	p_procname 字符串指针，指向设置的子进程名字

 * 返回值：
	无

 * 调用了函数：

 * 其他说明：

 * 例子说明：

 */
static int CreatWorkerProc(int inum, const char* p_procname)
{
    LogErrorCoreAddPrintAddr(NGX_LOG_INFO, 0, "");
    
	pid_t pid_fork = fork();
	switch (pid_fork)
	{
	case -1:   // 产生子进程失败
		LogErrorCoreAddPrintAddr(NGX_LOG_ALERT, errno, "fork()产生子进程inum =  %d, p_procname = \"%s\"失败!", inum, p_procname);
		return -1;

	case 0:    // 子进程分支
		g_ppid = g_pid;
		g_pid = getpid();
		// 所有worker子进程，在这个函数里不断循环着不出来，也就是说，子进程流程不往下边走;
		WorkerProcessCycle(inum, p_procname);
		break;

	default:
		break;
	}

	return -1;
}

/**
 * 功能：
	worker子进程的功能函数，每个woker子进程，就在这里循环着了
	（无限循环【处理网络事件和定时器事件以对外提供web服务】）

 * 输入参数：(int inum, const char* p_procname)
	inum 子进程编号
	p_procname 子进程名字

 * 返回值：
	无

 * 调用了函数：

 * 其他说明：

 * 例子说明：

 */
static void WorkerProcessCycle(int inum, const char* p_procname)
{
    LogErrorCoreAddPrintAddr(NGX_LOG_INFO, 0, "");
    
	g_process_type = NGX_PROCESS_IS_WORKER;

	SetProcTitle(p_procname);        // 设置标题
	InitWorkerProcess(inum);         // 这里面做了不少重要事情，重点追踪函数  

	LogErrorCoreAddPrintAddr(NGX_LOG_INFO, 0, "子进程%s[pid_%P]已经启动并初始化完成，下面要开始真正干活了...", p_procname, g_pid);

	for (;;)
	{
		ProcessEventsAndTimers();    // 处理网络事件和定时器事件，实质是个epoll事件分发，对应的事件分发给对应的回调函数去执行....

        if(true == g_is_stop_programe)
        {
            LogErrorCoreAddPrintAddr(NGX_LOG_INFO, 0, "g_is_stop_programe为ture，要求整个进程退出了，worker子进程退出死循环...");
            break;
        }

	}
    

    // 如果从上面for循环跳出来,就是子进程要退出了，下面放回收子进程资源的代码

    
    g_threadpool.StopAll();      // 停止线程池       
    g_socket.ShutdownSubproc();  // socket需要释放的东西考虑释放；

    return;
}

/**
 * 功能：
	子进程创建时调用本函数进行一些初始化工作

 * 输入参数：(int inum)
	inum 子进程编号

 * 返回值：
	无

 * 调用了函数：

 * 其他说明：

 * 例子说明：

 */
static void InitWorkerProcess(int inum)
{
    LogErrorCoreAddPrintAddr(NGX_LOG_INFO, 0, "");
    
	sigset_t set;

	sigemptyset(&set);   // 清空信号集
	// 原来是屏蔽那10个信号【防止fork()期间收到信号导致混乱】，现在不再屏蔽任何信号【接收任何信号】
	// 用于改变进程的当前阻塞信号集,也可以用来检测当前进程的信号掩码,这里是改变，由原来的阻塞改为不再阻塞
	if (sigprocmask(SIG_SETMASK, &set, NULL) == -1)
	{
		LogErrorCoreAddPrintAddr(NGX_LOG_ALERT, errno, "sigprocmask()失败!");
	}

    // 线程池代码，率先创建，至少要比和socket相关的内容优先，因为socket起来可能立刻就有事件需要线程处理
    CConfig* p_config  = CConfig::GetInstance();
    // 读配置，若配置文件中没有指定则默认给创建5个线程
    int creat_thread_n = p_config->GetIntDefault(CONFING_ITEMNAME_CREAT_THREAD_N, 5);  
    if(g_threadpool.Create(creat_thread_n) == false)
    {
        LogErrorCoreAddPrintAddr(NGX_LOG_ALERT, 0, "g_threadpool.Create()创建线程池失败!");
        // 内存没释放，但是简单粗暴退出；
        exit(-2);
    }
    sleep(1);        // 再休息1秒；

    if(g_socket.InitSubproc() == false) // 初始化子进程需要具备的一些多线程能力相关的信息
    {
        LogErrorCoreAddPrintAddr(NGX_LOG_ALERT, 0, "g_socket.InitSubproc()失败!");
        // 内存没释放，但是简单粗暴退出；
        exit(-2);
    }
    
	// 如下这些代码参照官方nginx里的ngx_event_process_init()函数中的代码
	g_socket.InitEpoll();           // 初始化epoll相关内容，同时 往监听socket上增加监听事件，从而开始让监听端口履行其职责
	
	// 将来扩充代码
}

