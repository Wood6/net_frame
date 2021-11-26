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
	CConfig& operator = (const CConfig&) {};

	static CConfig * m_instance;
public:
	std::vector<gp_stru_conf_item_t> m_vec_config_item;    // 保存配置信息的列表
public:
	static CConfig* GetInstance()
	{
		if (NULL == m_instance)
		{
			// 锁，这里还没写，留空到这里
			if (NULL == m_instance)
			{
				m_instance = new CConfig;
				static CGarbo c_carbo;
			}
			// 放锁
		}

		return m_instance;
	}

	// 回收机器人，生命周期结束时会自动释放掉这个单例类的资源
	class CGarbo
	{
	public:
		~CGarbo()
		{
			if (CConfig::m_instance)
			{
				delete CConfig::m_instance;
				CConfig::m_instance = NULL;
			}
		}
	};

	// *******************************************************
	// 再这下面是这个类的公用方法声明
	// *******************************************************
	bool Load(const char * p_conf_name);                                   // 加载配置
	const char* GetString(const char *p_itemname);                      // 根据ItemName获取信息字符串类型配置信息，不修改不用互斥
	int GetIntDefault(const char *p_itemname, const int def);          // 根据ItemName获取数字类型配置信息，不修改不用互斥
};

#endif
