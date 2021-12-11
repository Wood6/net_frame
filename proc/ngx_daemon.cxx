
#include "ngx_global.h"
#include "ngx_func.h"

/**
 * 功能：
	守护进程初始化

 * 输入参数：
	无

 * 返回值：
	执行失败：返回-1，
	子进程：返回0，父进程：返回1

 * 调用了函数：

 * 其他说明：

 * 例子说明：

 */
int CreatDaemon()
{
    LogErrorCoreAddPrintAddr(NGX_LOG_INFO, 0, "");
    
	switch (fork())
	{
	case -1:
		LogErrorCoreAddPrintAddr(NGX_LOG_EMERG, errno, "fork()创建守护进程失败！");
		return -1;
	case 0:
		// 子进程，直接break跳到后面去执行
		break;
	default:
		// 父进程以往 直接退出exit(0);现在希望回到主流程去释放一些资源
		return 1;  // 父进程直接返回
	}

	// 这里执行到的是上面fork()出来的新进程了
	g_ppid = g_pid;
	g_pid = getpid();

	if (setsid() == -1)
	{
		LogErrorCoreAddPrintAddr(NGX_LOG_EMERG, errno, "setsid()失败！");
		return -1;
	}

	// (3)设置为0，不要让它来限制文件权限，以免引起混乱
	umask(0);

	int fd = open("/dev/null", O_RDWR);
	if (-1 == fd)
	{
		LogErrorCoreAddPrintAddr(NGX_LOG_EMERG, errno, "open(\"/dev/null\")失败！");
		return -1;
	}
	// dup2(oldfd, newfd)  将old的文件映射到新的文件描述符newfd上，以后往newfd上写东西就是动写进了oldfd打开的文件里面去了
	// dup2()就是一个重定向的功能
	if (dup2(fd, STDIN_FILENO) == -1)  // 先关闭STDIN_FILENO[这是规矩，已经打开的描述符，动他之前，先close]，类似于指针指向null，让/dev/null成为标准输入；
	{
		LogErrorCoreAddPrintAddr(NGX_LOG_EMERG, errno, "dup2(STDIN)失败!");
		return -1;
	}
	if (dup2(fd, STDOUT_FILENO) == -1) // 再关闭STDIN_FILENO，类似于指针指向null，让/dev/null成为标准输出；
	{
		LogErrorCoreAddPrintAddr(NGX_LOG_EMERG, errno, "dup2(STDOUT)失败!");
		return -1;
	}
	if (fd > STDERR_FILENO)   // fd应该是3，这个应该成立
	{
		if (close(fd) == -1)  //释放资源这样这个文件描述符就可以被复用；不然这个数字【文件描述符】会被一直占着；
		{
			LogErrorCoreAddPrintAddr(NGX_LOG_EMERG, errno, "close(fd)失败!");
			return -1;
		}
	}

	// 子进程顺利执行完，返回0
	return 0;
}


