// 信号相关
#include <signal.h>
#include <sys/wait.h>

#include "ngx_global.h"
#include "ngx_func.h"

static void signal_handler(int signo, siginfo_t* siginfo, void* ucontext);
static void GetProcessStatus();

typedef struct
{
	int signo;                      // 信号的数字编号
	const char* signame;            // 信号字符名字
	void(*handler)(int signo, siginfo_t* siginfo, void* ucontext);  // 信号处理函数的函数指针
}gs_signal_t;


// 数组 ，定义本系统处理的各种信号，我们取一小部分nginx中的信号，并没有全部搬移到这里，日后若有需要根据具体情况再增加
// 在实际商业代码中，你能想到的要处理的信号，都弄进来
gs_signal_t  gs_arr2_signals[] = {
	// signo      signame             handler
	{ SIGHUP,    "SIGHUP",           signal_handler },        // 终端断开信号，对于守护进程常用于reload重载配置文件通知--标识1
	{ SIGINT,    "SIGINT",           signal_handler },        // 标识2   
	{ SIGTERM,   "SIGTERM",          signal_handler },        // 标识15
	{ SIGCHLD,   "SIGCHLD",          signal_handler },        // 子进程退出时，父进程会收到这个信号--标识17
	{ SIGQUIT,   "SIGQUIT",          signal_handler },        // 标识3
	{ SIGIO,     "SIGIO",            signal_handler },        // 指示一个异步I/O事件【通用异步I/O信号】
	{ SIGSYS,    "SIGSYS, SIG_IGN",  NULL           },        // 我们想忽略这个信号，SIGSYS表示收到了一个无效系统调用，如果我们不忽略，进程会被操作系统杀死，--标识31
															  // 所以我们把handler设置为NULL，代表 我要求忽略这个信号，请求操作系统不要执行缺省的该信号处理动作（杀掉我）

    //{ SIGKILL,   "SIGKILL",          signal_handler },	  // SIGKILL，自定义，收到这个优雅自己退出整个程序，在函数中去加个if处理
                                                              // 9 SIGKILL不能被捕获，所以传参给sigaction会报参数错误
    { SIGUSR1,   "SIGUSR1",          signal_handler },	      // 10 SIGUSR1 ，这个信号可以被捕获
                                                              // 自定义，收到这个优雅自己退出整个程序，在函数中去加个if处理
    
    //...日后根据需要再继续增加
	{ 0,         NULL,               NULL           }         // 信号对应的数字至少是1，所以可以用0作为一个特殊标记
};

/**
 * 功能：
	初始化信号的函数，用于注册信号处理程序

 * 输入参数：
	无

 * 返回值：
	true成功，false失败

 * 调用了函数：
	主要调用系统接口实现注册，sigemptyset(), sigaction(),
	ngx_log_error_core()

 * 其他说明：

 * 例子说明：

 */
bool InitSignals()
{
	bool ret = true;

	gs_signal_t *s_sig;
	struct sigaction sa;

	for (s_sig = gs_arr2_signals; s_sig->signo != 0; ++s_sig)
	{
		memset(&sa, 0, sizeof(struct sigaction));

		if (s_sig->handler)
		{
			sa.sa_sigaction = s_sig->handler;   // 挂载信号处理函数
			sa.sa_flags = SA_SIGINFO;           // 传递信息必须要置这个标志
		}
		else
		{
			sa.sa_handler = SIG_IGN;             // sa_handler:这个标记SIG_IGN给到sa_handler成员，表示忽略信号的处理程序，
			                                     // 否则操作系统的缺省信号处理程序很可能把这个进程杀掉；其实sa_handler和
												 // sa_sigaction都是一个函数指针用来表示信号处理程序。只不过这两个函数指针
			                                     // 他们参数不一样， sa_sigaction带的参数多，信息量大，而sa_handler带的参数少，
												 // 信息量少；如果你想用sa_sigaction，那么你就需要把sa_flags设置为SA_SIGINFO；                    
		}

		sigemptyset(&sa.sa_mask);                // 信号集置空，不阻塞任何信号

		// 注册信号
		if (sigaction(s_sig->signo, &sa, NULL) == -1)   // 如果失败打日志并退出
		{
            LogErrorCoreAddPrintAddr(NGX_LOG_EMERG, errno, "sigaction()注册信号[%s]的处理函数时失败！", s_sig->signame);
			return ret = false;
		}
	}

	return ret;
}

/**
 * 功能：
	信号处理函数的函数，对信号signo进行处理的函数

 * 输入参数：(int signo, siginfo_t* siginfo, void* ucontext)
	siginfo 这个系统定义的结构中包含了信号产生原因的有关信息

 * 返回值：
	无

 * 调用了函数：

 * 其他说明：

 * 例子说明：

 */
static void signal_handler(int signo, siginfo_t* siginfo, void* ucontext)
{
	gs_signal_t* sig;

	LogStderr(0, "-------------- 来信号%d，开始进入信号处理流程 --------------------", signo);

	for (sig = gs_arr2_signals; sig->signo != 0; ++sig)
	{


		if (sig->signo == signo)
		{
			// 这里就可以对这个信号进行处理
			/* 将来这里就添加执行代码，目前暂时就往标准输出点信息作为执行到这的查看 */
			LogStderr(0, "信号%d(%s)已经注册处理...", signo, sig->signame);

            // kill -15 pid号，使得程序优雅退出
            if(signo == SIGUSR1)   
            {
                g_is_stop_programe = true;  // 标志程序退出，后面的死循环中对此有检测
                break;
            }

			break;
		}
	}

	char *action = (char*)"";    // 目前还没有动作 

	/***** 多进程时，下面还可以用进程的标志变量来区分处理 ******/
	if (NGX_PROCESS_IS_MASTER == g_process_type)
	{
		switch (signo)
		{
		case SIGCHLD:             // 一般子进程退出会收到该信号
			// 标记子进程状态变化，日后master主进程的for(;;)循环中可能会用到这个变量【比如重新产生一个子进程】
			g_flag_workproc_change = 1;
			break;

			//.....其他信号处理以后增加 case 分支进行处理

		default:
			break;
		}
	}
	else if (NGX_PROCESS_IS_WORKER == g_process_type)  // worker进程，具体干活的进程，处理的信号相对比较少
	{
		// worker进程的往这里走
		// .....以后再增加
		// ....
	}
	else
	{
		// 非master非worker进程，先啥也不干
		// do nothing
	}

	// 这里记录一些信息的信息日志
	if (siginfo && siginfo->si_pid)   // si_pid = sending process ID【发出该信号的进程它的进程id】
	{
		LogErrorCore(NGX_LOG_NOTICE, 0, "signal %d(%s) received from %P%s", signo, sig->signame, siginfo->si_pid, action);
	}
	else
	{
		// 没有发送该信号的进程id，所以不显示发送该信号的进程id
		LogErrorCore(NGX_LOG_NOTICE, 0, "signal %d(%s) received %s", signo, sig->signame, action);
	}

	// ..... 其他需要扩展的将来这里可以写代码

	// 若收到的是信号SIGCHLD，特殊单独处理，避免僵尸进程产生
	// 子进程状态有变化，通常是意外退出【既然官方是在这里处理，我们也学习官方在这里处理】
	if (signo == SIGCHLD)
	{
		// 处理代码，后面补充
		// LogStderr(0, "收到信号%d(%s)，子进程要退出了...", signo, sig->signame);
		GetProcessStatus();          // 获取子进程的结束状态
	}

	LogStderr(0, "-------------- 信号处理流程结束退出了--------------------");
}


/**
 * 功能：
	获取子进程的结束状态，防止单独kill子进程时子进程变成僵尸进程

 * 输入参数：
	无

 * 返回值：
	无

 * 调用了函数：

 * 其他说明：

 * 例子说明：

 */
static void GetProcessStatus()
{
	int status;
	int err;
	int one = 0;     // 抄自官方nginx，应该是标记信号正常处理过一次

	for (; ; )
	{
		// waitpid,有人也用wait,但掌握和使用waitpid即可；
		// 这个waitpid说白了获取子进程的终止状态，这样子进程就不会成为僵尸进程了；
		// 第一次waitpid返回一个> 0值，表示成功，后边显示 2019/01/14 21:43:38 [alert] 3375: pid = 3377 exited on signal 9【SIGKILL】
		// 第二次再循环回来，再次调用waitpid会返回一个0，表示子进程还没结束，然后这里有return来退出；
		pid_t pid_wait = waitpid(-1, &status, WNOHANG); // 第一个参数为-1，表示等待任何子进程，
														// 第二个参数：保存子进程的状态信息(详细了解，可以百度一下)。
														// 第三个参数：提供额外选项，WNOHANG表示不要阻塞，让这个waitpid()立即返回        


		if (0 == pid_wait)
		{
			return;
		}
		if (-1 == pid_wait)
		{
			err = errno;
			if (EINTR == err)           // 调用被某个信号中断
			{
				continue;
			}
			if (ECHILD == err && one)   // 没有子进程
			{
				return;
			}
			if (ECHILD == err)          // 没有子进程
			{
				LogErrorCore(NGX_LOG_INFO, err, "waitpid() failed!");
				return;
			}

			LogErrorCore(NGX_LOG_ALERT, err, "waitpid() failed!");
			return;
		}

		// 走到这里，表示  成功【返回进程id】 ，这里根据官方写法，打印一些日志来记录子进程的退出
		one = 1;  // 标记waitpid()返回了正常返回值
		if (WTERMSIG(status))
		{
			// 获取使子进程终止的信号编号
			LogErrorCore(NGX_LOG_ALERT, 0, "pid = %P exited on signal %d!", pid_wait, WTERMSIG(status));
		}
		else
		{
			// WEXITSTATUS()获取子进程传递给exit或者_exit参数的低八位
			LogErrorCore(NGX_LOG_NOTICE, 0, "pid = %P exited with code %d!", pid_wait, WEXITSTATUS(status));
		}
	}

	return;
}

