
#include "ngx_global.h"
#include "ngx_c_conf.h"
#include "ngx_global.h"
#include "ngx_macro.h"
#include "ngx_func.h"
#include "ngx_global.h"

#include <sys/time.h>
#include <time.h>

// 全局量---------------------
// 错误等级，和ngx_macro.h里定义的日志等级宏是一一对应关系
static u_char arr2_err_levels[][20] =
{
	{"stderr"},         // 0: 控制台错误
	{"emerg"},          // 1: 紧急
	{"alert"},          // 2: 警戒
	{"crit"},           // 3: 严重
	{"error"},          // 4: 错误
	{"warn"},           // 5: 警告
	{"notice"},         // 6: 注意
	{"info"},           // 7: 信息
	{"debug"}           // 8: 调试
};

gs_log_t gs_log;

/**
 * 功能：
	日志初始化
 * 输入参数：
	无
 * 返回值：
	无
 * 其他说明：
	变参实现原理：通过这挨着变参的那个普通参数（这个函数是fmt）来寻址后续的所有可变参数的类型及其值
 */
void LogInit()
{
    // 单例类，main在全面已经实例化，这里拿到的指针是当时已经实体化的类指针  
	CConfig* p_config = CConfig::GetInstance();  
	const char* p_logpath = p_config->GetString("log_file_path"); // 直接从内存中读取的，不用耗时去读文件了
	if (NULL == p_logpath)
	{
		p_logpath = NGX_ERROR_LOG_PATH;
	}

	gs_log.log_level = p_config->GetIntDefault("log_level", NGX_LOG_NOTICE);
	gs_log.fd = open(p_logpath, O_WRONLY | O_APPEND | O_CREAT, 0644);
	if (-1 == gs_log.fd)
	{
		LogStderr(errno, "[alert] could not open error log file: open() \"%s\" failed", p_logpath);
		gs_log.fd = STDERR_FILENO;  //  如果有错误，则直接定位到 标准错误上去 
	}
}

/**
 * 功能：
	通过可变参数格式化组合出字符串，往标准错误上输出这个字符串

 * 输入参数：
	int err 如果err不为0，表示有错误，会将该错误编号以及对应的错误信息一并放到组合出的字符串中一起显示；
	const char* fmt 可能包含有格式化控制字的一个字符串
	...             变参，参数个数与fmt中的格式化控制字相等，若不相等会出错

 * 返回值：
	无

 * 调用了函数：
	VslPrintf(),

 * 其他说明：
	变参实现原理：通过这挨着变参的那个普通参数（这个函数是fmt）来寻址后续的所有可变参数的类型及其值

 * 例子说明：
	LogStderr(0, "invalid option: \"%s\"", argv[0]);  //nginx: invalid option: "./nginx"
	LogStderr(0, "invalid option: %10d", 21);         //nginx: invalid option:         21  ---21前面有8个空格
	LogStderr(0, "invalid option: %.6f", 21.378);     //nginx: invalid option: 21.378000   ---%.这种只跟f配合有效，往末尾填充0
	LogStderr(0, "invalid option: %.6f", 12.999);     //nginx: invalid option: 12.999000
	LogStderr(0, "invalid option: %.2f", 12.999);     //nginx: invalid option: 13.00
	LogStderr(0, "invalid option: %xd", 1678);        //nginx: invalid option: 68e
	LogStderr(0, "invalid option: %Xd", 1678);        //nginx: invalid option: 68E
	LogStderr(15, "invalid option: %s , %d", "testInfo",326);        //nginx: invalid option: testInfo , 326
 */
void LogStderr(int err, const char* fmt, ...)
{
	u_char arr_errstr[NGX_MAX_ERROR_STR + 1];
	memset(arr_errstr, 0, sizeof(arr_errstr));

	u_char* p_last_arr_errstr = NULL;
	u_char* p_errstr = NULL;   // 实时指向串要输入的位置的指针

	p_last_arr_errstr = arr_errstr + NGX_MAX_ERROR_STR;
	p_errstr = ngx_cpymem(arr_errstr, "nginx: ", 7);

	va_list args;
	va_start(args, fmt);
	p_errstr = VslPrintf(p_errstr, p_last_arr_errstr, fmt, args);
	va_end(args);

	// 如果错误代码不是0，表示有错误发生
	if (err)
	{
		// 错误代码和错误信息也要显示出来
		p_errstr = LogErrno(p_errstr, p_last_arr_errstr, err);
	}

	if (p_errstr >= p_last_arr_errstr)
	{
		// 最后一个有'\0',不能破坏
		// 所以再 -1，在这位置上设置上换行符 '\n'
		p_errstr = p_last_arr_errstr - 1;
	}
	*p_errstr++ = '\n';

	// 往标准错误【一般是屏幕】输出信息 
	// 将arr_errstr往STDERR_FILENO里面写p_errstr - arr_errstr个字节
	write(STDERR_FILENO, arr_errstr, p_errstr - arr_errstr);
	if (gs_log.fd > STDERR_FILENO)    // 如果这是个有效的日志文件，本条件肯定成立，此时也才有意义将这个信息写到日志文件
	{
		// 因为上边已经把err信息显示出来了，所以这里就不要显示了，否则显示重复了
		err = 0;         // 不要再次把错误信息弄到字符串里，否则字符串里重复了

		--p_errstr;      
		*p_errstr = 0;   // 把原来末尾的\n干掉，因为到ngx_log_err_core中还会加这个\n

		// 往日志文件里面写一行错误日志
		LogErrorCore(NGX_LOG_STDERR, err, (const char*)arr_errstr);
	}

}

/**
 * 功能：
	给一段内存p_buf，一个错误编号err，我要组合出一个字符串,形如
	(错误编号: 错误原因)
	将组合好后上行中这种字符串放到给的p_buf内存中去

 * 输入参数：(u_char* p_buf, u_char* p_last, int err)
	p_buf 往这里放按格式转换后的数据
	p_last 放的数据不要超过这里
	err 错误编码

 * 返回值：
	转换放好的数据后面一个指针位

 * 调用了函数：
	无

 * 其他说明：

 * 例子说明：

 */
u_char* LogErrno(u_char* p_buf, u_char* p_last, int err)
{
	char* perror_info = strerror(err);
	size_t len_perror_info = strlen(perror_info);

	// 组合格式字符
	char arr_left_str[10] = { 0 };
	sprintf(arr_left_str, " (%d: ", err);
	size_t len_left_str = strlen(arr_left_str);
	char arr_right_str[] = ") ";
	size_t len_right_str = strlen(arr_right_str);

	size_t len_extra = len_left_str + len_right_str;   // 左右格式字符占用的额外宽度

	// 保证整个我装得下，我就装，否则我全部抛弃 
	// nginx的做法是 如果位置不够，就硬留出50个位置
	// 【哪怕覆盖掉以往的有效内容】，也要硬往后边塞，这样当然也可以；
	if ((p_buf + len_perror_info + len_extra) < p_last)
	{
		p_buf = ngx_cpymem(p_buf, arr_left_str, len_left_str);
		p_buf = ngx_cpymem(p_buf, perror_info, len_perror_info);
		p_buf = ngx_cpymem(p_buf, arr_right_str, len_right_str);
	}

	return p_buf;
}

/**
 * 功能：
	往日志文件中写日志，代码中有自动加换行符，所以调用时字符串不用刻意加\n
	日过定向为标准错误，则直接往屏幕上写日志
	【比如日志文件打不开，则会直接定位到标准错误，此时日志就打印到屏幕上，参考ngx_log_init()】

 * 输入参数：(int level, int err, const char* fmt, ...)
	level 一个等级数字，我们把日志分成一些等级，以方便管理、显示、过滤等等,
		  如果这个等级数字比配置文件中的等级数字"log_level"大，那么该条信息不被写到日志文件中
	err 是个错误代码，如果不是0，就应该转换成显示对应的错误信息,一起写到日志文件中，
	fmt 可能包含有格式化控制字的一个字符串
	... 变参，参数个数与fmt中的格式化控制字%数量相等，若不相等会出错

 * 返回值：
	无

 * 调用了函数：
	SlPrintf(), VslPrintf(), LogErrno(),

 * 其他说明：
	外面调用接口写日志的核心函数

 * 例子说明：

 */
void LogErrorCore(int level, int err, const char* fmt, ...)
{
	struct timeval tv;
	struct tm tm;
	time_t sec;                 // 秒

	memset(&tv, 0, sizeof(struct timeval));
	memset(&tm, 0, sizeof(struct tm));

	gettimeofday(&tv, NULL);   // 获取当前时间，返回自1970-01-01 00:00:00到现在经历的秒数【第二个参数是时区，一般不关心】
	sec = tv.tv_sec;
	localtime_r(&sec, &tm);    // 把参数1的time_t转换为本地时间，保存到参数2中去，带_r的是线程安全的版本，尽量使用
	++tm.tm_mon;               // C的历史原因，时间戳中的月份是从零开始，所以对应到日常月份要 +1
	//tm.tm_yday += 1900;      // tm_yday时间戳在该年中的第几天，这里用错了
    tm.tm_year += 1900;        // 年份 在1900基础上算的秒数换算的，所以对应正常年份 +1900

	u_char arr_curr_time[40] = { 0 };
	// 将时间值格式化到arr_curr_time数组中
	// 若用一个u_char *接一个 (u_char *)-1,则 得到的结果是 0xffffffff....，这个值足够大
	SlPrintf(arr_curr_time, (u_char*)-1, "%4d/%02d/%02d %02d:%02d:%02d",     // 格式是 年/月/日 时:分:秒
		tm.tm_year,
		tm.tm_mon,
		tm.tm_mday,
		tm.tm_hour,
		tm.tm_min,
		tm.tm_sec);
    
	// 定义一个arr_errstr准备存放这个组合各种当前状态后的错误字符串
	u_char* p_arr_errstr = NULL;                  // 指向当前要拷贝数据到其中的内存位置
	u_char* p_last = NULL;
	u_char arr_errstr[NGX_MAX_ERROR_STR + 1];
	memset(arr_errstr, 0, sizeof(arr_errstr));
	p_last = arr_errstr + NGX_MAX_ERROR_STR;


	p_arr_errstr = ngx_cpymem(arr_errstr, arr_curr_time, strlen((const char*)arr_curr_time));  // 时间，得到形如：2019/01/08 20:26:07
	p_arr_errstr = SlPrintf(p_arr_errstr, p_last, " [%s] ", arr2_err_levels[level]);            // 日志等级，得到形如：2019/01/08 20:26:07 [crit] 
	p_arr_errstr = SlPrintf(p_arr_errstr, p_last, "pid_%P: ", g_pid);                               // 进程号，得到形如：2019/01/08 20:50:15 [crit] 2037:

	va_list args;
	va_start(args, fmt);        // 这行不能没有，否则走到VslPrintf中会报段错误
	p_arr_errstr = VslPrintf(p_arr_errstr, p_last, fmt, args);  // 把fmt和args参数弄进去，组合出来这个字符串
	va_end(args);

	// 若错误代码不是0，表示有错误发生
	if (err)
	{
		p_arr_errstr = LogErrno(p_arr_errstr, p_last, err);  // 错误代码和错误信息也要显示出来
	}

	if (p_arr_errstr >= p_last)
	{
		// 最后一个有'\0',不能破坏
		// 所以再 -1，在这位置上设置上换行符 '\n'
		p_arr_errstr = p_arr_errstr - 1;
	}
	*p_arr_errstr++ = '\n';   // 加换行

	ssize_t n = 0;
	while (1)
	{
		// 要打印的这个日志的等级太落后（等级数字太大，比配置文件中的数字大)
		// 这种日志就不打印了
		if (level > gs_log.log_level)
			break;

		// 磁盘是否满了的判断，先算了吧，还是由管理员保证这个事情吧；

		// 写日志文件
		n = write(gs_log.fd, arr_errstr, p_arr_errstr - arr_errstr);
		if (-1 == n)
		{
			if (ENOSPC == errno)
			{
				// 磁盘没空间了,没空间还写个毛线啊,先do nothing吧；
				// .....
			}
			else
			{
				// 这是有其他错误，那么我考虑把这个错误显示到标准错误设备吧；
				if (gs_log.fd != STDERR_FILENO)    // 当前是定位到日志文件的，则条件成立
				{
					n = write(STDERR_FILENO, arr_errstr, p_arr_errstr - arr_errstr);
				}
			}
		}
		// 一定不能忘了，忘了就死循环了。。。
		break;
	}
}


