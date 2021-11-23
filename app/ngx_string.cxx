
// һЩ���ַ���������صĺ���ʵ�������ļ���
#include <cstdio>
#include <cstring>

#include "ngx_func.h"


// �����ַ���ͷ�����пո�
void LeftTrim(char * s)
{
	if (NULL == s)
		return;

	// �ҵ���һ��Ϊ�յ��ַ�λ��
	char *pStart = s;
	while (*pStart != '\0')
	{
		if (' ' == *pStart)
			++pStart;
		else
			break;
	}

	// û�п�
	if (s == pStart)
		return;

	// ȫ�ǿ�
	if ('\0' == *pStart)
		return;

	// ����Ϊ������һ������һ���Ƶ�ǰ����
	while ('\0' != *pStart)
	{
		*s = *pStart;
		++s;
		++pStart;
	}
	*s = '\0';                   // �ַ����Ľ�β��Ƿ�����
}

// �����ַ���β�������ַ���' '  '\n'  '\r'��,��ȷ������'\0'��β
void RightTrim(char * s)
{
	if (NULL == s)
		return;

	// �ҵ���һ��Ϊ' '��'\n'��'\r'��ĩβλ��

	/*  �ðɣ��ƺ�û������ ʵ�ʲ��Է�����������Ҳ�ܴ������ϸ�ں�����׷���£�����ͨ��û˯�ˣ��е�������������ˣ���Ҫ��Ϣ�¡�����*/
	// ����Ҫ��֤����Ϸ�����������������������п�����Ч���ݻᱻ��ȡ������
	// DBInfo = 12  7.0. 0.1;  1234
	// 12  7.0. 0.1;  1234 ���ݲ����м��пո���ַ�����������1234�ͻᱻ��ȡ��
	size_t len = strlen(s);
	while (len > 0)
	{
		if (' ' == s[len - 1] || '\n' == s[len - 1] || '\r' == s[len - 1])
			break;
		else
			--len;
	}

	if (0 == len)  // û�пո�
		return;

	while (len > 0 && (' ' == s[len - 1] || '\n' == s[len - 1] || '\r' == s[len - 1]))
	{
		--len;
		s[len] = '\0';
	}
}



