/**
 * @file atomic.h
 * @brief	用户空间的原子操作
 *
 *	1.由于gcc 4.1之后的版本提供了原子操作的宏，因此以gcc版本为判断依据，之前的使用自定义的原子操作，之后的使用gcc内建的原子操作\n
 *	2.gcc内建版本的原子操作可以操作1，2，4，8个字节的结构，因此更加灵活
 *
 * @author	unknown
 * @version unknown
 * @date 2010-03-26
 */


#ifndef __ATOMIC_H__
#define __ATOMIC_H__

#include <stdint.h>


typedef struct {
    volatile int32_t counter;
} atomic32_t;

typedef struct {
    volatile int8_t counter;
} atomic8_t;

typedef struct {
    volatile int16_t counter;
} atomic16_t;

typedef struct {
    volatile int64_t counter;
} atomic64_t;

typedef struct {
    volatile int counter;
} atomic_t;

#define atomic_read(v)      ((v)->counter)
#define atomic_set(v, i)    (((v)->counter) = (i))


#if __GNUC__ < 4 || (__GNUC__ == 4 && __GNUC_MINOR__ < 1)
/* Technically wrong, but this avoids compilation errors on some gcc
   versions. */
#define ADDR "=m" (*(volatile long *) addr)
//#else
//#define ADDR "+m" (*(volatile long *) addr)
//#endif

#ifdef CONFIG_SMP
#define LOCK_PREFIX "lock; "
#else				/* ! CONFIG_SMP */
#define LOCK_PREFIX ""
#endif


static inline void atomic_inc(atomic_t *v)
{
    asm volatile(LOCK_PREFIX "incl %0":"=m"(v->counter)
                 :"m"(v->counter));
}

static inline void atomic_dec(atomic_t *v)
{
    asm volatile(LOCK_PREFIX "decl %0":"=m"(v->counter)
                 :"m"(v->counter));
}

static inline void set_bit(int nr, volatile void *addr)
{
    asm volatile(LOCK_PREFIX "bts %1,%0":ADDR:"Ir"(nr):"memory");
}

static inline int test_and_set_bit(int nr, volatile void *addr)
{
    int oldbit;
    asm volatile(LOCK_PREFIX "bts %2,%1nt"
                 "sbb %0,%0":"=r"(oldbit), ADDR:"Ir"(nr):"memory");
    return oldbit;
}

static inline void clear_bit(int nr, volatile void *addr)
{
    asm volatile(LOCK_PREFIX "btr %1,%0":ADDR:"Ir"(nr));
}

static inline int test_and_clear_bit(int nr, volatile void *addr)
{
    int oldbit;
    asm volatile(LOCK_PREFIX "btr %2,%1nt"
                 "sbb %0,%0":"=r"(oldbit), ADDR:"Ir"(nr):"memory");
    return oldbit;
}

#else

/**
 * @brief	atomic_inc	自增操作
 *
 * @param	x			原子结构
 */
#define atomic_inc(x) ((void) __sync_fetch_and_add (&(x)->counter, 1))

/**
 * @brief	atomic_dec	自减操作
 *
 * @param	x			原子结构
 */
#define atomic_dec(x) ((void) __sync_fetch_and_sub (&(x)->counter, 1))

/**
 *
 * @brief	atomic_dec_and_test		先自减然后测试值是否为1
 *
 * @param	x			原子结构
 */
#define atomic_dec_and_test(x) (__sync_fetch_and_add (&(x)->counter, -1) == 1)

/**
 * @brief	atomic_add		增加值
 *
 * @param	x				原子结构
 * @param	v				待增加的值
 */
#define atomic_add(x, v) ((void) __sync_add_and_fetch(&(x)->counter, (v)))

/**
 * @brief	atomic_sub		缩减值
 *
 * @param	x				原子结构
 * @param	v				待减少的值
 */
#define atomic_sub(x, v) ((void) __sync_sub_and_fetch(&(x)->counter, (v)))

/**
 * @brief	atomic_cmpxchg		先比较，如果相等就交换值
 *
 * @param	x					原子结构
 * @param	oldv				比较的值
 * @param	newv				交换的值
 *
 * @return 如果比较相等且交换成功返回0，否则-1
 */
#define atomic_cmpxchg(x, oldv, newv) __sync_val_compare_and_swap (&(x)->atomic, oldv, newv)

/**
 * @brief	atomic_init		初始化原子结构的值
 *
 * @param	x				原子结构
 * @param	v				初始化的值
 */
#define atomic_init(a,v) atomic_set(a,v)

#endif


#endif	/* __ATOMIC_H__ */
