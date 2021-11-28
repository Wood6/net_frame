// 互斥锁的一个头文件

#ifndef __NGX_C_LOCKMUTEX_H__
#define __NGX_C_LOCKMUTEX_H__

#include <pthread.h>


// 本类用于自动释放互斥量，防止忘记调用pthread_mutex_unlock的情况发生
class CLock
{
private:
    pthread_mutex_t* mp_mutex;

public:
    CLock(pthread_mutex_t* p_mutex)
    {
        mp_mutex = p_mutex;
        pthread_mutex_lock(mp_mutex);    // 加锁互斥量
    }

    ~CLock()
    {
        pthread_mutex_unlock(mp_mutex);  //  解锁互斥量
    }
};

#endif



