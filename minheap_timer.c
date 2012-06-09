#include "timer.h"
#include "atomic.h"
#include "list.h"
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <time.h>
#include <sys/time.h>
#include <sys/timerfd.h>
#include <sys/types.h>
#include <unistd.h>
#include <string.h>
#include <pthread.h>
#include <signal.h>
#include <stdint.h>
/* #include <math.h> */



///定时器结构
struct mh_timer_internal {
        timer_type type;
        timer_run_type run_type;
        unsigned int interval;
        void* ( *cb )( void * );
        void *param;
        int param_len;
        //timer_id id;
        unsigned int round;	///定时器维护圈数
        struct timespec expiretime;
        //struct list_head list;
};




///定时器管理单元的最原始结构
struct mh_timer_s_internal {
        TIMER_BOOL( *init )( MH_TIMER_MANAGER *this, int max_size );
        TIMER_BOOL( *push )( MH_TIMER_MANAGER *this, struct timer *timer );
        void ( *enable )( MH_TIMER_MANAGER *this );
        void ( *disable )( MH_TIMER_MANAGER *this );
        //TIMER_BOOL( *pop )( MH_TIMER_MANAGER *this );
        //TIMER_BOOL (*reset) (MH_TIMER_MANAGER *this, timer_id id);
        void ( *start )( MH_TIMER_MANAGER *this, timer_start_type type );
        void ( *stop )( MH_TIMER_MANAGER *this );
        void ( *close )( MH_TIMER_MANAGER *this );


        ///支持的最大定时器数量
        int max_timer_num;
        int cur_timer_num;

        struct mh_timer_internal **queue;

        volatile pthread_t pid;
        atomic_t init_flag;
        volatile int start_flag;
        int enable_flag;		//阻止push操作
        pthread_rwlock_t lock;
        pthread_mutex_t mh_lock;

        int timerfd;

};


static TIMER_BOOL ti_init( MH_TIMER_MANAGER *this, int max_size );
static TIMER_BOOL ti_push( MH_TIMER_MANAGER *this, struct timer *timer );
static void ti_stop( MH_TIMER_MANAGER *this );
static void ti_start( MH_TIMER_MANAGER *this, timer_start_type type );
static void ti_close( MH_TIMER_MANAGER *this );
static void ti_enable( MH_TIMER_MANAGER *this );
static void ti_disable( MH_TIMER_MANAGER *this );
MH_TIMER_MANAGER* create_mh_timer_manager_();
void destroy_mh_timer_manager( MH_TIMER_MANAGER * );
static void catch_signal( int i );
static void *entry( void *p );

/* ==  ==  ==  ==  ==  ==  ==  ==  ==  ==  ==  ==  ==  ==  ==  ==  ==  ==  == */

static TIMER_BOOL repush( struct mh_timer_s_internal *this, struct mh_timer_internal *timer );


/**
 * @brief	create_timer
 *
 * 创建最小堆定时器管理对象
 *
 * @return	定时器管理对象指针
 */
MH_TIMER_MANAGER *create_mh_timer_manager()
{
        struct mh_timer_s_internal *p = malloc( sizeof( struct mh_timer_s_internal ) );

        if( p  ==  NULL ) {
                perror( "malloc failed" );
                return NULL;
        }

        memset( p, 0, sizeof( struct mh_timer_s_internal ) );
        p->init = ti_init;
        p->push = ti_push;
        p->enable = ti_enable;
        p->disable = ti_disable;
        p->stop = ti_stop;
        p->start = ti_start;
        p->close = ti_close;
        pthread_rwlockattr_t attr;
        pthread_rwlockattr_init( &attr );
        pthread_rwlockattr_setpshared( &attr, PTHREAD_PROCESS_SHARED );
        pthread_rwlock_init( &p->lock, &attr );
        pthread_mutex_init( &p->mh_lock, NULL );
        pthread_rwlockattr_destroy( &attr );
        atomic_set( &p->init_flag, 0 );
        p->start_flag = 0;
        return ( MH_TIMER_MANAGER * )p;
}

/**
 * @brief	destroy_timer
 *
 * 销毁最小堆定时器管理对象
 *
 * @param	this	定时器对象指针
 */
void destroy_mh_timer_manager( MH_TIMER_MANAGER *this )
{
        struct mh_timer_s_internal *p = ( struct mh_timer_s_internal * )this;
        p->close( this );
        pthread_mutex_destroy( &p->mh_lock );
        pthread_rwlock_destroy( &p->lock );
        free( ( void * )this );
}


/**
 * @brief	init
 *
 * 最小堆定时器管理单元的初始化
 *
 * @param	this	定时器管理对象指针
 * @param	max_size	支持的最大定时器
 *
 * @return	库的布尔值
 */
static TIMER_BOOL ti_init( MH_TIMER_MANAGER *this, int max_size )
{
        if( this  ==  NULL )
                return TIMER_FALSE;

        struct mh_timer_s_internal *p = ( struct mh_timer_s_internal * )this;
        pthread_rwlock_wrlock( &p->lock );

        ///已经初始化了
        if( atomic_read( &p->init_flag )  ==  1 ) {
                fprintf( stderr, "timer has already init \n" );
                pthread_rwlock_unlock( &p->lock );
                return TIMER_FALSE;
        }

        if( ( p->timerfd = timerfd_create( CLOCK_REALTIME, 0 ) ) == -1 ) {
                perror( "create timerfd failed" );
                pthread_rwlock_unlock( &p->lock );
                return TIMER_FALSE;
        }

        if( max_size  <=  0 )
                p->max_timer_num = DEFAULT_TIMER_MAX_NUM;
        else
                p->max_timer_num = max_size;

        p->cur_timer_num =  0;
        p->pid = 0;
        ///初始化所有时间轮节点的链表头
        p->queue = ( struct mh_timer_internal ** )malloc( sizeof( void * ) * ( p->max_timer_num ) );

        if( p->queue == NULL ) {
                pthread_rwlock_unlock( &p->lock );
                return TIMER_FALSE;
        } else
                memset( p->queue, 0, sizeof( void * ) * ( p->max_timer_num ) );

        atomic_set( &p->init_flag,  1 );
        p->enable_flag = 1;
        pthread_rwlock_unlock( &p->lock );
        return TIMER_TRUE;
}


/**
 * @brief	push
 *
 * 添加定时器到定时器管理对象
 *
 * @param	this		定时器管理对象指针
 * @param	timer		定时器对象指针
 *
 * @note
 *	即便定时器管理对象没有运行，也可以添加定时器进去，只是interval的时间是从运行时开始算起
 *
 * @attention
 *
 * 始终保持最小的interval总是位于链表最前面
 *
 * @return	库的布尔值
 */
static TIMER_BOOL ti_push( MH_TIMER_MANAGER *this, struct timer *timer )
{
        if( this  ==  NULL )
                return TIMER_FALSE;

        int s, parent;
        struct mh_timer_s_internal *p = ( struct mh_timer_s_internal* )this;
        struct mh_timer_internal *temp;
        pthread_rwlock_rdlock( &p->lock );

        if( atomic_read( &p->init_flag )  ==  0 ) {
                fprintf( stderr, "TIMER Manager has not been init yet\n" );
                pthread_rwlock_unlock( &p->lock );
                return TIMER_FALSE;
        }

        if( p->enable_flag == 0 ) {
                fprintf( stderr, "TIMER Manager has not been disable\n" );
                pthread_rwlock_unlock( &p->lock );
                return TIMER_TRUE;
        }

        if( timer->interval <= 0 ) {
                fprintf( stderr, "timer is illegal \n" );
                pthread_rwlock_unlock( &p->lock );
                return TIMER_FALSE;
        }

        struct mh_timer_internal *t = malloc( sizeof( struct mh_timer_internal ) );

        if( t  ==  NULL ) {
                perror( "malloc failed" );
                pthread_rwlock_unlock( &p->lock );
                return TIMER_FALSE;
        } else {
                *( struct timer * )t = *timer;
                t->param = malloc( t->param_len );

                if( t->param == NULL ) {
                        perror( "malloc failed\n" );
                        pthread_rwlock_unlock( &p->lock );
                        free( t );
                        return TIMER_FALSE;
                }

                memcpy( t->param, timer->param, t->param_len );

                if( clock_gettime( CLOCK_REALTIME, &t->expiretime ) == -1 ) {
                        perror( "push timer: get time failed" );
                        pthread_rwlock_unlock( &p->lock );
                        free( t );
                        return TIMER_FALSE;
                } else
                        t->expiretime.tv_sec += timer->interval;

                //t->round = t->interval;
        }

        pthread_mutex_lock( &p->mh_lock );

        if( p->cur_timer_num  >= p->max_timer_num ) {
                fprintf( stderr, "ACHIEVE TIMER MAX NUMBER\n" );
                pthread_mutex_unlock( &p->mh_lock );
                pthread_rwlock_unlock( &p->lock );
                return TIMER_FALSE;
        }

        s = p->cur_timer_num++;

        while( s > 0 ) {
                parent = ( s - 1 ) / 2;
                temp = p->queue[parent];

                /*
                                if( p->queue[parent]->round < t->round ) {
                                        break;
                                }
                */
                if( less( temp->expiretime, t->expiretime ) ) {
                        //if( (temp->expiretime.tv_sec < t->expiretime.tv_sec) || ((temp->expiretime.tv_sec == t->expiretime.tv_sec) && (temp->expiretime.tv_nsec < t->expiretime.tv_nsec) )) {
                        break;
                }

                p->queue[s] = temp;
                s = parent;
        }

        p->queue[s] = t;
        pthread_mutex_unlock( &p->mh_lock );
        pthread_rwlock_unlock( &p->lock );
        return TIMER_TRUE;
}


/**
 * @brief	repush
 *
 * 专门针对repeat timer的一个push重定义版本
 *
 * @param	this		定时器内部管理对象指针
 * @param	timer		定时器内部结构指针
 *
 * @return
 */
static TIMER_BOOL repush( struct mh_timer_s_internal *this, struct mh_timer_internal *timer ) //只会被调用，在被调用处会加锁
{
        if( this  ==  NULL )
                return TIMER_FALSE;

        //pthread_rwlock_rdlock( &this->lock );
        struct mh_timer_internal *temp;

        if( this->start_flag == 0 )
                return TIMER_FALSE;

        int s, parent ;
        s = this->cur_timer_num++;

        if( clock_gettime( CLOCK_REALTIME, &timer->expiretime ) == -1 ) {
                perror( "push timer: get time failed" );
                return TIMER_FALSE;
        } else
                timer->expiretime.tv_sec += timer->interval;

        while( s > 0 ) {
                parent = ( s - 1 ) / 2;
                /*
                                if( this->queue[parent]->round < timer->round ) {
                                        break;
                                }
                */
                temp = this->queue[parent];

                if( less( temp->expiretime, timer->expiretime ) ) {
                        //if( (temp->expiretime.tv_sec < timer->expiretime.tv_sec) || ((temp->expiretime.tv_sec == timer->expiretime.tv_sec) && (temp->expiretime.tv_nsec < timer->expiretime.tv_nsec) )) {
                        break;
                }

                this->queue[s] = this->queue[parent];
                s = parent;
        }

        this->queue[s] = timer;
        return TIMER_TRUE;
}
/**
 * @brief	pop
 *
 * 删除定时器
 *
 * @param	p		定时器管理对象
 *
 * @note
 *
 * @return		库布尔值
 */
static TIMER_BOOL ti_pop( struct mh_timer_s_internal *p )		//只会被调用，在被调用处会加锁
{
        if( p  ==  NULL )
                return TIMER_FALSE;

        int s, i = 0, half, child, right;
        struct mh_timer_internal *temp;
        //pthread_rwlock_rdlock( &p->lock );
        //pthread_mutex_lock(&p->mh_lock);

        if( p->start_flag == 0 )
                return TIMER_FALSE;

        /*
        if(p->cur_timer_num <= 0) {
        	return TIMER_FALSE;
        }*/
        s = --p->cur_timer_num;
        half = s / 2;

        while( i < half ) {
                child = 2 * i + 1;
                right = child + 1;
                /*
                                if( right < s && p->queue[child]->round > p->queue[right]->round ) {
                                        child = right;
                                }

                                if( p->queue[s]->round < p->queue[child]->round ) {
                                        break;
                                }

                                p->queue[i]->round = p->queue[child]->round;
                */
                temp = p->queue[child];

                if( right < s && great( temp->expiretime, p->queue[right]->expiretime ) ) {
                        child = right;
                }

                if( less( p->queue[s]->expiretime , temp->expiretime ) ) {
                        break;
                }

                p->queue[i]->expiretime = p->queue[child]->expiretime;
                i = child;
        }

        p->queue[i] = p->queue[s];
        p->queue[s] = NULL;
        //pthread_mutex_unlock(&p->mh_lock);
        //pthread_rwlock_unlock( &p->lock );
        return TIMER_TRUE;
}


/**
 * @brief	stop
 *
 * 停止定时器
 *
 * @param	this	定时器管理对象指针
 */
static void ti_stop( MH_TIMER_MANAGER *this )
{
        if( this  ==  NULL )
                return;

        struct mh_timer_s_internal *p = ( struct mh_timer_s_internal * )this;
        ///如果定时器并没有开始，直接结束
        pthread_rwlock_rdlock( &p->lock );

        if( p->start_flag == 0 ) {
                pthread_rwlock_unlock( &p->lock );
                return ;
        }

        p->start_flag = 0 ;
        sleep( 1 );

        if( p->pid != 0 ) {
                pthread_kill( p->pid, MH_TIMER_STOP_SIGNAL );
                p->pid = 0;
        }

        pthread_rwlock_unlock( &p->lock );
}


/**
 * @brief	start
 *
 * 以两种方式来开启定时器
 *
 * @param	this		定时器管理对象指针
 * @param	type		开启方式
 */
static void ti_start( MH_TIMER_MANAGER *this, timer_start_type type )
{
        if( this  ==  NULL )
                return;

        struct mh_timer_s_internal *p = ( struct mh_timer_s_internal * )this;
        pthread_rwlock_wrlock( &p->lock );

        if( p->start_flag  == 1 ) {
                pthread_rwlock_unlock( &p->lock );
                return;
        } else {
                p->start_flag = 1 ;
        }

        pthread_attr_t attr;
        pthread_attr_init( &attr );

        switch( type ) {
                case TIMER_START_UNBLOCK:
                        pthread_attr_setdetachstate( &attr, PTHREAD_CREATE_DETACHED );

                        if( pthread_create( ( pthread_t * )( &p->pid ), &attr, entry, ( void * )p ) != 0 ) {
                                p->start_flag = 0 ;
                                p->pid = 0;
                                pthread_rwlock_unlock( &p->lock );
                                return;
                        }

                        pthread_rwlock_unlock( &p->lock );
                        break;
                case TIMER_START_BLOCK:

                        if( pthread_create( ( pthread_t * )( &p->pid ), NULL, entry, ( void * )p ) != 0 ) {
                                p->start_flag = 0 ;
                                p->pid = 0;
                                pthread_rwlock_unlock( &p->lock );
                                return;
                        }

                        pthread_rwlock_unlock( &p->lock );

                        if( pthread_join( p->pid, NULL ) != 0 ) {
                                perror( "block failed" );
                        }

                        break;
                default:
                        break;
        }
}

/**
 * @brief	entry
 *
 * 定时器开启的入口函数, 完成定时器的全部工作
 *
 * @param	p
 *
 * @return
 */
static void *entry( void *p )
{
        if( p  ==  NULL )
                return NULL;

        struct mh_timer_s_internal *this = ( struct mh_timer_s_internal * )p;
        pthread_t id;
        pthread_attr_t attr;
        sigset_t sigmask;
        /* sigemptyset(&sigmask);
        sigaddset(&sigmask, TIMER_STOP_SIGNAL);
        pthread_sigmask(SIG_UNBLOCK, &sigmask, NULL);  */
        sigfillset( &sigmask );
        sigdelset( &sigmask, MH_TIMER_STOP_SIGNAL );
        pthread_sigmask( SIG_SETMASK, &sigmask, NULL );
        signal( MH_TIMER_STOP_SIGNAL, catch_signal );
        struct itimerspec new_value;
        struct timespec now, increment;
        /* int timerfd; */
        struct mh_timer_internal *temp;
        uint64_t exp;
        int replay = 0;
        increment.tv_sec = 1;
        increment.tv_nsec = 0;
        new_value.it_value.tv_sec = increment.tv_sec;
        new_value.it_value.tv_nsec = increment.tv_nsec;
        new_value.it_interval.tv_sec = increment.tv_sec;
        new_value.it_interval.tv_nsec = increment.tv_nsec;

        if( timerfd_settime( this->timerfd, 0, &new_value, NULL ) == -1 ) {
                perror( "[timer exit normally] - timer_set failed" );
                goto MH_END;
        }

        while( this->start_flag ) {
                //gettimeofday(&now, NULL);
                if( clock_gettime( CLOCK_REALTIME, &now ) == -1 ) {
                        printf( "get clock time failed\n" );
                        goto MH_END;
                }

                pthread_mutex_lock( &this->mh_lock );
                temp = this->queue[0];

                if( ( temp != NULL ) && gte( temp->expiretime, now ) ) {
                        switch( temp->run_type ) {
                                case SIGNAL:
                                        kill( getpid(), SIGALRM );
                                        break;
                                case THREAD:
                                        pthread_attr_init( &attr );
                                        pthread_attr_setdetachstate( &attr, PTHREAD_CREATE_DETACHED );

                                        if( pthread_create( &id, &attr, temp->cb, ( void * )temp->param )  ==  -1 ) {
                                                perror( "[timer exit normally] - execute expiry func failed" );
                                        }

                                        pthread_attr_destroy( &attr );
                                        break;
                                case DIRECT:
                                        temp->cb( temp->param );
                                default:
                                        break;
                        }

                        switch( temp->type ) {
                                case REPEAT:

                                        if( ti_pop( this ) == TIMER_FALSE ) {
                                                pthread_mutex_unlock( &this->mh_lock );
                                                goto MH_END;
                                        }

                                        if( repush( this, temp ) == TIMER_FALSE ) {
                                                pthread_mutex_unlock( &this->mh_lock );
                                                goto MH_END;
                                        }

                                        break;
                                case SINGLE_SHOT:
                                default:

                                        if( ti_pop( this ) == TIMER_FALSE ) {
                                                pthread_mutex_unlock( &this->mh_lock );
                                                goto MH_END;
                                        }

                                        if( temp->param_len != 0 )
                                                free( temp->param );

                                        free( temp );
                                        break;
                        }
                }

                pthread_mutex_unlock( &this->mh_lock );
                ///只对有定时器的节点进行时间检测，主要是防止直接调用函数，执行函数的时间超过一个time_slot

                if( read( this->timerfd, &exp, sizeof( uint64_t ) ) != sizeof( uint64_t ) ) {
                        perror( "[mh timer exit abnormally] - read failed" );
                        goto MH_END;
                } else {
                        if( exp > 1 ) {
                                fprintf( stderr, "maintain timer_list expire one time_slot\n" );

                                if( ++replay == 5 )
                                        goto MH_END;
                        }
                }
        }

        fprintf( stderr, "mh timer exit normally\n" );
MH_END:
        this->pid = 0;
        this->start_flag = 0;
        return NULL;
}

static void ti_close( MH_TIMER_MANAGER *this )
{
        if( this  ==  NULL )
                return;

        struct mh_timer_s_internal *p = ( struct mh_timer_s_internal * )this;
        struct mh_timer_internal *temp;
        pthread_rwlock_wrlock( &p->lock );

        ///还没有初始化，直接返回
        if( atomic_read( &p->init_flag )  ==  0 )
                return;

        //p->stop( this );
        if( p->start_flag  != 0 ) {
                p->start_flag = 0 ;
                sleep( 1 );

                if( p->pid != 0 ) {
                        pthread_kill( p->pid, MH_TIMER_STOP_SIGNAL );
                        p->pid = 0;
                }
        }

        int cnt = 0;

        for( ; cnt < p->max_timer_num; ++cnt ) {
                temp = p->queue[cnt];

                if( temp != NULL ) {
                        if( temp->param != NULL && temp->param_len != 0 )
                                free( temp->param );

                        free( temp );
                }
        }

        p->max_timer_num = 0;

        if( p->queue )
                free( p->queue );

        if( p->timerfd > 2 )
                close( p->timerfd );

        atomic_set( &p->init_flag,  0 );
        pthread_rwlock_unlock( &p->lock );
}


/* ==  ==  ==  ==  ==  ==  ==  ==  ==  ==  HELPER FUNC ==  ==  ==  ==  ==  ==  ==  ==  ==  ==  ==  == = */
static void ti_enable( MH_TIMER_MANAGER *this )
{
        if( this  ==  NULL )
                return;

        struct mh_timer_s_internal *p = ( struct mh_timer_s_internal * )this;
        pthread_rwlock_rdlock( &p->lock );
        p->enable_flag = 1;
        pthread_rwlock_unlock( &p->lock );
}
static void ti_disable( MH_TIMER_MANAGER *this )
{
        if( this  ==  NULL )
                return;

        struct mh_timer_s_internal *p = ( struct mh_timer_s_internal * )this;
        pthread_rwlock_rdlock( &p->lock );
        p->enable_flag = 0;
        pthread_rwlock_unlock( &p->lock );
}
static void catch_signal( int i )
{
        switch( i ) {
                case 62:
                        pthread_exit( 0 );
                        break;
                default:
                        break;
        }
}


