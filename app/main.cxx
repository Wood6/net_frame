// 主文件

#include "ngx_global.h"
#include "ngx_c_conf.h"
#include "ngx_macro.h"
#include "ngx_func.h"


#ifdef LIYAO_DEBUG
#include <iostream>
#include <string>
#endif 

const char* CONFIG_FILE_PATH = "nginx.conf";
const char LIYAO_TEST_ARR[] = "LIYAO";

using namespace std;

// global variable
size_t g_argv_need_mem = 0;
size_t g_env_need_mem = 0;
int g_os_argc = 0;
char** g_os_argv = NULL;
char* gp_envmem = NULL;   // 指向自己分配的env环境变量的内存，在InitSetProcTitle()函数中会被分配内存


int g_is_daemon = 0;     // 是否开启守护进程模式，0未启用，1启用

// 和进程本身有关的全局变量
pid_t g_pid;           // 当前进程的pid
pid_t g_ppid;          // 父进程pid
int g_process_type;    // 进程类型，用来标识是master进程还是worker进程

// 标记子进程状态变化[一般是子进程发来SIGCHLD信号表示退出],
// sig_atomic_t:系统定义的类型：访问或改变这些变量需要在计算机的一条指令内完成
// 一般等价于int【通常情况下，int类型的变量通常是原子访问的，也可以认为 sig_atomic_t就是int类型的数据】
sig_atomic_t g_flag_workproc_change;


// 专门在程序执行末尾释放资源的函数【一系列的main返回前的释放动作函数】
void FreeResource();

int main(int argc, char **argv)
{
	int exit_code = 0;

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
#ifdef LIYAO_DEBUG_PASS
		string debugStr = environ[i];
#endif
		g_env_need_mem += strlen(environ[i]) + 1;
	}
	g_os_argc = argc;
	g_os_argv = argv;

	// 全局变量有必要初始化的，在这第一部分后面给其先初始化以供后用
	gs_log.fd = -1;                             // -1：表示日志文件尚未打开；因为后边ngx_log_stderr要用所以这里先给-1
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

#ifdef LIYAO_DEBUG_PASS
	cout << "******************************** liyao debug start ********************************" << endl;
	cout << "m_vec_config_item.size() = " << p_config->m_vec_config_item.size() << endl;
	for (std::vector<gp_stru_conf_item_t>::iterator iter = p_config->m_vec_config_item.begin(); iter != p_config->m_vec_config_item.end(); ++iter)
	{
		string strName = (*iter)->c_arr_item_name;
		string strContent = (*iter)->c_arr_iter_content;

		// 输出配置项到标准输出显示
		cout << strName << "=" << strContent << endl;
	}
	cout << endl;
	cout << "log_file_path = " << p_config->GetString("log_file_path") << endl;
	cout << "log_level = " << p_config->GetIntDefault("log_level", 888) << endl;
	cout << "******************************** liyao debug end ********************************" << endl;
#endif

	// 第三部分：一些必须事先准备好的资源，先初始化
	LogInit();
#ifdef LIYAO_DEBUG_PASS
	LogStderr(0, "黎瑶开始测试了。。。!");
	LogStderr(0, "输入888，希望整型数显示，实际显示了%d", 888);
	LogStderr(0, "输入888.888，希望以三位小数位显示，实际显示了%.3f", 888.888);
	LogStderr(0, "输入888.888，希望以两位小数位显示，实际显示了%.2f", 888.888);
	LogStderr(0, "输入888.8，希望以一位小数位显示，实际显示了%.1f", 888.888);
	LogStderr(0, "输入888.8，希望以八位正式位，八位小数位显示，实际显示了%8.8f", 888.888);
	LogStderr(0, "测试结束了。。。!");
#endif
#ifdef LIYAO_DEBUG_PASS
	do
	{
		LogStderr(0, "黎瑶开始测试了写日志文件了。。。!");
		LogErrorCore(NGX_LOG_INFO, 7, "%s第一个写日志来了，后面几行要去看日志文件了哦", " LIYAO ");
		LogErrorCore(NGX_LOG_DEBUG, 8, "调试错误号%d", NGX_LOG_DEBUG);
		LogStderr(0, "测试结束了。。。!");
	} while (0);
#endif

	// 第四部分：一些初始化函数，准备放这里
	if (InitSignals() == false)    // 信号注册
	{
		exit_code = 1;
		goto lblexit;
	}

	// 第五部分：一些不好归类的其他类别代码，准备放这里
	InitSetProcTitle();     // 把环境变量搬家

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


void FreeResource()
{
	// 关闭日志文件
	if (gs_log.fd != STDERR_FILENO && gs_log.fd != -1)
	{
		close(gs_log.fd);   // 不用判断结果了
		gs_log.fd = -1;     // 标记下，防止被再次close吧
	}
}







