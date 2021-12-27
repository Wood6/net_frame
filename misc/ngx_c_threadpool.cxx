// 线程池相关实现

#include "ngx_c_threadpool.h"
#include <stdarg.h>
#include <unistd.h>             // usleep
#include <pthread.h>
#include <arpa/inet.h>          // ntohs()

#include "ngx_global.h"
#include "ngx_func.h"
#include "ngx_c_threadpool.h"
#include "ngx_c_memory.h"
#include "ngx_macro.h"

// 静态成员初始化
pthread_mutex_t CThreadPool::m_pthread_mutex = PTHREAD_MUTEX_INITIALIZER;  // #define PTHREAD_MUTEX_INITIALIZER ((pthread_mutex_t) -1)
pthread_cond_t CThreadPool::m_pthread_cond = PTHREAD_COND_INITIALIZER;     // #define PTHREAD_COND_INITIALIZER ((pthread_cond_t) -1)
bool CThreadPool::m_is_shutdown = false;                                   // 刚开始标记整个线程池的线程是不退出的      


/**
 * 功能：
    构造函数

 * 输入参数：
 	无

 * 返回值：
	无

 * 调用了函数：

 * 其他说明：

 * 例子说明：

 */
CThreadPool::CThreadPool()
{
    LogErrorCoreAddPrintAddr(NGX_LOG_INFO, 0, "");
    
    m_running_thread_n = 0;   // 正在运行的线程，开始给个0【注意这种写法：原子的对象给0也可以直接赋值，当整型变量来用】
    m_last_emg_time = 0;      // 上次报告线程不够用了的时间；
    //m_iPrintInfoTime = 0;   // 上次打印参考信息的时间；

    m_recv_msg_queue_n = 0;   // 收消息队列
}

/**
 * 功能：
    析构函数

 * 输入参数：
 	无

 * 返回值：
	无

 * 调用了函数：

 * 其他说明：

 * 例子说明：

 */
CThreadPool::~CThreadPool()
{
    LogErrorCoreAddPrintAddr(NGX_LOG_INFO, 0, "");
    
    // 资源释放在StopAll()里统一进行，就不在这里进行了

    // 接收消息队列中内容释放
    ClearMsgRecvQueue();
}

/**
 * 功能：
    清理接收消息队列，注意这个函数的写法。

 * 输入参数：
 	无

 * 返回值：
	无

 * 调用了函数：

 * 其他说明：
    注意清理队列结构实质就是清理其中中的每一个元素的这个思想

 * 例子说明：

 */
void CThreadPool::ClearMsgRecvQueue()
{
    LogErrorCoreAddPrintAddr(NGX_LOG_INFO, 0, "");
    
    char* p_tmp = NULL;
    
    // 消息队列是个链表结构，清理这个结构实质是要清理掉这个链表上的每一个元素
    // 所以这里定义一个队列上的元素指针，等会就是调用元素清理函数实现清理过程
    CMemory* p_memory = CMemory::GetInstance();
   
    while(!m_list_rece_msg_queue.empty())
    {
        p_tmp = m_list_rece_msg_queue.front();
        m_list_rece_msg_queue.pop_front();

        // 每个元素经此清理，while循环遍历每个元素完后，整个链表队列也就清理完毕了
        p_memory->FreeMemory(p_tmp);
    }
}

/**
 * 功能：
    创建线程池中的线程，要手工调用，不在构造函数里调用了

 * 输入参数：(int thread_n)
 	thread_n 要创建多少个线程

 * 返回值：
	返回值：所有线程都创建成功则返回true，
	出现错误则返回false

 * 调用了函数：
	系统函数：pthread_create()

 * 其他说明：
    #include <pthread.h>
    int pthread_create(
                  pthread_t *restrict tidp,             // 新创建的线程ID指向的内存单元。
                  const pthread_attr_t *restrict attr,  // 线程属性，默认为NULL
                  void *(*start_rtn)(void *),           // 新创建的线程从start_rtn函数的地址开始运行
                  void *restrict arg                    // 默认为NULL。若上述函数需要参数，将参数放入结构中并将地址作为arg传入。
                   );
    pthread_create() 成功返回0，失败返回错误编号

 * 例子说明：

 */
bool CThreadPool::Create(int thread_n)
{ 
    LogErrorCoreAddPrintAddr(NGX_LOG_INFO, 0, "参数: 线程池创建线程数量thread_n = %d", thread_n);
    
    m_creat_thread_n = thread_n; // 保存要创建的线程数量    

	ps_thread_item_t p_thread = NULL;
	int err = 0;
    for(int i = 0; i < m_creat_thread_n; ++i)
    {
        // 这里有个编程技巧，就是在这里将类的this指针传递给了s_thread_item这个结构体保存
        // 后面ThreadFunc()静态成员函数中就可以利用这个取到类的this指针了
		p_thread = new s_thread_item_t(this);       // new 一个新线程对象 
        m_vec_thread.push_back(p_thread);           // 将新线程对象插入到容器中    

        // 创建线程，实质绑定线程入口函数为ThreadFunc
        err = pthread_create(&p_thread->_Handle, NULL, ThreadFunc, p_thread);      // 创建线程，错误不返回到errno，一般返回错误码
        if(err != 0)
        {
            // 创建线程有错
            LogStderr(err,"CThreadPool::Create()创建线程%ud失败，返回的错误码为%d!", i, err);
            return false;
        }
        else
        {
            // 创建线程成功
            LogStderr(0,"CThreadPool::Create()创建线程%d成功,线程id = %ud", i, p_thread->_Handle);
        }        
    }

    // 我们必须保证每个线程都启动并运行到 pthread_cond_wait()阻塞在等待队列里，等待信号激活使用，
    // 本函数才返回，只有这样线程池中的线程才算全部创建好，才算线程池创建成功
    std::vector<s_thread_item_t*>::iterator iter;
lblfor:
    for(iter = m_vec_thread.begin(); iter != m_vec_thread.end(); ++iter)
    {   
        // is_running 在线程创建后，会在其入口函数ThreadFunc()中执行 pthread_cond_wait() 前会被设置为true
        // 这里依旧为false的说明还没有执行到pthread_cond_wait()阻塞起来，那就goto重新遍历一遍
        // 一定要到所有要求创建的线程都阻塞起来，即这里为true之后才会退出这个函数体返回创建线程池成功
        if( (*iter)->is_running == false)     // 这个条件保证所有线程完全启动起来，以保证整个线程池中的线程正常工作；
        {
            // 这说明有没有启动完全的线程
            usleep(100 * 1000);               // 单位是微妙,又因为1毫秒=1000微妙，所以 100 *1000 = 100毫秒

            goto lblfor;                     // 任意一个创建失败到这里，又返回到for开始处重新遍历一遍
			                                  // 直到要创建的线程数没有一个失败进到这里执行这个goto的
        }
    }

    return true;
}


/**
 * 功能：
    线程入口函数，当用pthread_create()创建线程后，这个ThreadFunc()函数都会被立即执行；
    特别注意，这个函数是个静态成员函数，没有this指针

 * 输入参数：(void* thread_data)
 	thread_data 

 * 返回值：
	void* 

 * 调用了函数：
	系统函数：pthread_self(), pthread_cond_wait()
	自定义函数：CMemory::GetInstance(), CSocket::OutMsgRecvQueue()

 * 其他说明：
    pthread_cond_wait()的说明
    1) pthread_cond_wait会先解除之前的pthread_mutex_lock锁定的mtx，
    2) 然后阻塞在等待队列里休眠，直到再次被唤醒，大多数情况下是等待的条件成立而被唤醒，
    3) 唤醒后，该进程会先锁定，即先pthread_mutex_lock(&mtx)，
    4) 再读取资源

 * 例子说明：

 */
void* CThreadPool::ThreadFunc(void* thread_data)
{
    LogErrorCoreAddPrintAddr(NGX_LOG_INFO, 0, "参数: 创建线程[%ud]时传递的参数thread_data = %p", pthread_self(), thread_data);
    
    // 这个是静态成员函数，是不存在this指针的,所以这里通过这种方式先拿到类的this指针，方便后面代码编码
    ps_thread_item_t p_thread = static_cast<ps_thread_item_t>(thread_data);
    CThreadPool* p_threadpool_obj = p_thread->_pThis;
    
    CMemory* p_memory = CMemory::GetInstance();	    
    
    int err;
    while(true)
    {
        // 线程用pthread_mutex_lock()函数去锁定指定的mutex变量，
        // 若该mutex已经被另外一个线程锁定了，该调用将会阻塞线程直到mutex被解锁。  
        err = pthread_mutex_lock(&m_pthread_mutex);  
        if(err != 0) 
            LogErrorCoreAddPrintAddr(NGX_LOG_ALERT, 0, "pthread_mutex_lock()失败，函数执行的返回值err = %d", err);  // 有问题，要及时报告
        

        // 以下这行程序写法技巧十分重要，必须要用while这种写法，
        // 因为：pthread_cond_wait()是个值得注意的函数，调用一次pthread_cond_signal()可能会唤醒多个【惊群】
        
        // 【官方描述是 至少一个/pthread_cond_signal 在多处理器上可能同时唤醒多个线程】
        // 在《c++入门到精通 c++ 98/11/14/17》里第六章第十三节谈过虚假唤醒，实际上是一个意思；
        // 在《c++入门到精通 c++ 98/11/14/17》里第六章第八节谈过条件变量、wait()、notify_one()、notify_all()，
        // 其实跟这里的pthread_cond_wait、pthread_cond_signal、pthread_cond_broadcast非常类似
        
        // pthread_cond_wait()函数，如果只有一条消息 唤醒了两个线程干活，那么其中有一个线程拿不到消息，
        // 那如果不用while写，就会出问题，所以被惊醒后必须再次用while拿消息，拿到才走下来；
        while( (0 == p_threadpool_obj->m_recv_msg_queue_n) && (false == m_is_shutdown) )
        {
            // 如果这个pthread_cond_wait被唤醒【被唤醒后程序执行流程往下走的前提是拿到了锁--官方：pthread_cond_wait()返回时，互斥量再次被锁住】，
            // 那么会立即再次执行g_socket.OutMsgRecvQueue()，如果拿到了一个NULL，则继续在这里wait着();
            if(p_thread->is_running == false)            
                p_thread->is_running = true;  // 标记为true了才允许调用StopAll()：测试中发现
                                              // 如果Create()和StopAll()紧挨着调用，就会导致线程混乱，
                                              // 所以每个线程必须执行到这里，才认为是启动成功了；
            
            // LogStderr(0,"执行了pthread_cond_wait-------------begin");
            // 刚开始执行pthread_cond_wait()的时候，会卡在这里，而且m_pthread_mutex会被释放掉；
            pthread_cond_wait(&m_pthread_cond, &m_pthread_mutex);   // 整个服务器程序刚初始化的时候，所有线程必然是卡在这里等待的；
            //LogStderr(0,"执行了pthread_cond_wait-------------end");
        }

        // 能走下来的，必然是 拿到了真正的 消息队列中的数据   或者 m_is_shutdown == tru
        //pthread_t tid = pthread_self();                                // 获取线程自身id，以方便调试打印信息等

        // 走到这里时刻，互斥量肯定是锁着的。。。。。。
        
        // 先判断线程退出这个条件
        // 这个变量在stop()中会被设置ture
        if(m_is_shutdown)
        {            
            pthread_mutex_unlock(&m_pthread_mutex);                     // 解锁互斥量
            break; 
        }

        // 走到这里，可以取得消息进行处理了【消息队列中必然有消息】,注意，目前还是互斥着呢
        char *jobbuf = p_threadpool_obj->m_list_rece_msg_queue.front();  // 返回第一个元素但不检查元素存在与否
        p_threadpool_obj->m_list_rece_msg_queue.pop_front();             // 移除第一个元素但不返回	
        --p_threadpool_obj->m_recv_msg_queue_n;                          // 收消息队列数字-1
               
        // 可以解锁互斥量了,让其他线程可以拿到锁
        err = pthread_mutex_unlock(&m_pthread_mutex);
        if(err != 0)  
            LogErrorCoreAddPrintAddr(NGX_LOG_ALERT, 0, "pthread_mutex_unlock()失败，函数执行的返回值err = %d", err);            // 有问题，要及时报告
        
        // 加个信息日志，方便调试
        LogErrorCoreAddPrintAddr(NGX_LOG_INFO, 0, "线程[%ud]被激活正在处理从消息队列中取出最上面一个消息，"
                                                   "消息队列中最上面一个消息表示[包头+包体]的长度len_pkg = %ud",\
                                                  pthread_self(), ntohs(((gps_pkg_header_t)(jobbuf+sizeof(gs_msg_header_t)))->len_pkg ) );

        // 能走到这里的，就是有消息可以处理，开始处理
        ++p_threadpool_obj->m_running_thread_n;    // 原子+1，这比互斥量要快很多，运行线程数+1

        g_socket.ThreadRecvProcFunc(jobbuf);       // 处理消息队列中来的消息

        p_memory->FreeMemory(jobbuf);              // 释放消息内存 
        --p_threadpool_obj->m_running_thread_n;    // 原子-1，运行线程数-1

    } 

    // 能走出来表示整个程序要结束啊，怎么判断所有线程都结束？
    return (void*)0;
}


/**
 * 功能：
    停止所有线程【等待结束线程池中所有线程，该函数返回后，应该是所有线程池中线程都结束了】
    
 * 输入参数：
 	无

 * 返回值：
	无

 * 调用了函数：
    系统函数：pthread_cond_broadcast(),pthread_join(),pthread_mutex_destroy(),pthread_cond_destroy()
    
 * 其他说明：
    pthread_cond_broadcast(),激活所有阻塞着的线程逐个去拿锁退出阻塞
    
    pthread_join(),pthread_join使一个线程等待另一个线程结束，通常用法是主进程等待子线程
                   代码中如果没有pthread_join主线程会很快结束从而使整个进程结束，
                   从而使创建的线程没有机会开始执行就结束了。加入pthread_join后，
                   主线程会一直等待直到等待的线程结束自己才结束，使创建的线程有机会执行。
                   
    pthread_mutex_destroy(),互斥锁销毁函数在执行成功后返回 0，否则返回错误码。
    
    pthread_cond_destroy(),用来销毁一个条件变量，
                           需要注意的是只有在没有线程在该条件变量上等待时，才可以注销条件变量，
                           否则会返回EBUSY。同时Linux在实现条件变量时，并没有为条件变量分配资源，
                           所以在销毁一个条件变量时，只要注意该变量是否仍有等待线程即可。

 * 例子说明：

 */
void CThreadPool::StopAll() 
{
    LogErrorCoreAddPrintAddr(NGX_LOG_INFO, 0, "");
    // (1)已经调用过，就不要重复调用了
    if(true == m_is_shutdown)
    {
        return;
    }
    
    m_is_shutdown = true;

    // (2)唤醒等待该条件【卡在pthread_cond_wait()的】的所有线程，一定要在改变条件状态以后再给线程发信号
    int err = pthread_cond_broadcast(&m_pthread_cond); 
    if(err != 0)
    {
        // 这肯定是有问题，要打印紧急日志
		LogStderr(err, "CThreadPool::StopAll()中pthread_cond_broadcast()失败，返回的错误码为%d!", err);
        return;
    }

    // (3)等等线程，让线程真返回    
    std::vector<s_thread_item_t*>::iterator iter;
	for(iter = m_vec_thread.begin(); iter != m_vec_thread.end(); ++iter)
    {
        pthread_join((*iter)->_Handle, NULL); // 等待子线程终止，
                                              // 本质是等待线程激活后一个个去执行完其被指定的执行函数后退出，
                                              // 等他们执行完线程函数退出后，主进程就可以在此之后一一回收线程资源了

        // 这里再补充一点，linux线程其实本质是系统调用clone()函数体，重新生成了一个“进程”
    }

    // 流程走到这里，那么所有的线程池中的线程肯定都返回了；
    pthread_mutex_destroy(&m_pthread_mutex);
    pthread_cond_destroy(&m_pthread_cond);    

    // (4)释放一下new出来的s_thread_item_t【线程池中的线程】    
	for(iter = m_vec_thread.begin(); iter != m_vec_thread.end(); ++iter)
	{
		if(*iter)
			delete *iter;
	}
	m_vec_thread.clear();

    LogErrorCoreAddPrintAddr(NGX_LOG_INFO, 0, "线程池回收程序StopAll()成功执行完，线程池中线程全部正常结束");
    
    return;    
}


/**
 * 功能：
    来任务了，调一个线程池中的线程下来干活
    
 * 输入参数：
 	无

 * 返回值：
	无

 * 调用了函数：
    系统函数：pthread_cond_signal()
    
 * 其他说明：
    pthread_cond_signal(),送一个信号给另外一个正在处于阻塞等待状态的线程,
                          使其脱离阻塞状态,继续执行.
                          如果没有线程处在阻塞等待状态,pthread_cond_signal也会成功返回。
                          
    使用pthread_cond_signal不会有“惊群现象”产生，他最多只给一个线程发信号。
    假如有多个线程正在阻塞等待着这个条件变量的话，那么是根据各等待线程优先级的高低
    确定哪个线程接收到信号开始继续执行。如果各线程优先级相同，则根据等待时间的长短
    来确定哪个线程获得信号。但无论如何一个pthread_cond_signal调用最多发信一次。

    pthread_cond_wait必须放在pthread_mutex_lock和pthread_mutex_unlock之间，因为
    他要根据共享变量的状态来决定是否要等待，而为了不永远等待下去所以必须要在lock/unlock队中
    
 * 例子说明：

 */
void CThreadPool::Call()
{
    LogErrorCoreAddPrintAddr(NGX_LOG_INFO, 0, "");

    int err = pthread_cond_signal(&m_pthread_cond); // 唤醒一个等待该条件的线程，也就是可以唤醒卡在pthread_cond_wait()的线程
    if(err != 0 )
    {
        // 这是有问题啊，要打印日志啊
		LogErrorCoreAddPrintAddr(NGX_LOG_ALERT, 0, "pthread_cond_signal()失败，函数执行返回值err = %d", err);
    }

    if(m_creat_thread_n == m_running_thread_n)  // 线程池中线程总量，跟当前正在干活的线程数量一样，说明所有线程都忙碌起来，线程不够用了
    {        
        // 线程不够用了
        time_t currtime = time(NULL);
        if(currtime - m_last_emg_time > 10)     // 最少间隔10秒钟才报一次线程池中线程不够用的问题；
        {
            // 两次报告之间的间隔必须超过10秒，不然如果一直出现当前工作线程全忙，但频繁报告日志也够烦的
            m_last_emg_time = currtime;         // 更新时间
            // 写日志，通知这种紧急情况给用户，用户要考虑增加线程池中线程数量了
			LogErrorCoreAddPrintAddr(NGX_LOG_ALERT, 0, "发现线程池中干活线程与总线程数一样了，表示当前空闲线程数量为0，要考虑扩容线程池了!");
        }
    } 
    
    return;
}

/**
 * 功能：
    当收到一个完整包之后，将完整包入消息队列，这个包在服务器端应该是 消息头+包头+包体 格式
    
 * 输入参数：(char* p_buf)
    p_buf 指针，指向一段内存=消息头 + 包头 + 包体


 * 返回值：
	无

 * 调用了函数：

 * 其他说明：
    注意清理队列结构实质就是清理其中中的每一个元素的这个思想

 * 例子说明：

 */
void CThreadPool::AddMsgRecvQueueAndSignal(char* p_buf)
{
    LogErrorCoreAddPrintAddr(NGX_LOG_INFO, 0, "参数: p_buf = %p", p_buf);

	//  互斥
	int err = pthread_mutex_lock(&m_pthread_mutex);
	if (err != 0)
	{
		LogStderrAddPrintAddr(err, "pthread_mutex_lock()失败，返回的错误码为%d", err);
	}

	m_list_rece_msg_queue.push_back(p_buf);  // 入消息队列
	++m_recv_msg_queue_n;                    // 收消息队列数字+1，个人认为用成员变量更方便一点，
	                                         // 比 m_list_rece_msg_queue.size()高效

	// 取消互斥
	err = pthread_mutex_unlock(&m_pthread_mutex);
	if (err != 0)
	{
		LogStderrAddPrintAddr(err, "pthread_mutex_unlock()失败，返回的错误码为%d", err);
	}

    // 加个信息日志，方便调试
    LogErrorCoreAddPrintAddr(NGX_LOG_INFO, 0, "在发信号给线程之前从消息队列中取出最上面一个消息，消息队列中最上面一个消息表示[包头+包体]的长度len_pkg = %ud",\
                                  ntohs(((gps_pkg_header_t)(m_list_rece_msg_queue.front()+sizeof(gs_msg_header_t)))->len_pkg ) );

	// 可以激发一个线程来干活了
	Call();
}


// 唤醒丢失问题，sem_t sem_write;
// 参考信号量解决方案：https://blog.csdn.net/yusiguyuan/article/details/20215591  linux多线程编程--信号量和条件变量 唤醒丢失事件



