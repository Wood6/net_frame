
// 给保存接受消息那个数据结构申请堆内存用的一个类头文件
// 
#ifndef __NGX_MEMORY_H__
#define __NGX_MEMORY_H__


#include <stddef.h>    // NULL

// 内存相关类，以单例模式实现
class CMemory
{
private:
    // 单例类，所以是私有化下面这三个函数
    CMemory() {};
    CMemory(const CMemory&) {};
    CMemory& operator =(const CMemory&) {};

public:
	~CMemory() {};

private:
    static CMemory* mp_instance;

public:
    static CMemory* GetInstance()
    {
        if(NULL == mp_instance)
        {
            // 锁
            if(NULL == mp_instance)
            {
                // 第一次调用不应该放在线程中，应该放在主进程中，
                // 以免和其他线程调用冲突从而导致同时执行两次new CMemory
                mp_instance = new CMemory();
                static CGarbo c_carbo;
            }
            // 放锁
        }

		return mp_instance;
	};


    // 回收机器人，生命周期结束时会自动释放掉这个单例类的资源
	class CGarbo
	{
	public:
		~CGarbo()
		{
			if (CMemory::mp_instance != NULL)
			{
				delete CMemory::mp_instance;     // 这个释放是整个系统退出的时候，系统来调用释放内存的
				CMemory::mp_instance = NULL;
			}
		}
	};


    void* AllocMemory(int mem_count, bool is_memset);
    void FreeMemory(void* p_mem);
};


#endif




