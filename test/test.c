/*
 * =====================================================================================
 *
 *       Filename:  test.c
 *
 *    Description:  定时器库的单元测试
 *
 *        Version:  1.0
 *        Created:  2011年05月23日 15时17分19秒
 *       Revision:  none
 *       Compiler:  gcc
 *
 *         Author:  YOUR NAME (tangfu),
 *        Company:  UESTC
 *
 * =====================================================================================
 */
#include <stdio.h>
#include "timer.h"
#include <stdarg.h>
#include <setjmp.h>
#include <cmockery.h>

TIMER_MANAGER *p;
MH_TIMER_MANAGER *p1;

void *timer_task( void *p )
{
        fprintf( stderr, "execute timer_task : %s\n", ( char * )p );
        return NULL;
}


void test_init( void **state )
{
        struct timer_manager_conf conf = {1000, 30, 100},
               conf_1 = {1000, 1, 4},
               conf_2 = {1000, 5, 0};
        assert_int_equal( p->init( p, &conf_1 ), TIMER_FALSE );
        assert_int_equal( p->init( p, &conf_2 ), TIMER_FALSE );
        assert_int_equal( p->init( p, &conf ), TIMER_TRUE );
        assert_int_equal( p->init( p, &conf ), TIMER_FALSE );
        //minheap_timer
        assert_int_equal( p1->init( p1, -10 ), TIMER_TRUE );
        p1->close( p1 );
        assert_int_equal( p1->init( p1, 0 ), TIMER_TRUE );
        p1->close( p1 );
        assert_int_equal( p1->init( p1, 100 ), TIMER_TRUE );
}

void test_add_and_del( void **state )
{
        struct timer t = {REPEAT, DIRECT, 1000, timer_task, "timer", sizeof( "timer" )},
               t1 = {SINGLE_SHOT, DIRECT, 1000, NULL, NULL, 0},
               t2 = {SINGLE_SHOT, DIRECT, 100, timer_task, "timer2", sizeof( "timer2" )},
               t3 = {SINGLE_SHOT, DIRECT, 100, timer_task, NULL, 3},
               t4 = {SINGLE_SHOT, DIRECT, 100, timer_task, "timer2",  0};
        struct timer_manager_conf conf_f = {1000, 30, 100};
        p->close( p );
        assert_int_equal( p->add( p, &t ), 0 );
        p->init( p, &conf_f );
        assert_int_equal( p->add( p, &t1 ), 0 );
        assert_int_equal( p->add( p, &t2 ), 0 );
        assert_int_equal( p->add( p, &t3 ), 0 );
        assert_int_equal( p->add( p, &t4 ), 0 );
        assert_int_equal( p->del( p, 1 ), TIMER_FALSE );
        assert_int_equal( p->add( p, &t ), 1 );
        assert_int_equal( p->del( p, 1 ), TIMER_TRUE );
        assert_int_equal( p->add( p, &t ), 1 );
        //printf( "id=%d\n", p->add( p, &t ) );
        assert_int_equal( p->add( p, &t ), 2 );
}

int main()
{
        p = create_timer_manager();
        p1 = create_mh_timer_manager();

        if( p  == NULL ) {
                fprintf( stderr, "create timer manager failed \n" );
                return -1;
        }

        if( p1 == NULL ) {
                fprintf( stderr, "create minheap_timer manager failed \n" );
                return -1;
        }

        UnitTest TESTS[] = {
                unit_test( test_init ),
                unit_test( test_add_and_del )
        };
        return run_tests( TESTS );
}

