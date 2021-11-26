
// 一些和字符串处理相关的函数实现在这文件中
#include <cstdio>
#include <cstring>

#include "ngx_func.h"


// 消除字符串头部所有空格
void LeftTrim(char * s)
{
	if (NULL == s)
		return;

	// 找到第一不为空的字符位置
	char *pStart = s;
	while (*pStart != '\0')
	{
		if (' ' == *pStart)
			++pStart;
		else
			break;
	}

	// 没有空
	if (s == pStart)
		return;

	// 全是空
	if ('\0' == *pStart)
		return;

	// 将不为空数据一个个逐一搬移到前面来
	while ('\0' != *pStart)
	{
		*s = *pStart;
		++s;
		++pStart;
	}
	*s = '\0';                   // 字符串的结尾标记符打上
}

// 消除字符串尾部特殊字符（' '  '\n'  '\r'）,并确保是以'\0'结尾
void RightTrim(char * s)
{
	if (NULL == s)
		return;

	// 找到第一个为' '或'\n'或'\r'的末尾位置

	/*  好吧，似乎没有问题 实际测试反现下面数据也能处理，这个细节后面再追踪下，这下通宵没睡了，有点怕身体出问题了，我要休息下。。。*/
	// 配置要保证输入合法，若出现下面这种情况，有可能有效数据会被截取掉部分
	// DBInfo = 12  7.0. 0.1;  1234
	// 12  7.0. 0.1;  1234 数据部分中间有空格等字符，这个后面的1234就会被截取掉
	size_t len = strlen(s);
	while (len > 0)
	{
		if (' ' == s[len - 1] || '\n' == s[len - 1] || '\r' == s[len - 1])
			break;
		else
			--len;
	}

	if (0 == len)  // 没有空格
		return;

	while (len > 0 && (' ' == s[len - 1] || '\n' == s[len - 1] || '\r' == s[len - 1]))
	{
		--len;
		s[len] = '\0';
	}
}



