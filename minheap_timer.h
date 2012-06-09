/**
 * @file timer.h
 * @brief 定时器
 *
 * 1.暂时只采用单粒度时间轮来实现，未来版本会考虑支持多粒度的时间轮\n
 * 2.未来可能会加入最小堆的实现\n
 * 3.由于使用了线程读写锁, 并设置了pshared属性，因此线程锁可以在多个进程中使用，使用时必须链接pthread库\n
 * 4.库使用了SIGRTMAX-2信号，因此使用时不要冲突\n
 * 5.定时单位是毫秒，因此精确到ms(毫秒)\n
 * 6.添加定时器，不限制重复性，完全一样的定时器可以添加也不会被覆盖
 * 7.由于系统使用了clock_gettime因此使用时必须链接rt库(注意-lrt在-ltimer后面)
 *
 * @author tangfu - abctangfuqiang2008@163.com
 * @version 0.1
 * @date 2011-05-19
 */

#ifndef __TIMER_H__
#define	__TIMER_H__



/***********mini heap timer************/
#define MH_TIMER_STOP_SIGNAL SIGRTMAX-3

#define DEFAULT_TIME_SLOT		1000		//1s
#define DEFAULT_SLOT_NUM		10
#define DEFAULT_TIMER_MAX_NUM	64

typedef unsigned int timer_id;
typedef struct mh_timer_manager_s	MH_TIMER_MANAGER;

typedef enum timer_type_s {SINGLE_SHOT, REPEAT} timer_type;
typedef enum timer_start_type_s {TIMER_START_UNBLOCK = 0, TIMER_START_BLOCK} timer_start_type;
typedef enum timer_run_type_s {DIRECT = 0, SIGNAL, THREAD} timer_run_type;

struct timer {
        timer_type type;
        timer_run_type run_type;
        ///单位时毫秒
        unsigned int interval;
        void* ( *cb )( void * );
        void* param;			//该参数在内部都是拷贝到定时器管理单元中的
        int param_len;		//if param is string, param_len 不包含字符串最后的结束符
};

#ifndef TIMER_BOOL
typedef int TIMER_BOOL;
#define TIMER_FALSE -1
#define TIMER_TRUE	0
#endif


struct mh_timer_manager_s {
        TIMER_BOOL( *init )( MH_TIMER_MANAGER *this, int max_size );
        TIMER_BOOL( *push )( MH_TIMER_MANAGER *this, struct timer *timer );
        void ( *enable )( MH_TIMER_MANAGER *this );
        void ( *disable )( MH_TIMER_MANAGER *this );
        //TIMER_BOOL( *pop )( MH_TIMER_MANAGER *this );	//不允许随便删除
        //	TIMER_BOOL (*reset) (TIMER_MANAGER *this, timer_id id);
        void ( *start )( MH_TIMER_MANAGER *this, timer_start_type type );
        void ( *stop )( MH_TIMER_MANAGER *this );
        void ( *close )( MH_TIMER_MANAGER *this );
};

MH_TIMER_MANAGER* create_mh_timer_manager();
void destroy_mh_timer_manager( MH_TIMER_MANAGER * );
/***********mini heap timer************/



#endif		/* __TIMER_H__  */


