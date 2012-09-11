/**
 * @file timer.h
 * @brief
 *
 *  使用时间轮实现的定时器
 *
 * @author tangfu - abctangfuqiang2008@163.com
 * @version 0.1
 * @date 2012-06-09
 */

#ifndef __TIMER_H__
#define	__TIMER_H__


#define TIMER_STOP_SIGNAL SIGRTMAX-2

#define DEFAULT_TIME_SLOT		1000		//1s
#define DEFAULT_SLOT_NUM		10
#define DEFAULT_TIMER_MAX_NUM	64

#ifndef TIMER_BOOL
typedef int TIMER_BOOL;
#define TIMER_FALSE -1
#define TIMER_TRUE	0
#endif


// point to 'struct spectime'
#define less(a,b) 	( (a.tv_sec < b.tv_sec) || ( (a.tv_sec == b.tv_sec) && (a.tv_nsec < b.tv_nsec)) )
#define great(a,b)		( (a.tv_sec > b.tv_sec) || ( (a.tv_sec == b.tv_sec) && (a.tv_nsec > b.tv_nsec)) )
#define gte(a,b)	((a.tv_sec > b.tv_sec) ||  ( (a.tv_sec == b.tv_sec) && (a.tv_nsec >= b.tv_nsec)) )


typedef unsigned int timer_id;
typedef struct timer_manager_s	TIMER_MANAGER;

typedef enum timer_type_s {SINGLE_SHOT, REPEAT} timer_type;
typedef enum timer_start_type_s {TIMER_START_UNBLOCK = 0, TIMER_START_BLOCK} timer_start_type;
typedef enum timer_run_type_s {DIRECT = 0, SIGNAL, THREAD} timer_run_type;

struct timer {
        timer_type type;
        timer_run_type run_type;
        ///单位是毫秒，对于堆定时器，这个代表秒
        unsigned int interval;
        void* ( *cb )( void * );
        void* param;
        int param_len;		//if param is string, param_len 不包含字符串最后的结束符
};

/***********************main_timer***************************/
struct timer_manager_conf {
        ///时间片长度，单位毫秒，相当于定时器的精度
        unsigned int time_slot;
        ///时间片个数，
        unsigned int slot_num;
        ///能够维护的最大定时器个数
        unsigned int timer_max_num;
};

struct timer_manager_s {
        TIMER_BOOL( *init )( TIMER_MANAGER *this, struct timer_manager_conf *conf );
        timer_id( *add )( TIMER_MANAGER *this, struct timer *timer );
        TIMER_BOOL( *del )( TIMER_MANAGER *this, timer_id id );
        void ( *enable )( TIMER_MANAGER *this );
        void ( *disable )( TIMER_MANAGER *this );
        //	TIMER_BOOL (*reset) (TIMER_MANAGER *this, timer_id id);
        void ( *start )( TIMER_MANAGER *this, timer_start_type type );
        void ( *stop )( TIMER_MANAGER *this );
        void ( *close )( TIMER_MANAGER *this );
};

#ifdef __cplusplus
extern "C" {
#endif

        TIMER_MANAGER* create_timer_manager();
        void destroy_timer_manager( TIMER_MANAGER * );

#ifdef __cplusplus
}
#endif

/***********mini heap timer************/
#define MH_TIMER_STOP_SIGNAL SIGRTMAX-3
typedef struct mh_timer_manager_s	MH_TIMER_MANAGER;

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

#ifdef __cplusplus
extern "C" {
#endif

        MH_TIMER_MANAGER* create_mh_timer_manager();
        void destroy_mh_timer_manager( MH_TIMER_MANAGER * );

#ifdef __cplusplus
}
#endif
/**************************************/

#endif		/* __TIMER_H__  */


