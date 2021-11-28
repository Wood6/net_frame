// 线程池相关的头文件

#ifndef __NGX_C_THREADPOOL_H__
#define __NGX_C_THREADPOOL_H__

#include <vector>
#include <atomic>      // c++11里的原子操作

#include <pthread.h>


// 线程池相关类
class CThreadPool
{
private:
    static pthread_mutex_t     m_pthread_mutex;     // 线程同步互斥量/也叫线程同步锁
    static pthread_cond_t      m_pthread_cond;      // 线程同步条件变量
    static bool                m_is_shutdown;       // 线程退出标志，false不退出，true退出

    int                        m_creat_thread_n;    // 要创建的线程数量

    //int                      m_running_thread_n;  // 线程数, 运行中的线程数	
    std::atomic<int>           m_running_thread_n;  // 线程数, 运行中的线程数，原子操作
    time_t                     m_last_emg_time;     // 上次发生线程不够用【紧急事件】的时间,防止日志报的太频繁
    //time_t                   m_iPrintInfoTime;    // 打印信息的一个间隔时间，我准备10秒打印出一些信息供参考和调试
    //time_t                   m_iCurrTime;         // 当前时间


    // 定义一个 线程池中的 线程 的结构，以后可能做一些统计之类的 功能扩展，
    // 所以引入这么个结构来 代表线程 更方便一些；    
    typedef struct s_thread_item   
    {
        pthread_t    _Handle;                        // 线程句柄,实质是以此标记这个结构体实例化后的首地址
        CThreadPool* _pThis;                         // 记录线程池的指针   
        bool         is_running;                     // 标记是否正式启动起来，启动起来后，才允许调用StopAll()来释放

        // 构造函数
		s_thread_item(CThreadPool* pthis) : _pThis(pthis), is_running(false){}
        //  析构函数
        ~s_thread_item(){}
    }s_thread_item_t, *ps_thread_item_t;

	std::vector<ps_thread_item_t>  m_vec_thread;      // 线程 容器，容器里就是各个线程了 

private:
    static void* ThreadFunc(void* thread_data);       // 新线程的线程回调函数

public:
    // 构造函数
    CThreadPool();                 
    // 析构函数
    ~CThreadPool();                           

    bool Create(int thream_n);                  // 创建该线程池中的所有线程
    void StopAll();                             // 使线程池中的所有线程退出
    void Call(int irmqc);                       // 来任务了，调一个线程池中的线程下来干活

};


#endif


