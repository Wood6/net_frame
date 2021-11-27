// 内存单例类

#include "ngx_c_memory.h"

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


// 类静态成员初始化赋值
CMemory* CMemory::mp_instance = NULL;

/**
 * 功能：
	分配内存

 * 输入参数：(int mem_count, bool is_memset)
    mem_count 分配的字节大小
    is_memset 是否要把分配的内存初始化为0。因为memset是有开销的，所以这里提供个选择

 * 返回值：
	void* 返回申请的内存首地址

 * 调用了函数：

 * 其他说明：

 * 例子说明：

 */
void* CMemory::AllocMemory(int mem_count, bool is_memset)
{
    // 我并不会判断new是否成功，如果new失败，程序根本不应该继续运行，就让它崩溃以方便我们排错吧
    void* p_ret = (void*)new char[mem_count];
    if(is_memset)   //  要求内存清0
    {
        memset(p_ret, 0, mem_count);
    }

    return p_ret;
}

/**
 * 功能：
	内存释放函数

 * 输入参数：(void* p_mem)
    p_mem 指针，指向要释放的内存首地址

 * 返回值：
	无

 * 调用了函数：

 * 其他说明：

 * 例子说明：

 */
void CMemory::FreeMemory(void* p_mem)
{
    //delete [] p_mem;  //这么删除编译会出现警告：warning: deleting ‘void*’ is undefined [-Wdelete-incomplete]
    delete []((char*)p_mem);   // new的时候是char *，这里弄回char *，以免出警告
}


