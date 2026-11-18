/** @file spinlock.h @brief 以独立 pthread 互斥对象替代 SDK 跨核短锁，仅用于主机测试。 */
#ifndef PRODUCT_UI_HOST_SPINLOCK_H
#define PRODUCT_UI_HOST_SPINLOCK_H
#include <pthread.h>
typedef pthread_mutex_t spinlock_t;
#define DEFINE_SPINLOCK(name) spinlock_t name = PTHREAD_MUTEX_INITIALIZER
/**
 * @brief 锁住传入的 pthread 互斥对象，替代当前 SDK 短锁。
 * @param[in,out] lock 调用方持有的已初始化主机互斥对象。
 */
static inline void spin_lock(spinlock_t *lock) { (void)pthread_mutex_lock(lock); }
/**
 * @brief 释放调用线程已取得的主机互斥对象。
 * @param[in,out] lock 与 spin_lock 配对的同一主机互斥对象。
 */
static inline void spin_unlock(spinlock_t *lock) { (void)pthread_mutex_unlock(lock); }
#endif
