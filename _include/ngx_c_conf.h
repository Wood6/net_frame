// 配置文件相关头文件
#ifndef __NGX_C_CONF_H__
#define __NGX_C_CONF_H__

#include <vector>

#include "ngx_global.h"

class CConfig
{
private:
	// 单例类将这些私有化，禁止默认构造方式
	CConfig() {};
	CConfig(const CConfig&) {};
	// 正常是不能用这个传参来做直接返回的
	// 但这里加上返回值是消除警告的，反正这个已经私有化了，外界是调用不了的
	// 即这个赋值构造永远都不会被调用，所以为了消除编译器警告，这样用吧
	CConfig& operator = (const CConfig& obj) { return const_cast<CConfig&>(obj); };

	static CConfig * mp_instance;
public:
	std::vector<gps_stru_conf_item_t> m_vec_config_item;    // 保存配置信息的列表
public:
	static CConfig* GetInstance()
	{
		if (NULL == mp_instance)
		{
			// 锁，这里还没写，留空到这里
			if (NULL == mp_instance)
			{
				mp_instance = new CConfig;
				static CGarbo c_carbo;
			}
			// 放锁
		}

		return mp_instance;
	}

	// 回收机器人，生命周期结束时会自动释放掉这个单例类的资源
	class CGarbo
	{
	public:
		~CGarbo()
		{
			if (CConfig::mp_instance)
			{
				delete CConfig::mp_instance;
				CConfig::mp_instance = NULL;
			}
		}
	};

	// *******************************************************
	// 再这下面是这个类的公用方法声明
	// *******************************************************
	bool Load(const char * p_conf_name);                               // 加载配置
	const char* GetString(const char *p_itemname);                     // 根据ItemName获取信息字符串类型配置信息，不修改不用互斥
	int GetIntDefault(const char *p_itemname, const int def);          // 根据ItemName获取数字类型配置信息，不修改不用互斥
};

#endif
