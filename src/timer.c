#include "timer.h"
#include "atomic.h"
#include "list.h"
#include "rbtree.h"
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

#define TIMER_FD_QUEUE_LEN	50

///检查配置文件是否合法
#define check_timer_manager_conf(cf) (cf->time_slot > 0  && cf->slot_num >= 2  && cf->timer_max_num > 0)
/*
///计算位图所占字节数
#define bitmap_bytes(cnt)		(cnt - 1) / 8 + 1
///设置位图的id位(设为1)
#define set_bit(pointer, id)	*(pointer + (id - 1) / 8) |= (1 << (7 - (id - 1) % 8))
///取消位图的id位的设置(设为0)
#define unset_bit(pointer, id)	*(pointer + (id - 1) / 8) &= ~(1 << (7 - (id - 1) % 8))
*/

///计算位图所占字节数
#define bitmap_bytes(cnt)   ((cnt - 1) >> 3) + 1
///设置位图的id位(设为1)
#define set_bit(pointer, id)	*(pointer + ((id - 1) >> 3) ) |= (128U >> ((id - 1) & 7))
/////取消位图的id位的设置(设为0)
#define unset_bit(pointer, id)    *(pointer + ((id - 1) >> 3)) &= ~(128U >> ((id - 1) & 7))


///定时器管理单元的节点
struct timer_node {
    ///时间片号
    int slot_id;
    ///该时间片号下对应的定时器个数
    atomic_t timer_cnt;
    /* int slot;  */
    struct list_head head;
};


///定时器结构
struct timer_internal {
    timer_type type;
    timer_run_type run_type;
    unsigned int interval;
    void*(*cb)(void *);
    void *param;
    int param_len;
    timer_id id;
    unsigned int round;	///时间轮圈数
    struct list_head list;
};




///定时器管理单元的最原始结构
struct timer_s_internal {
    TIMER_BOOL(*init)(TIMER_MANAGER *this, struct timer_manager_conf *conf);
    timer_id(*add)(TIMER_MANAGER *this, struct timer *timer);
    TIMER_BOOL(*del)(TIMER_MANAGER *this, timer_id id);
    void (*enable)(TIMER_MANAGER *this);
    void (*disable)(TIMER_MANAGER *this);
    //TIMER_BOOL (*reset) (TIMER_MANAGER *this, timer_id id);
    void (*start)(TIMER_MANAGER *this, timer_start_type type);
    void (*stop)(TIMER_MANAGER *this);
    void (*close)(TIMER_MANAGER *this);

    unsigned int time_slot; ///毫秒ms
    unsigned int slot_num;	///时间片个数
    ///支持的最大定时器数量
    unsigned int timer_max_num;
    atomic_t cur_timer_num;
    volatile unsigned int cur_slot;

    ///timer_id从1开始
    volatile pthread_t pid;
    volatile timer_id timer_fd_min_free;
    atomic_t init_flag;
    volatile int start_flag;
    int enable_flag;		//阻止add操作
    pthread_rwlock_t lock;

    int timerfd;
    unsigned char *timer_fd_bitmap;
    struct timer_node *data;
    rb_node_t *rb_root;
};


static TIMER_BOOL ti_init(TIMER_MANAGER *this, struct timer_manager_conf *conf);
static timer_id ti_add(TIMER_MANAGER *this, struct timer *timer);
static TIMER_BOOL ti_del(TIMER_MANAGER *this, timer_id id);
static void ti_stop(TIMER_MANAGER *this);
static void ti_start(TIMER_MANAGER *this, timer_start_type type);
static void ti_close(TIMER_MANAGER *this);
static void ti_enable(TIMER_MANAGER *this);
static void ti_disable(TIMER_MANAGER *this);
TIMER_MANAGER *create_timer_manager_();
void destroy_timer_manager(TIMER_MANAGER *);
static void catch_signal(int i);
static void *entry(void *p);

/* ==  ==  ==  ==  ==  ==  ==  ==  ==  ==  ==  ==  ==  ==  ==  ==  ==  ==  == */

timer_id find_min_id(unsigned char *tmp, int len);
static inline timer_id timer_id_pop(struct timer_s_internal *this);
static inline void timer_id_push(struct timer_s_internal *this, timer_id id);
static TIMER_BOOL del_and_add(struct timer_s_internal *this, struct timer_internal *timer, struct timer_node *node);
static inline TIMER_BOOL check_timer(struct timer_s_internal *this, struct timer *conf);

/**
 * @brief	create_timer
 *
 * 创建定时器管理对象
 *
 * @return	定时器管理对象指针
 */
TIMER_MANAGER *create_timer_manager()
{
    struct timer_s_internal *p = malloc(sizeof(struct timer_s_internal));

    if(p  ==  NULL) {
        perror("malloc failed");
        return NULL;
    }

    memset(p, 0, sizeof(struct timer_s_internal));
    p->init = ti_init;
    p->add = ti_add;
    p->del = ti_del;
    p->enable = ti_enable;
    p->disable = ti_disable;
    p->stop = ti_stop;
    p->start = ti_start;
    p->close = ti_close;
    pthread_rwlockattr_t attr;
    pthread_rwlockattr_init(&attr);
    pthread_rwlockattr_setpshared(&attr, PTHREAD_PROCESS_SHARED);
    pthread_rwlock_init(&p->lock, &attr);
    pthread_rwlockattr_destroy(&attr);
    atomic_set(&p->init_flag, 0);
    //atomic_set( &p->start_flag, 0 );
    p->start_flag = 0;
    return (TIMER_MANAGER *)p;
}

/**
 * @brief	destroy_timer
 *
 * 销毁定时器管理对象
 *
 * @param	this	定时器对象指针
 */
void destroy_timer_manager(TIMER_MANAGER *this)
{
    struct timer_s_internal *p = (struct timer_s_internal *)this;
    p->close(this);
    pthread_rwlock_destroy(&p->lock);
    free((void *)this);
}


/**
 * @brief	init
 *
 * 定时器管理单元的初始化
 *
 * @param	this	定时器管理对象指针
 * @param	conf	定时器配置结构
 *
 * @return	库的布尔值
 */
static TIMER_BOOL ti_init(TIMER_MANAGER *this, struct timer_manager_conf *conf)
{
    if(this  ==  NULL || conf  ==  NULL) {
        return TIMER_FALSE;
    }

    struct	timer_s_internal *p = (struct timer_s_internal *)this;
    unsigned int i = 0;
    pthread_rwlock_wrlock(&p->lock);

    ///已经初始化了
    if(atomic_read(&p->init_flag)  ==  1) {
        fprintf(stderr, "timer has already init \n");
        pthread_rwlock_unlock(&p->lock);
        return TIMER_FALSE;
    } else {
        atomic_set(&p->init_flag,  1);
    }

    if((p->timerfd = timerfd_create(CLOCK_REALTIME, 0)) == -1) {
        perror("create timerfd failed");
        atomic_set(&p->init_flag, 0);
        pthread_rwlock_unlock(&p->lock);
        return TIMER_FALSE;
    }

    if(conf  ==  NULL) {
        p->time_slot = DEFAULT_TIME_SLOT;
        p->slot_num = DEFAULT_SLOT_NUM;
        p->timer_max_num = DEFAULT_TIMER_MAX_NUM;
    } else {
        if(check_timer_manager_conf(conf) == 0) {
            fprintf(stderr, "timer_conf is illegal \n");
            atomic_set(&p->init_flag, 0);
            pthread_rwlock_unlock(&p->lock);
            return TIMER_FALSE;
        } else {
            p->time_slot = conf->time_slot;
            p->slot_num = conf->slot_num;
            p->timer_max_num = conf->timer_max_num;
        }
    }

    atomic_set(&p->cur_timer_num,  0);
    p->data = malloc(sizeof(struct timer_node) * p->slot_num);

    if(p->data  ==  NULL) {
        perror("malloc failed");
        atomic_set(&p->init_flag, 0);
        pthread_rwlock_unlock(&p->lock);
        return TIMER_FALSE;
    } else {
        memset(p->data, 0,  sizeof(struct timer_node) * p->slot_num);
    }

    p->timer_fd_bitmap = malloc(bitmap_bytes(p->timer_max_num));

    if(p->timer_fd_bitmap  == NULL) {
        perror("malloc failed");
        free(p->data);
        p->data = NULL;
        atomic_set(&p->init_flag, 0);
        pthread_rwlock_unlock(&p->lock);
        return TIMER_FALSE;
    } else {
        memset(p->timer_fd_bitmap,  0,  sizeof(bitmap_bytes(p->timer_max_num)));
        p->timer_fd_min_free = 1;
    }

    ///初始化所有时间轮节点的链表头
    while(i < p->slot_num) {
        INIT_LIST_HEAD(&(p->data[i].head));
        p->data[i].slot_id = i;
        ++i;
    }

    pthread_rwlock_unlock(&p->lock);
    p->enable_flag = 1;
    return TIMER_TRUE;
}


/**
 * @brief	add
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
static timer_id ti_add(TIMER_MANAGER *this, struct timer *timer)
{
    if(this  ==  NULL) {
        return TIMER_FALSE;
    }

    struct timer_s_internal *p = (struct timer_s_internal *)this;
    pthread_rwlock_wrlock(&p->lock);

    if(atomic_read(&p->init_flag)  ==  0) {
        fprintf(stderr, "TIMER Manager has not been init yet\n");
        pthread_rwlock_unlock(&p->lock);
        return 0;
    }

    if(p->enable_flag == 0) {
        fprintf(stderr, "TIMER Manager has been disable\n");
        pthread_rwlock_unlock(&p->lock);
        return 0;
    }

    if((unsigned int)atomic_read(&p->cur_timer_num) >= p->timer_max_num) {
        fprintf(stderr, "ACHIEVE TIMER MAX NUMBER\n");
        pthread_rwlock_unlock(&p->lock);
        return 0;
    }

    if(check_timer(p, timer) == TIMER_FALSE) {
        fprintf(stderr, "timer is illegal \n");
        pthread_rwlock_unlock(&p->lock);
        return 0;
    }

    int index = (p->cur_slot + timer->interval / p->time_slot) % p->slot_num;
    struct timer_internal *temp,  *t = malloc(sizeof(struct timer_internal));
    struct list_head *header = &p->data[index].head;
    struct timer_node *tmp = container_of(header, struct timer_node , head);

    if(t  ==  NULL) {
        perror("malloc failed");
        pthread_rwlock_unlock(&p->lock);
        return 0;
    } else {
        *(struct timer *)t = *timer;
        t->param = malloc(t->param_len);

        if(t->param == NULL) {
            perror("malloc failed\n");
            pthread_rwlock_unlock(&p->lock);
            free(t);
            return 0;
        }

        memcpy(t->param, timer->param, t->param_len);
        t->id = timer_id_pop(p);
        t->round = timer->interval / (p->time_slot * p->slot_num);
        INIT_LIST_HEAD(&t->list);
    }

    ///链表中是否有定时器 分开处理
    if(header->next != header) {
        temp = container_of(header->next, struct timer_internal, list);

        if(temp->interval > t->interval) {
            list_add(&t->list, header);
        } else {
            list_add_tail(&t->list, header);
        }
    } else {
        list_add(&t->list, header);
    }

    p->rb_root = rb_insert(t->id, (void *)t, p->rb_root);
    atomic_inc(&tmp->timer_cnt);
    atomic_inc(&p->cur_timer_num);
    pthread_rwlock_unlock(&p->lock);
    return t->id;
}


/**
 * @brief	del_and_add
 *
 * 专门针对repeat timer的一个add重定义版本
 *
 * @param	this		定时器内部管理对象指针
 * @param	timer		定时器内部结构指针
 * @param	node	时间轮节点
 *
 * @return
 */
static TIMER_BOOL del_and_add(struct timer_s_internal *this, struct timer_internal *timer, struct timer_node *node)
{
    if(this  ==  NULL) {
        return TIMER_FALSE;
    }

    pthread_rwlock_wrlock(&this->lock);

    if(atomic_read(&this->init_flag)  ==  0) {
        fprintf(stderr, "TIMER Manager has not been init yet\n");
        pthread_rwlock_unlock(&this->lock);
        return TIMER_FALSE;
    }

    if((unsigned int)atomic_read(&this->cur_timer_num)  >=  this->timer_max_num) {
        fprintf(stderr, "ACHIEVE TIMER MAX NUMBER\n");
        pthread_rwlock_unlock(&this->lock);
        return TIMER_FALSE;
    }

    list_del(&timer->list);
    atomic_dec(&node->timer_cnt);
    int index = (this->cur_slot + timer->interval / this->time_slot) % this->slot_num;
    struct timer_internal *temp;
    struct timer_node *tmp;
    struct list_head *header = &(this->data[index]).head;
    timer->round = timer->interval / (this->time_slot * this->slot_num);

    ///链表中是否有定时器 分开处理
    if(header->next != header) {
        temp = container_of(header->next, struct timer_internal, list);

        if(temp->interval > timer->interval) {
            list_add(&timer->list, header);
        } else {
            list_add_tail(&timer->list, header);
        }
    } else {
        list_add(&timer->list, header);
    }

    tmp = container_of(header, struct timer_node ,  head);
    atomic_inc(&tmp->timer_cnt);
    pthread_rwlock_unlock(&this->lock);
    return TIMER_TRUE;
}
/**
 * @brief	del
 *
 * 删除定时器
 *
 * @param	this		定时器管理对象
 * @param	id			定时器标志
 *
 * @note
 *		id <= 0表示删除所有定时器, 定时器标志也是从1开始的
 *
 * @return		库布尔值
 */
static TIMER_BOOL ti_del(TIMER_MANAGER *this, timer_id id)
{
    if(this  ==  NULL) {
        return TIMER_FALSE;
    }

    struct timer_s_internal *p = (struct timer_s_internal *)this;
    rb_node_t *node;
    unsigned int cnt = 0, stat = 0;
    struct timer_internal *temp;
    struct list_head *header;
    pthread_rwlock_wrlock(&p->lock);

    if(id <= 0) {
        for(; cnt < p->slot_num; ++cnt) {
            if(atomic_read(&p->data[cnt].timer_cnt) != 0) {
                stat = 1;
                header = &p->data[cnt].head;

                while(!list_empty(header)) {
                    list_del(header->next);
                    temp = container_of(header->next, struct timer_internal, list);

                    if(temp->param_len  !=  0) {
                        free(temp->param);
                    }

                    free(temp);
                }
            }
        }

        atomic_set(&p->cur_timer_num , 0);
        pthread_rwlock_unlock(&p->lock);

        if(stat == 1) {
            return TIMER_TRUE;
        } else {
            return TIMER_FALSE;
        }
    } else {
        /*
                        for( ; cnt < p->slot_num; ++cnt ) {
                                if( atomic_read( &p->data[cnt].timer_cnt ) != 0 ) {
                                        header = &p->data[cnt].head;
                                        list_for_each_entry( temp, header, list ) {
                                                if( temp->id  ==  id ) {
                                                        list_del( &temp->list );

                                                        if( temp->param_len != 0 )
                                                                free( temp->param );

                                                        free( temp );
                                                        timer_id_push( p, id );
                                                        atomic_dec( &p->cur_timer_num );
                                                        pthread_rwlock_unlock( &p->lock );
                                                        return TIMER_TRUE;
                                                }
                                        }
                                }
                        }
        */
        node = rb_search(id, p->rb_root);

        if(node != NULL) {
            temp = (struct timer_internal *)(node->data);
            list_del(&temp->list);

            if(temp->param_len != 0) {
                free(temp->param);
            }

            free(temp);
            timer_id_push(p, id);
            atomic_dec(&p->cur_timer_num);
            p->rb_root = rb_erase(id, p->rb_root);
            pthread_rwlock_unlock(&p->lock);
            return TIMER_TRUE;
        } else {
            pthread_rwlock_unlock(&p->lock);
            return TIMER_FALSE;
        }
    }
}


/**
 * @brief	stop
 *
 * 停止定时器
 *
 * @param	this	定时器管理对象指针
 */
static void ti_stop(TIMER_MANAGER *this)
{
    if(this  ==  NULL) {
        return;
    }

    struct timer_s_internal *p = (struct timer_s_internal *)this;
    ///如果定时器并没有开始，直接结束
    pthread_rwlock_wrlock(&p->lock);

    if(p->start_flag  == 0) {
        pthread_rwlock_unlock(&p->lock);
        return ;
    }

    //atomic_set( &p->start_flag, 0 );
    p->start_flag = 0;
    sleep(1);

    if(p->pid != 0) {
        pthread_kill(p->pid, TIMER_STOP_SIGNAL);
        p->pid = 0;
    }

    pthread_rwlock_unlock(&p->lock);
}


/**
 * @brief	start
 *
 * 以两种方式来开启定时器
 *
 * @param	this		定时器管理对象指针
 * @param	type		开启方式
 */
static void ti_start(TIMER_MANAGER *this, timer_start_type type)
{
    if(this  ==  NULL) {
        return;
    }

    struct timer_s_internal *p = (struct timer_s_internal *)this;
    pthread_rwlock_wrlock(&p->lock);

    if(p->start_flag  == 1) {
        pthread_rwlock_unlock(&p->lock);
        return;
    } else {
        p->start_flag = 1 ;
    }

    pthread_attr_t attr;
    pthread_attr_init(&attr);

    switch(type) {
        case TIMER_START_UNBLOCK:
            pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);

            if(pthread_create((pthread_t *)(&p->pid), &attr, entry, (void *)p) != 0) {
                p->start_flag = 0 ;
                p->pid = 0;
                pthread_rwlock_unlock(&p->lock);
                return;
            }

            pthread_rwlock_unlock(&p->lock);
            break;
        case TIMER_START_BLOCK:

            if(pthread_create((pthread_t *)(&p->pid), NULL, entry, (void *)p) != 0) {
                p->start_flag = 0 ;
                p->pid = 0;
                pthread_rwlock_unlock(&p->lock);
                return;
            }

            pthread_rwlock_unlock(&p->lock);

            if(pthread_join(p->pid, NULL) != 0) {
                perror("block failed");
            }

            break;
        default:
            break;
    }

    pthread_rwlock_unlock(&p->lock);
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
static void *entry(void *p)
{
    if(p  ==  NULL) {
        return NULL;
    }

    struct timer_s_internal *this = (struct timer_s_internal *)p;
    pthread_t id;
    pthread_attr_t attr;
    sigset_t sigmask;
    /* sigemptyset(&sigmask);
    sigaddset(&sigmask, TIMER_STOP_SIGNAL);
    pthread_sigmask(SIG_UNBLOCK, &sigmask, NULL);  */
    sigfillset(&sigmask);
    sigdelset(&sigmask, TIMER_STOP_SIGNAL);
    pthread_sigmask(SIG_SETMASK, &sigmask, NULL);
    signal(TIMER_STOP_SIGNAL, catch_signal);
    struct itimerspec new_value;
    struct timespec increment;
    /* int timerfd; */
    struct timer_internal *temp;
    struct timer_node *node;
    struct list_head *header, *tmp;
    //int64_t diff;
    uint64_t exp;
    int replay = 0;
    increment.tv_sec = this->time_slot / 1000;
    increment.tv_nsec = this->time_slot % 1000 * 1000000;
    new_value.it_value.tv_sec = increment.tv_sec;
    new_value.it_value.tv_nsec = increment.tv_nsec;
    new_value.it_interval.tv_sec = increment.tv_sec;
    new_value.it_interval.tv_nsec = increment.tv_nsec;

    if(timerfd_settime(this->timerfd, 0, &new_value, NULL) == -1) {
        perror("[timer exit normally] - timer_set failed");
        goto END;
    }

    while(this->start_flag) {
        //gettimeofday(&now, NULL);
        header = &((this->data[this->cur_slot]).head);
        node = &this->data[this->cur_slot];
        tmp = header;

        /* list_for_each_entry(temp, &(this->data[this->cur_slot]).head, list) { */
        while(!list_is_last(tmp, header)) {
            temp = container_of(tmp->next, struct timer_internal, list);

            if(temp->round == 0) {
                switch(temp->run_type) {
                    case SIGNAL:
                        kill(getpid(), SIGALRM);
                        break;
                    case THREAD:
                        pthread_attr_init(&attr);
                        pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);

                        if(pthread_create(&id, &attr, temp->cb, (void *)temp->param)  ==  -1) {
                            perror("execute expiry func failed");
                        }

                        pthread_attr_destroy(&attr);
                        break;
                    case DIRECT:
                        temp->cb(temp->param);
                    default:
                        break;
                }

                switch(temp->type) {
                    case REPEAT:
                        del_and_add(this, temp, node);
                        /*
                        list_del( tmp->next );
                        this->rb_root = rb_erase( temp->id, this->rb_root );
                        atomic_dec( &node->timer_cnt );
                        atomic_dec( &this->cur_timer_num );
                        readd( this, temp );
                        */
                        break;
                    case SINGLE_SHOT:
                    default:
                        pthread_rwlock_rdlock(&this->lock);			////////////////
                        list_del(tmp->next);
                        this->rb_root = rb_erase(temp->id, this->rb_root);

                        if(temp->param_len != 0) {
                            free(temp->param);
                        }

                        free(temp);
                        timer_id_push(this, temp->id);
                        atomic_dec(&node->timer_cnt);
                        pthread_rwlock_unlock(&this->lock);
                        break;
                }
            } else {
                --temp->round;
            }

            tmp = tmp->next;
        }

        if(read(this->timerfd, &exp, sizeof(uint64_t)) != sizeof(uint64_t)) {
            perror("[timer exit abnormally] - read failed");
            goto END;
        } else {
            if(exp > 1) {
                fprintf(stderr, "maintain timer_list expire one time_slot\n");

                if(++replay == 5) {
                    goto END;
                }
            }
        }

        if(this->cur_slot == this->slot_num - 1) {
            this->cur_slot = 0;
        } else {
            ++this->cur_slot;
        }
    }

    fprintf(stderr, "timer exit normally\n");
    //atomic_set( &this->start_flag, 0 );
END:
    this->start_flag = 0;
    this->pid = 0;
    return NULL;
}

static void ti_close(TIMER_MANAGER *this)
{
    if(this  ==  NULL) {
        return;
    }

    struct timer_s_internal *p = (struct timer_s_internal *)this;
    pthread_rwlock_wrlock(&p->lock);

    ///还没有初始化，直接返回
    if(atomic_read(&p->init_flag)  ==  0) {
        return;
    }

    //p->stop( this );
    if(p->start_flag  != 0) {
        p->start_flag = 0;
        sleep(1);

        if(p->pid != 0) {
            pthread_kill(p->pid, TIMER_STOP_SIGNAL);
            p->pid = 0;
        }
    }

    unsigned int cnt = 0;
    struct list_head *header,  *tmp;
    struct timer_internal *temp;

    for(; cnt < p->slot_num; ++cnt) {
        if(atomic_read(&p->data[cnt].timer_cnt) != 0) {
            header = &p->data[cnt].head;

            while(!list_empty(header)) {
                tmp = header->next;
                list_del(header->next);
                temp = container_of(tmp, struct timer_internal, list);

                if(temp->param_len != 0) {
                    free(temp->param);
                }

                free(temp);
            }
        }
    }

    p->time_slot = 0;
    p->slot_num = 0;
    p->timer_max_num = 0;

    if(p->data) {
        free(p->data);
    }

    if(p->timer_fd_bitmap) {
        free(p->timer_fd_bitmap);
    }

    if(p->timerfd > 2) {
        close(p->timerfd);
    }

    atomic_set(&p->init_flag,  0);
    pthread_rwlock_unlock(&p->lock);
}


/* ==  ==  ==  ==  ==  ==  ==  ==  ==  ==  HELPER FUNC ==  ==  ==  ==  ==  ==  ==  ==  ==  ==  ==  == = */
/**
 * @brief	find_min_id
 *
 * 从位图中找寻最小的未用id
 *
 * @param	tmp
 * @param	len
 *
 * @note
 *	因为timer_id是从1开始的，因此从位图获取的值还要加1
 *
 * @return
 */
timer_id find_min_id(unsigned char *tmp, int len)
{
    int bytes = bitmap_bytes(len);
    int i = 0, cnt = 0;
    unsigned char base = 0x80;

    while(i < bytes) {
        if(tmp[i]  ==  255) {
            ++i;
            continue;
        } else {
            while(1) {
                if((tmp[i] & base) == 0) {
                    return i * 8 + cnt + 1;
                } else {
                    base = base >> 1;
                    ++cnt;
                }
            }
        }
    }

    ///表示位图已经被占完
    return 0;
}


/**
 * @brief	timer_id_pop
 *
 * 从定时器管理单元内部取出一个最小未用的id
 *
 * @param	this
 *
 * @return
 */
static inline timer_id timer_id_pop(struct timer_s_internal *this)
{
    ///因为调用这些函数时都是在加了锁的环境中，因此这些函数本身就不进行加锁了
    timer_id temp = this->timer_fd_min_free;
    set_bit(this->timer_fd_bitmap, temp);
    this->timer_fd_min_free = find_min_id(this->timer_fd_bitmap, this->timer_max_num);
    return temp;
}

/**
 * @brief	timer_id_push
 *
 * 将已用的id重新放回定时器管理单元结构中
 *
 * @param	this
 * @param	id
 */
static inline void timer_id_push(struct timer_s_internal *this, timer_id id)
{
    unset_bit(this->timer_fd_bitmap, id);

    if(this->timer_fd_min_free > id) {
        this->timer_fd_min_free = id;
    }
}

static void ti_enable(TIMER_MANAGER *this)
{
    if(this  ==  NULL) {
        return;
    }

    struct timer_s_internal *p = (struct timer_s_internal *)this;
    pthread_rwlock_rdlock(&p->lock);
    p->enable_flag = 1;
    pthread_rwlock_unlock(&p->lock);
}

static void ti_disable(TIMER_MANAGER *this)
{
    if(this  ==  NULL) {
        return;
    }

    struct timer_s_internal *p = (struct timer_s_internal *)this;
    pthread_rwlock_rdlock(&p->lock);
    p->enable_flag = 0;
    pthread_rwlock_unlock(&p->lock);
}

static void catch_signal(int i)
{
    switch(i) {
        case 62:
            pthread_exit(0);
            break;
        default:
            break;
    }
}

static inline TIMER_BOOL check_timer(struct timer_s_internal *this, struct timer *conf)
{
    if(conf->interval < this->time_slot) {
        fprintf(stderr, "timer precision can not been achieve\n");
        return TIMER_FALSE;
    }

    if(conf->cb  ==  NULL) {
        fprintf(stderr, "timer's callback func can not be NULL\n");
        return TIMER_FALSE;
    }

    if((conf->param  ==  NULL  && conf->param_len  != 0) || (conf->param != NULL  && conf->param_len  == 0)) {
        fprintf(stderr, "timer's param and param_len is not conform\n");
        return TIMER_FALSE;
    }

    return TIMER_TRUE;
}
