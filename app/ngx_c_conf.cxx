
#include "ngx_func.h"
#include "ngx_global.h"
#include "ngx_c_conf.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>


#ifdef LIYAO_DEBUG
#include <iostream>
#include <string>
using namespace std;
#endif

CConfig* CConfig::m_instance = NULL;

/**
 * 功能：
	将配置文件中的配置读取到CConfig::m_vec_config_item数据结构体中
	将配置从磁盘加载到内存中供程序运行使用
 * 输入参数：
	p_config_name 磁盘中配置文件的路径
 * 返回值：
	false 加载成功
	true  加载失败
 */
bool CConfig::Load(const char * p_config_name)
{
	bool ret = true;

	FILE *fp = NULL;
	fp = fopen(p_config_name, "r");
	if (NULL == fp)
	{
#ifdef LIYAO_DEBUG_PASS
		cout << "errno = " << errno << ", [" << strerror(errno) << "]" << endl;
#endif
		return ret = false;
	}


	// 开始从文件中循环读数据最终存储到内存的数据结构vector中
	char lineBuf[501];
	while (!feof(fp))
	{
		// 读取一行
		if (NULL == fgets(lineBuf, sizeof(lineBuf) - 1, fp))
			continue;

		// 对读取的行数据合法性检测，有非法字符的直接舍弃这行的读取
		if (0 == lineBuf[0])    // 行首为NUL字符跳过
			continue;
		// 读取的数据末尾若有换行，回车，空格，全部去掉
		if (';' == lineBuf[0] || ' ' == lineBuf[0] || '#' == lineBuf[0] || '\t' == lineBuf[0] || '\n' == lineBuf[0])            // 注释行跳过
			continue;

		// NUL字符跳过
		if (0 == lineBuf[0])    // 行首为NUL字符跳过
			continue;
		// '[' 行首为这个跳过
		if ('[' == lineBuf[0])    // 行首为NUL字符跳过
			continue;

		// 执行到这里，合法性检测已经通过。查找行中数据的标志 '='

		char *pChTmp = strchr(lineBuf, '=');
		if (NULL != pChTmp)
		{
			// 将'='两边数据截取暂存到内存变量中
			gp_stru_conf_item_t psConfItem = new gs_conf_iterm_t();
			memset(psConfItem, 0, sizeof(gs_conf_iterm_t));
			strncpy(psConfItem->c_arr_item_name, lineBuf, static_cast<int>(pChTmp - lineBuf));
			strcpy(psConfItem->c_arr_iter_content, pChTmp + 1);

			// 消除数据两边所有空格
			LeftTrim(psConfItem->c_arr_item_name);
			RightTrim(psConfItem->c_arr_item_name);
			LeftTrim(psConfItem->c_arr_iter_content);
			RightTrim(psConfItem->c_arr_iter_content);

			// 将读取的数据插入到类成员的vector数据结构中，以此维护整个系统的配置
			m_vec_config_item.push_back(psConfItem);
		}
	}

	fclose(fp);

#ifdef LIYAO_DEBUG_PASS
	for (auto iter = m_vec_config_item.begin(); iter != m_vec_config_item.end(); ++iter)
	{
		string strName = (*iter)->c_arr_item_name;
		string strContent = (*iter)->c_arr_iter_content;

		// 输出配置项到标准输出显示
		cout << strName << "=" << strContent << endl;
	}
#endif

	return ret;
}

const char* CConfig::GetString(const char *pItemname)
{
	char *ret = NULL;

	std::vector<gp_stru_conf_item_t>::iterator iter;
	for (iter = m_vec_config_item.begin(); iter != m_vec_config_item.end(); ++iter)
	{
		if (0 == strcmp(pItemname, (*iter)->c_arr_item_name))
		{
			return (*iter)->c_arr_iter_content;
		}
	}

	return ret;
}

int CConfig::GetIntDefault(const char *pItemname, const int def)
{
	int ret = 0;

	std::vector<gp_stru_conf_item_t>::iterator iter;
	for (iter = m_vec_config_item.begin(); iter != m_vec_config_item.end(); ++iter)
	{
		if (0 == strcmp(pItemname, (*iter)->c_arr_item_name))
		{
			return atoi((*iter)->c_arr_iter_content);
		}
	}

	return ret = def;
}

