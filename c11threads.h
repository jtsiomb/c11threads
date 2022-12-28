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
#ifdef _MSC_VER
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

#define C11THREADS_TIMEDLOCK_POLL_INTERVAL	5000000	/* 5 ms */
#define C11THREADS_CALLONCE_POLL_INTERVAL	5000000	/* 5 ms */

#ifdef __cplusplus
extern "C" {
#endif

/* types */
#if defined(_WIN32) && !defined(C11THREADS_PTHREAD_WIN32)
typedef unsigned long thrd_t;
typedef struct {
	void *debug_info;
	long lock_count;
	long recursion_count;
	void *owning_thread;
	void *lock_semaphore;
	void *spin_count;
} mtx_t;
typedef struct {
	void *ptr;
} cnd_t;
typedef unsigned long tss_t;
typedef struct {
	void *ptr;
} once_flag;
struct _c11threads_timespec32_win32_t {
	long tv_sec;
	long tv_nsec;
};
struct _c11threads_timespec64_win32_t {
	long long tv_sec;
	long tv_nsec;
};
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

#if defined(_WIN32) && !defined(C11THREADS_PTHREAD_WIN32)
#define C11THREADS_MAYBE_INLINE
#else
#define C11THREADS_MAYBE_INLINE static inline
#endif

/* Library functions. */

#if defined(_WIN32) && !defined(C11THREADS_PTHREAD_WIN32)
/* Win32: Initialize library. */
void c11threads_init_win32(void);
/* Win32: Destroy library. */
void c11threads_destroy_win32(void);
#endif

/* Thread functions. */

#if defined(_WIN32) && !defined(C11THREADS_PTHREAD_WIN32)
/* Win32: Register current Win32 thread in c11threads to allow for proper thrd_join(). Memory leak if neither joined nor detached. */
int c11threads_thrd_self_register_win32(void);
/* Win32: Register other Win32 thread by ID in c11threads to allow for proper thrd_join(). Memory leak if neither joined nor detached. */
int c11threads_thrd_register_win32(thrd_t thr);
#endif
C11THREADS_MAYBE_INLINE int thrd_create(thrd_t *thr, thrd_start_t func, void *arg);
/* Win32: Threads not created with thrd_create() need to call this to clean up TSS. */
C11THREADS_MAYBE_INLINE void thrd_exit(int res);
C11THREADS_MAYBE_INLINE int thrd_join(thrd_t thr, int *res);
C11THREADS_MAYBE_INLINE int thrd_detach(thrd_t thr);
C11THREADS_MAYBE_INLINE thrd_t thrd_current(void);
C11THREADS_MAYBE_INLINE int thrd_equal(thrd_t a, thrd_t b);
static inline int thrd_sleep(const struct timespec *ts_in, struct timespec *rem_out);
C11THREADS_MAYBE_INLINE void thrd_yield(void);

/* Mutex functions. */

C11THREADS_MAYBE_INLINE int mtx_init(mtx_t *mtx, int type);
C11THREADS_MAYBE_INLINE void mtx_destroy(mtx_t *mtx);
C11THREADS_MAYBE_INLINE int mtx_lock(mtx_t *mtx);
C11THREADS_MAYBE_INLINE int mtx_trylock(mtx_t *mtx);
static inline int mtx_timedlock(mtx_t *mtx, const struct timespec *ts);
C11THREADS_MAYBE_INLINE int mtx_unlock(mtx_t *mtx);

/* Condition variable functions. */

#if !defined(_WIN32) || defined(C11THREADS_PTHREAD_WIN32) || !defined(C11THREADS_NO_COND_WIN32)
C11THREADS_MAYBE_INLINE int cnd_init(cnd_t *cond);
#endif
static inline void cnd_destroy(cnd_t *cond);
#if !defined(_WIN32) || defined(C11THREADS_PTHREAD_WIN32) || !defined(C11THREADS_NO_COND_WIN32)
C11THREADS_MAYBE_INLINE int cnd_signal(cnd_t *cond);
C11THREADS_MAYBE_INLINE int cnd_broadcast(cnd_t *cond);
C11THREADS_MAYBE_INLINE int cnd_wait(cnd_t *cond, mtx_t *mtx);
static inline int cnd_timedwait(cnd_t *cond, mtx_t *mtx, const struct timespec *ts);
#endif

/* Thread-specific storage functions. */

C11THREADS_MAYBE_INLINE int tss_create(tss_t *key, tss_dtor_t dtor);
C11THREADS_MAYBE_INLINE void tss_delete(tss_t key);
C11THREADS_MAYBE_INLINE int tss_set(tss_t key, void *val);
C11THREADS_MAYBE_INLINE void *tss_get(tss_t key);

/* One-time callable function. */

C11THREADS_MAYBE_INLINE void call_once(once_flag *flag, void (*func)(void));

#ifdef C11THREADS_NO_TIMESPEC_GET
#ifndef UTC_TIME
#define UTC_TIME 1
#endif
static inline int timespec_get(struct timespec *ts, int base);
#endif


#if defined(_WIN32) && !defined(C11THREADS_PTHREAD_WIN32)

/* ---- thread management ---- */

#ifdef _USE_32BIT_TIME_T
int _thrd_sleep32_win32(const struct _c11threads_timespec32_win32_t *ts_in, struct _c11threads_timespec32_win32_t *rem_out);
static inline int thrd_sleep(const struct timespec *ts_in, struct timespec *rem_out)
{
	return _thrd_sleep32_win32((const struct _c11threads_timespec32_win32_t*)ts_in, (struct _c11threads_timespec32_win32_t*)rem_out);
}
#else
int _thrd_sleep64_win32(const struct _c11threads_timespec64_win32_t *ts_in, struct _c11threads_timespec64_win32_t *rem_out);
static inline int thrd_sleep(const struct timespec *ts_in, struct timespec *rem_out)
{
	return _thrd_sleep64_win32((const struct _c11threads_timespec64_win32_t*)ts_in, (struct _c11threads_timespec64_win32_t*)rem_out);
}
#endif

/* ---- mutexes ---- */

#ifdef _USE_32BIT_TIME_T
int _mtx_timedlock32_win32(mtx_t *mtx, const struct _c11threads_timespec32_win32_t *ts);
static inline int mtx_timedlock(mtx_t *mtx, const struct timespec *ts)
{
	return _mtx_timedlock32_win32(mtx, (const struct _c11threads_timespec32_win32_t*)ts);
}
#else
int _mtx_timedlock64_win32(mtx_t *mtx, const struct _c11threads_timespec64_win32_t *ts);
static inline int mtx_timedlock(mtx_t *mtx, const struct timespec *ts)
{
	return _mtx_timedlock64_win32(mtx, (const struct _c11threads_timespec64_win32_t*)ts);
}
#endif

/* ---- condition variables ---- */

static inline void cnd_destroy(cnd_t *cond)
{
	(void)cond;
}

#ifndef C11THREADS_NO_COND_WIN32
#ifdef _USE_32BIT_TIME_T
int _cnd_timedwait32_win32(cnd_t *cond, mtx_t *mtx, const struct _c11threads_timespec32_win32_t *ts);
static inline int cnd_timedwait(cnd_t *cond, mtx_t *mtx, const struct timespec *ts)
{
	return _cnd_timedwait32_win32(cond, mtx, (const struct _c11threads_timespec32_win32_t*)ts);
}
#else
int _cnd_timedwait64_win32(cnd_t *cond, mtx_t *mtx, const struct _c11threads_timespec64_win32_t *ts);
static inline int cnd_timedwait(cnd_t *cond, mtx_t *mtx, const struct timespec *ts)
{
	return _cnd_timedwait64_win32(cond, mtx, (const struct _c11threads_timespec64_win32_t*)ts);
}
#endif
#endif

/* ---- misc ---- */

#ifdef C11THREADS_NO_TIMESPEC_GET
#ifdef _USE_32BIT_TIME_T
int _c11threads_timespec32_get_win32(struct _c11threads_timespec32_win32_t *ts, int base);
static inline int timespec_get(struct timespec *ts, int base)
{
	return _c11threads_timespec32_get_win32((struct _c11threads_timespec32_win32_t*)ts, base);
}
#else
int _c11threads_timespec64_get_win32(struct _c11threads_timespec64_win32_t *ts, int base);
static inline int timespec_get(struct timespec *ts, int base)
{
	return _c11threads_timespec64_get_win32((struct _c11threads_timespec64_win32_t*)ts, base);
}
#endif
#endif

#else /* !defined(_WIN32) || defined(C11THREADS_PTHREAD_WIN32) */

/* ---- thread management ---- */

static inline int thrd_create(thrd_t *thr, thrd_start_t func, void *arg)
{
	int res = pthread_create(thr, 0, (void*(*)(void*))func, arg);
	if(res == 0) {
		return thrd_success;
	}
	return res == ENOMEM ? thrd_nomem : thrd_error;
}

static inline void thrd_exit(int res)
{
	pthread_exit((void*)(intptr_t)res);
}

static inline int thrd_join(thrd_t thr, int *res)
{
	void *retval;

	if(pthread_join(thr, &retval) != 0) {
		return thrd_error;
	}
	if(res) {
		*res = (int)(intptr_t)retval;
	}
	return thrd_success;
}

static inline int thrd_detach(thrd_t thr)
{
	return pthread_detach(thr) == 0 ? thrd_success : thrd_error;
}

static inline thrd_t thrd_current(void)
{
	return pthread_self();
}

static inline int thrd_equal(thrd_t a, thrd_t b)
{
	return pthread_equal(a, b);
}

static inline int thrd_sleep(const struct timespec *ts_in, struct timespec *rem_out)
{
	if(nanosleep(ts_in, rem_out) < 0) {
		if(errno == EINTR) return -1;
		return -2;
	}
	return 0;
}

static inline void thrd_yield(void)
{
	sched_yield();
}

/* ---- mutexes ---- */

static inline int mtx_init(mtx_t *mtx, int type)
{
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
}

static inline void mtx_destroy(mtx_t *mtx)
{
	pthread_mutex_destroy(mtx);
}

static inline int mtx_lock(mtx_t *mtx)
{
	int res = pthread_mutex_lock(mtx);
	if(res == EDEADLK) {
		return thrd_busy;
	}
	return res == 0 ? thrd_success : thrd_error;
}

static inline int mtx_trylock(mtx_t *mtx)
{
	int res = pthread_mutex_trylock(mtx);
	if(res == EBUSY) {
		return thrd_busy;
	}
	return res == 0 ? thrd_success : thrd_error;
}

static inline int mtx_timedlock(mtx_t *mtx, const struct timespec *ts)
{
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
}

static inline int mtx_unlock(mtx_t *mtx)
{
	return pthread_mutex_unlock(mtx) == 0 ? thrd_success : thrd_error;
}

/* ---- condition variables ---- */

static inline int cnd_init(cnd_t *cond)
{
	return pthread_cond_init(cond, 0) == 0 ? thrd_success : thrd_error;
}

static inline void cnd_destroy(cnd_t *cond)
{
	pthread_cond_destroy(cond);
}

static inline int cnd_signal(cnd_t *cond)
{
	return pthread_cond_signal(cond) == 0 ? thrd_success : thrd_error;
}

static inline int cnd_broadcast(cnd_t *cond)
{
	return pthread_cond_broadcast(cond) == 0 ? thrd_success : thrd_error;
}

static inline int cnd_wait(cnd_t *cond, mtx_t *mtx)
{
	return pthread_cond_wait(cond, mtx) == 0 ? thrd_success : thrd_error;
}

static inline int cnd_timedwait(cnd_t *cond, mtx_t *mtx, const struct timespec *ts)
{
	int res;

	if((res = pthread_cond_timedwait(cond, mtx, ts)) != 0) {
		return res == ETIMEDOUT ? thrd_timedout : thrd_error;
	}
	return thrd_success;
}

/* ---- thread-specific data ---- */

static inline int tss_create(tss_t *key, tss_dtor_t dtor)
{
	return pthread_key_create(key, dtor) == 0 ? thrd_success : thrd_error;
}

static inline void tss_delete(tss_t key)
{
	pthread_key_delete(key);
}

static inline int tss_set(tss_t key, void *val)
{
	return pthread_setspecific(key, val) == 0 ? thrd_success : thrd_error;
}

static inline void *tss_get(tss_t key)
{
	return pthread_getspecific(key);
}

/* ---- misc ---- */

static inline void call_once(once_flag *flag, void (*func)(void))
{
	pthread_once(flag, func);
}

#ifdef C11THREADS_NO_TIMESPEC_GET
static inline int timespec_get(struct timespec *ts, int base)
{
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
}
#endif

#endif  /* !defined(_WIN32) || defined(C11THREADS_PTHREAD_WIN32) */

#ifdef __cplusplus
}
#endif

#endif	/* C11THREADS_H_ */
