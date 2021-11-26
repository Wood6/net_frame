//和开启子进程相关
#include "ngx_global.h"

//处理网络事件和定时器事件，我们遵照nginx引入这个同名函数
void ProcessEventsAndTimers()
{
	g_socket.EpollProcessEvents(-1); //-1表示卡着等待把

	//...再完善
}
