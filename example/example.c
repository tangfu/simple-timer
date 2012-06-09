/**
 * @file example.c
 * @brief example
 *
 * 使用timer库的例子程序
 *
 * @author tangfu - abctangfuqiang2008@163.com
 * @version	1.0
 * @date 2011-05-23
 */


#include "timer.h"
#include <stdio.h>
#include <pthread.h>
#include <unistd.h>

void *timer1_task( void *p )
{
        printf( "this is task1 : %d\n", ( int )p );
        return NULL;
}
void *timer2_task( void *p )
{
        printf( "this is task2 : %d\n", ( int )p );
        return NULL;
}

void *test( void *p )
{
        TIMER_MANAGER *this = ( TIMER_MANAGER * )p;
        sleep( 7 );
        this->stop( this );
        return NULL;
}

int main()
{
        pthread_t id;
        struct timer_manager_conf conf = {1000, 30, 100};
        struct timer timer1 = {SINGLE_SHOT, DIRECT, 1000, timer1_task, NULL,  0},  timer2 = {REPEAT, DIRECT, 5000,  timer2_task, NULL, 0};
        struct timer timer3 = {SINGLE_SHOT, DIRECT, 2, timer1_task, NULL,  0},  timer4 = {REPEAT, DIRECT, 3,  timer2_task, NULL, 0};
        TIMER_MANAGER *p = create_timer_manager();
        MH_TIMER_MANAGER *p1 = create_mh_timer_manager();

        if( p  ==  NULL ) {
                fprintf( stderr, "create timer manager failed\n" );
                return -1;
        }

        if( p1  ==  NULL ) {
                fprintf( stderr, "create mh timer manager failed\n" );
                return -1;
        }

        p->init( p, &conf );
        printf( "add timer1 : %u\n", p->add( p, &timer1 ) );
        printf( "add timer2 : %u\n", p->add( p, &timer2 ) );
        p->start( p, TIMER_START_UNBLOCK );
        p1->init( p1, 30 );
        printf( "add mh timer3 : %u\n", p1->push( p1, &timer3 ) );
        printf( "add mh timer4 : %u\n", p1->push( p1, &timer4 ) );
        p1->start( p1, TIMER_START_UNBLOCK );
        pthread_create( &id, NULL, test, ( void * )p );
        pthread_join( id, NULL );
        //sleep( 7 );
        p->close( p );
        p1->close( p1 );
        return 0;
}

/**
 * @example example.c
 *
 * 使用timer库完成定时
 *
 */
