/*
c11threads

Authors:
John Tsiombikas <nuclear@member.fsf.org>
Oliver Old <oliver.old@outlook.com>

I place this piece of code in the public domain. Feel free to use as you see
fit. I'd appreciate it if you keep my name at the top of the code somewhere, but
whatever.

Main project site: https://github.com/jtsiomb/c11threads
*/

#ifndef C11THREADS_H_
#define C11THREADS_H_

#if !defined(_WIN32) || defined(C11THREADS_PTHREAD_WIN32)
#include <stdint.h>
#include <errno.h>
#include <pthread.h>
#include <sched.h>	/* for sched_yield */
#include <sys/time.h>
#endif
#include <time.h>

#ifndef thread_local
#ifdef __STDC_VERSION__
#if __STDC_VERSION__ <= 201710L /* The thread_local macro will be removed in C23. */
#define C11THREADS_DEFINE_THREAD_LOCAL
#endif
#elif defined(__cplusplus)
#if __cplusplus < 201103L /* C++11 has its own thread_local keyword. */
#define C11THREADS_DEFINE_THREAD_LOCAL
#endif
#endif
#endif

#ifdef C11THREADS_DEFINE_THREAD_LOCAL
#ifdef _WIN32
#define thread_local		__declspec(thread)
#else
#define thread_local		_Thread_local
#endif
#endif

#if defined(_WIN32) && !defined(C11THREADS_PTHREAD_WIN32)
#define ONCE_FLAG_INIT		{0}
#define TSS_DTOR_ITERATIONS	4
#else
#define ONCE_FLAG_INIT		PTHREAD_ONCE_INIT
#define TSS_DTOR_ITERATIONS	PTHREAD_DESTRUCTOR_ITERATIONS
#endif

#if defined(_WIN32) && !defined(C11THREADS_PTHREAD_WIN32)
#define C11THREADS_NO_TIMED_MUTEX
#ifdef _MSC_VER
#if _MSC_VER < 1900
#define C11THREADS_NO_TIMESPEC_GET
#endif
#else
#define C11THREADS_NO_TIMESPEC_GET
#endif
#elif defined(__APPLE__)
/* Darwin doesn't implement timed mutexes currently */
#define C11THREADS_NO_TIMED_MUTEX
#include <Availability.h>
#ifndef __MAC_10_15
#define C11THREADS_NO_TIMESPEC_GET
#endif
#elif __STDC_VERSION__ < 201112L
#define C11THREADS_NO_TIMESPEC_GET
#endif

#ifdef C11THREADS_NO_TIMED_MUTEX
#define C11THREADS_TIMEDLOCK_POLL_INTERVAL 5000000	/* 5 ms */
#endif

/* types */
#if defined(_WIN32) && !defined(C11THREADS_PTHREAD_WIN32)
typedef unsigned long thrd_t;
#pragma pack(push, 8)
typedef struct {
	void *debug_info;
	long lock_count;
	long recursion_count;
	void *owning_thread;
	void *lock_semaphore;
	void *spin_count;
} mtx_t;
#pragma pack(pop)
#ifndef C11THREADS_SUPPORT_WINNT_OLDER_THAN_VISTA
typedef struct {
	void *ptr;
} cnd_t;
#endif
typedef unsigned long tss_t;
typedef struct {
	void *ptr;
} once_flag;
#else
typedef pthread_t thrd_t;
typedef pthread_mutex_t mtx_t;
typedef pthread_cond_t cnd_t;
typedef pthread_key_t tss_t;
typedef pthread_once_t once_flag;
#endif

typedef int (*thrd_start_t)(void*);
typedef void (*tss_dtor_t)(void*);

enum {
	mtx_plain		= 0,
	mtx_recursive	= 1,
	mtx_timed		= 2,
};

enum {
	thrd_success,
	thrd_timedout,
	thrd_busy,
	thrd_error,
	thrd_nomem
};

/* Library functions. */

/* Win32: Initialize library. */
static inline void c11threads_init(void);
/* Win32: Destroy library. */
static inline void c11threads_destroy(void);

/* Thread functions. */

/* Win32: Register foreign thread in c11threads to allow for proper thrd_join(). Memory leak if neither joined nor detached. */
static inline int c11threads_thrd_self_register(void);
static inline int thrd_create(thrd_t *thr, thrd_start_t func, void *arg);
/* Win32: Threads not created with thrd_create() need to call this to clean up TSS. */
static inline void thrd_exit(int res);
static inline int thrd_join(thrd_t thr, int *res);
static inline int thrd_detach(thrd_t thr);
static inline thrd_t thrd_current(void);
static inline int thrd_equal(thrd_t a, thrd_t b);
static inline int thrd_sleep(const struct timespec *ts_in, struct timespec *rem_out);
static inline void thrd_yield(void);

/* Mutex functions. */

static inline int mtx_init(mtx_t *mtx, int type);
static inline void mtx_destroy(mtx_t *mtx);
static inline int mtx_lock(mtx_t *mtx);
static inline int mtx_trylock(mtx_t *mtx);
static inline int mtx_timedlock(mtx_t *mtx, const struct timespec *ts);
static inline int mtx_unlock(mtx_t *mtx);

#if !defined(_WIN32) || defined(C11THREADS_PTHREAD_WIN32) || !defined(C11THREADS_SUPPORT_WINNT_OLDER_THAN_VISTA)
/* Condition variable functions. */

static inline int cnd_init(cnd_t *cond);
static inline void cnd_destroy(cnd_t *cond);
static inline int cnd_signal(cnd_t *cond);
static inline int cnd_broadcast(cnd_t *cond);
static inline int cnd_wait(cnd_t *cond, mtx_t *mtx);
static inline int cnd_timedwait(cnd_t *cond, mtx_t *mtx, const struct timespec *ts);
#endif

/* Thread-specific storage functions. */

static inline int tss_create(tss_t *key, tss_dtor_t dtor);
static inline void tss_delete(tss_t key);
static inline int tss_set(tss_t key, void *val);
static inline void *tss_get(tss_t key);

#if !defined(_WIN32) || defined(C11THREADS_PTHREAD_WIN32) || !defined(C11THREADS_SUPPORT_WINNT_OLDER_THAN_VISTA)
/* One-time callable function. */

static inline void call_once(once_flag *flag, void (*func)(void));
#endif

#ifdef C11THREADS_NO_TIMESPEC_GET
#ifndef UTC_TIME
#define UTC_TIME 1
#endif
static inline int timespec_get(struct timespec *ts, int base);
#endif


/* ---- platform ---- */

#if defined(_WIN32) && !defined(C11THREADS_PTHREAD_WIN32)
void _c11threads_init_win32(void);
void _c11threads_destroy_win32(void);

int _c11threads_thrd_self_register_win32(void);
int _thrd_create_win32(thrd_t *thr, thrd_start_t func, void *arg);
void _thrd_exit_win32(int res);
int _thrd_join_win32(thrd_t thr, int *res);
int _thrd_detach_win32(thrd_t thr);
thrd_t _thrd_current_win32(void);
int _thrd_sleep_win32(const struct timespec *ts_in, struct timespec *rem_out);
void _thrd_yield_win32(void);

int _mtx_init_win32(mtx_t *mtx, int type);
void _mtx_destroy_win32(mtx_t *mtx);
int _mtx_lock_win32(mtx_t *mtx);
int _mtx_trylock_win32(mtx_t *mtx);
int _mtx_timedlock_win32(mtx_t *mtx, const struct timespec *ts);
int _mtx_unlock_win32(mtx_t *mtx);

#if !defined(_WIN32) || defined(C11THREADS_PTHREAD_WIN32) || !defined(C11THREADS_SUPPORT_WINNT_OLDER_THAN_VISTA)
int _cnd_init_win32(cnd_t *cond);
int _cnd_signal_win32(cnd_t *cond);
int _cnd_broadcast_win32(cnd_t *cond);
int _cnd_wait_win32(cnd_t *cond, mtx_t *mtx);
int _cnd_timedwait_win32(cnd_t *cond, mtx_t *mtx, const struct timespec *ts);
#endif

int _tss_create_win32(tss_t *key, tss_dtor_t dtor);
void _tss_delete_win32(tss_t key);
int _tss_set_win32(tss_t key, void *val);
void *_tss_get_win32(tss_t key);

#if !defined(_WIN32) || defined(C11THREADS_PTHREAD_WIN32) || !defined(C11THREADS_SUPPORT_WINNT_OLDER_THAN_VISTA)
void _call_once_win32(once_flag *flag, void (*func)(void));
#endif

#ifdef C11THREADS_NO_TIMESPEC_GET
int _timespec_get_win32(struct timespec *ts, int base);
#endif
#endif

/* ---- library ---- */

static inline void c11threads_init(void)
{
#if defined(_WIN32) && !defined(C11THREADS_PTHREAD_WIN32)
	_c11threads_init_win32();
#endif
}

static inline void c11threads_destroy(void)
{
#if defined(_WIN32) && !defined(C11THREADS_PTHREAD_WIN32)
	_c11threads_destroy_win32();
#endif
}

/* ---- thread management ---- */

static inline int c11threads_thrd_self_register(void)
{
#if defined(_WIN32) && !defined(C11THREADS_PTHREAD_WIN32)
	return _c11threads_thrd_self_register_win32();
#else
	return thrd_success;
#endif
}

static inline int thrd_create(thrd_t *thr, thrd_start_t func, void *arg)
{
#if defined(_WIN32) && !defined(C11THREADS_PTHREAD_WIN32)
	return _thrd_create_win32(thr, func, arg);
#else
	int res = pthread_create(thr, 0, (void*(*)(void*))func, arg);
	if(res == 0) {
		return thrd_success;
	}
	return res == ENOMEM ? thrd_nomem : thrd_error;
#endif
}

static inline void thrd_exit(int res)
{
#if defined(_WIN32) && !defined(C11THREADS_PTHREAD_WIN32)
	_thrd_exit_win32(res);
#else
	pthread_exit((void*)(intptr_t)res);
#endif
}

static inline int thrd_join(thrd_t thr, int *res)
{
#if defined(_WIN32) && !defined(C11THREADS_PTHREAD_WIN32)
	return _thrd_join_win32(thr, res);
#else
	void *retval;

	if(pthread_join(thr, &retval) != 0) {
		return thrd_error;
	}
	if(res) {
		*res = (int)(intptr_t)retval;
	}
	return thrd_success;
#endif
}

static inline int thrd_detach(thrd_t thr)
{
#if defined(_WIN32) && !defined(C11THREADS_PTHREAD_WIN32)
	return _thrd_detach_win32(thr);
#else
	return pthread_detach(thr) == 0 ? thrd_success : thrd_error;
#endif
}

static inline thrd_t thrd_current(void)
{
#if defined(_WIN32) && !defined(C11THREADS_PTHREAD_WIN32)
	return _thrd_current_win32();
#else
	return pthread_self();
#endif
}

static inline int thrd_equal(thrd_t a, thrd_t b)
{
#if defined(_WIN32) && !defined(C11THREADS_PTHREAD_WIN32)
	return a == b;
#else
	return pthread_equal(a, b);
#endif
}

static inline int thrd_sleep(const struct timespec *ts_in, struct timespec *rem_out)
{
#if defined(_WIN32) && !defined(C11THREADS_PTHREAD_WIN32)
	return _thrd_sleep_win32(ts_in, rem_out);
#else
	if(nanosleep(ts_in, rem_out) < 0) {
		if(errno == EINTR) return -1;
		return -2;
	}
	return 0;
#endif
}

static inline void thrd_yield(void)
{
#if defined(_WIN32) && !defined(C11THREADS_PTHREAD_WIN32)
	_thrd_yield_win32();
#else
	sched_yield();
#endif
}

/* ---- mutexes ---- */

static inline int mtx_init(mtx_t *mtx, int type)
{
#if defined(_WIN32) && !defined(C11THREADS_PTHREAD_WIN32)
	return _mtx_init_win32(mtx, type);
#else
	int res;
	pthread_mutexattr_t attr;

	pthread_mutexattr_init(&attr);

	if(type & mtx_timed) {
#ifdef PTHREAD_MUTEX_TIMED_NP
		pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_TIMED_NP);
#else
		pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_NORMAL);
#endif
	}
	if(type & mtx_recursive) {
		pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
	}

	res = pthread_mutex_init(mtx, &attr) == 0 ? thrd_success : thrd_error;
	pthread_mutexattr_destroy(&attr);
	return res;
#endif
}

static inline void mtx_destroy(mtx_t *mtx)
{
#if defined(_WIN32) && !defined(C11THREADS_PTHREAD_WIN32)
	_mtx_destroy_win32(mtx);
#else
	pthread_mutex_destroy(mtx);
#endif
}

static inline int mtx_lock(mtx_t *mtx)
{
#if defined(_WIN32) && !defined(C11THREADS_PTHREAD_WIN32)
	return _mtx_lock_win32(mtx);
#else
	int res = pthread_mutex_lock(mtx);
	if(res == EDEADLK) {
		return thrd_busy;
	}
	return res == 0 ? thrd_success : thrd_error;
#endif
}

static inline int mtx_trylock(mtx_t *mtx)
{
#if defined(_WIN32) && !defined(C11THREADS_PTHREAD_WIN32)
	return _mtx_trylock_win32(mtx);
#else
	int res = pthread_mutex_trylock(mtx);
	if(res == EBUSY) {
		return thrd_busy;
	}
	return res == 0 ? thrd_success : thrd_error;
#endif
}

static inline int mtx_timedlock(mtx_t *mtx, const struct timespec *ts)
{
#if defined(_WIN32) && !defined(C11THREADS_PTHREAD_WIN32)
	return _mtx_timedlock_win32(mtx, ts);
#else
	int res = 0;
#ifdef C11THREADS_NO_TIMED_MUTEX
	/* fake a timedlock by polling trylock in a loop and waiting for a bit */
	struct timeval now;
	struct timespec sleeptime;

	sleeptime.tv_sec = 0;
	sleeptime.tv_nsec = C11THREADS_TIMEDLOCK_POLL_INTERVAL;

	while((res = pthread_mutex_trylock(mtx)) == EBUSY) {
		gettimeofday(&now, NULL);

		if(now.tv_sec > ts->tv_sec || (now.tv_sec == ts->tv_sec &&
					(now.tv_usec * 1000) >= ts->tv_nsec)) {
			return thrd_timedout;
		}

		nanosleep(&sleeptime, NULL);
	}
#else
	if((res = pthread_mutex_timedlock(mtx, ts)) == ETIMEDOUT) {
		return thrd_timedout;
	}
#endif
	return res == 0 ? thrd_success : thrd_error;
#endif
}

static inline int mtx_unlock(mtx_t *mtx)
{
#if defined(_WIN32) && !defined(C11THREADS_PTHREAD_WIN32)
	return _mtx_unlock_win32(mtx);
#else
	return pthread_mutex_unlock(mtx) == 0 ? thrd_success : thrd_error;
#endif
}

/* ---- condition variables ---- */

#if !defined(_WIN32) || defined(C11THREADS_PTHREAD_WIN32) || !defined(C11THREADS_SUPPORT_WINNT_OLDER_THAN_VISTA)
static inline int cnd_init(cnd_t *cond)
{
#if defined(_WIN32) && !defined(C11THREADS_PTHREAD_WIN32)
	return _cnd_init_win32(cond);
#else
	return pthread_cond_init(cond, 0) == 0 ? thrd_success : thrd_error;
#endif
}

static inline void cnd_destroy(cnd_t *cond)
{
#if defined(_WIN32) && !defined(C11THREADS_PTHREAD_WIN32)
	(void)cond;
#else
	pthread_cond_destroy(cond);
#endif
}

static inline int cnd_signal(cnd_t *cond)
{
#if defined(_WIN32) && !defined(C11THREADS_PTHREAD_WIN32)
	return _cnd_signal_win32(cond);
#else
	return pthread_cond_signal(cond) == 0 ? thrd_success : thrd_error;
#endif
}

static inline int cnd_broadcast(cnd_t *cond)
{
#if defined(_WIN32) && !defined(C11THREADS_PTHREAD_WIN32)
	return _cnd_broadcast_win32(cond);
#else
	return pthread_cond_broadcast(cond) == 0 ? thrd_success : thrd_error;
#endif
}

static inline int cnd_wait(cnd_t *cond, mtx_t *mtx)
{
#if defined(_WIN32) && !defined(C11THREADS_PTHREAD_WIN32)
	return _cnd_wait_win32(cond, mtx);
#else
	return pthread_cond_wait(cond, mtx) == 0 ? thrd_success : thrd_error;
#endif
}

static inline int cnd_timedwait(cnd_t *cond, mtx_t *mtx, const struct timespec *ts)
{
#if defined(_WIN32) && !defined(C11THREADS_PTHREAD_WIN32)
	_cnd_timedwait_win32(cond, mtx, ts);
#else
	int res;

	if((res = pthread_cond_timedwait(cond, mtx, ts)) != 0) {
		return res == ETIMEDOUT ? thrd_timedout : thrd_error;
	}
	return thrd_success;
#endif
}
#endif

/* ---- thread-specific data ---- */

static inline int tss_create(tss_t *key, tss_dtor_t dtor)
{
#if defined(_WIN32) && !defined(C11THREADS_PTHREAD_WIN32)
	return _tss_create_win32(key, dtor);
#else
	return pthread_key_create(key, dtor) == 0 ? thrd_success : thrd_error;
#endif
}

static inline void tss_delete(tss_t key)
{
#if defined(_WIN32) && !defined(C11THREADS_PTHREAD_WIN32)
	_tss_delete_win32(key);
#else
	pthread_key_delete(key);
#endif
}

static inline int tss_set(tss_t key, void *val)
{
#if defined(_WIN32) && !defined(C11THREADS_PTHREAD_WIN32)
	return _tss_set_win32(key, val);
#else
	return pthread_setspecific(key, val) == 0 ? thrd_success : thrd_error;
#endif
}

static inline void *tss_get(tss_t key)
{
#if defined(_WIN32) && !defined(C11THREADS_PTHREAD_WIN32)
	return _tss_get_win32(key);
#else
	return pthread_getspecific(key);
#endif
}

/* ---- misc ---- */

#if !defined(_WIN32) || defined(C11THREADS_PTHREAD_WIN32) || !defined(C11THREADS_SUPPORT_WINNT_OLDER_THAN_VISTA)
static inline void call_once(once_flag *flag, void (*func)(void))
{
#if defined(_WIN32) && !defined(C11THREADS_PTHREAD_WIN32)
	_call_once_win32(flag, func);
#else
	pthread_once(flag, func);
#endif
}
#endif

#ifdef C11THREADS_NO_TIMESPEC_GET
static inline int timespec_get(struct timespec *ts, int base)
{
#if defined(_WIN32) && !defined(C11THREADS_PTHREAD_WIN32)
	return _timespec_get_win32(ts, base);
#else
	struct timeval tv;

	if(base != TIME_UTC) {
		return 0;
	}

	if(gettimeofday(&tv, 0) == -1) {
		return 0;
	}

	ts->tv_sec = tv.tv_sec;
	ts->tv_nsec = tv.tv_usec * 1000;

	return base;
#endif
}
#endif

#endif	/* C11THREADS_H_ */
