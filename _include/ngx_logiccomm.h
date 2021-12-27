
// 与客户端定义业务逻辑相关的数据结构

#ifndef __NGX_LOGICCOMM_H__
#define __NGX_LOGICCOMM_H__

// 收发命令宏定义
#define _CMD_START	                    0  
#define _CMD_REGISTER 		            _CMD_START + 5   // 注册
#define _CMD_LOGIN 		                _CMD_START + 6   // 登录
#define _CMD_PING				   	    _CMD_START + 0   // ping命令【心跳包】


// 结构定义
#pragma pack (1)   // 对齐方式,1字节对齐【结构之间成员不做任何字节对齐：紧密的排列在一起】
typedef struct _s_register
{
	int           type;           // 类型
	char          username[56];   // 用户名 
	char          password[40];   // 密码

}gs_register_t, *gps_register_t;

typedef struct _gs_login
{
	char          username[56];   // 用户名 
	char          password[40];   // 密码

}gs_login_t, *gps_login_t;
#pragma pack()    // 取消指定对齐，恢复缺省对齐

#endif
