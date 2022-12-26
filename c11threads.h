/*
Authors:	John Tsiombikas <nuclear@member.fsf.org>
			Oliver Old <oliver.old@outlook.com>

I place this piece of code in the public domain. Feel free to use as you see
fit. I'd appreciate it if you keep my name at the top of the code somehwere,
but whatever.

Main project site: https://github.com/jtsiomb/c11threads
*/

/* Important note to users: You need to define C11THREADS_DEFINE_GLOBALS **ONCE** per binary. (Needed for Win32.) */

#ifndef C11THREADS_H_
#define C11THREADS_H_

#ifdef _WIN32
#ifdef _CRT_NO_TIME_T
#error _CRT_NO_TIME_T defined: c11threads needs time_t
#endif
#include <Windows.h>
#if _WIN32_WINNT < _WIN32_WINNT_VISTA /* for InitOnce...() */
#error _WIN32_WINNT < _WIN32_WINNT_VISTA: c11threads needs some Windows Vista functions
#endif
#include <stdlib.h> /* for abort() */
#else
#include <stdint.h>
#include <errno.h>
#include <pthread.h>
#include <sched.h>	/* for sched_yield */
#include <sys/time.h>
#endif
#include <time.h>

#ifdef C11THREADS_DEFINE_GLOBALS
#ifdef _WIN32
CRITICAL_SECTION _thrd_list_critical_section;
struct _thrd_entry_t *_thrd_list = NULL;
CRITICAL_SECTION _tss_dtor_list_critical_section;
struct _tss_dtor_entry_t *_tss_dtor_list = NULL;
INIT_ONCE _thrd_init_once = INIT_ONCE_STATIC_INIT;
#endif
#endif

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

#ifdef _WIN32
#ifdef C11THREADS_DEFINE_THREAD_LOCAL
#define thread_local		__declspec(thread)
#endif
#define ONCE_FLAG_INIT		INIT_ONCE_STATIC_INIT
#define TSS_DTOR_ITERATIONS	4
#else
#ifdef C11THREADS_DEFINE_THREAD_LOCAL
#define thread_local		_Thread_local
#endif
#define ONCE_FLAG_INIT		PTHREAD_ONCE_INIT
#define TSS_DTOR_ITERATIONS	PTHREAD_DESTRUCTOR_ITERATIONS
#endif

#ifdef _WIN32
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
#ifdef _WIN32
typedef DWORD thrd_t;
typedef CRITICAL_SECTION mtx_t;
typedef CONDITION_VARIABLE cnd_t;
typedef DWORD tss_t;
typedef INIT_ONCE once_flag;
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

#ifdef C11THREADS_NO_TIMESPEC_GET
#ifndef UTC_TIME
#define UTC_TIME 1
#endif
static inline int timespec_get(struct timespec *ts, int base);
#endif

static inline int thrd_create(thrd_t *thr, thrd_start_t func, void *arg);
static inline void thrd_exit(int res);
static inline int thrd_join(thrd_t thr, int *res);
static inline int thrd_detach(thrd_t thr);
static inline thrd_t thrd_current(void);
static inline int thrd_equal(thrd_t a, thrd_t b);
static inline int thrd_sleep(const struct timespec *ts_in, struct timespec *rem_out);
static inline void thrd_yield(void);

static inline int mtx_init(mtx_t *mtx, int type);
static inline void mtx_destroy(mtx_t *mtx);
static inline int mtx_lock(mtx_t *mtx);
static inline int mtx_trylock(mtx_t *mtx);
static inline int mtx_timedlock(mtx_t *mtx, const struct timespec *ts);
static inline int mtx_unlock(mtx_t *mtx);

static inline int cnd_init(cnd_t *cond);
static inline void cnd_destroy(cnd_t *cond);
static inline int cnd_signal(cnd_t *cond);
static inline int cnd_broadcast(cnd_t *cond);
static inline int cnd_wait(cnd_t *cond, mtx_t *mtx);
static inline int cnd_timedwait(cnd_t *cond, mtx_t *mtx, const struct timespec *ts);

static inline int tss_create(tss_t *key, tss_dtor_t dtor);
static inline void tss_delete(tss_t key);
static inline int tss_set(tss_t key, void *val);
static inline void *tss_get(tss_t key);

static inline void call_once(once_flag *flag, void (*func)(void));


/* ---- platform ---- */

#ifdef _WIN32
struct _thrd_entry_t {
	thrd_t thrd;
	HANDLE h;
	struct _thrd_entry_t *next;
};

struct _tss_dtor_entry_t {
	tss_t key;
	tss_dtor_t dtor;
	struct _tss_dtor_entry_t *next;
};

extern INIT_ONCE _thrd_init_once;
extern CRITICAL_SECTION _thrd_list_critical_section;
extern struct _thrd_entry_t *_thrd_list;
extern CRITICAL_SECTION _tss_dtor_list_critical_section;
extern struct _tss_dtor_entry_t *_tss_dtor_list;

static inline int __stdcall _thrd_init_globals_callback(INIT_ONCE *init_once, void *parameter, void **context)
{
	(void)init_once;
	(void)parameter;
	(void)context;
	InitializeCriticalSection(&_thrd_list_critical_section);
	InitializeCriticalSection(&_tss_dtor_list_critical_section);
	return TRUE;
}

static inline void _thrd_init_globals()
{
	if (!InitOnceExecuteOnce(&_thrd_init_once, _thrd_init_globals_callback, NULL, NULL)) {
		abort();
	}
}
#endif

/* ---- utilities ---- */

#ifdef _WIN32
static inline int _thrd_util_is_timespec_valid(const struct timespec *ts)
{
	return ts->tv_sec >= 0 && ts->tv_nsec >= 0 && ts->tv_nsec <= 999999999;
}

static inline long long _thrd_util_timespec_to_file_time(const struct timespec *ts)
{
	unsigned long long sec_res;
	unsigned long long nsec_res;

#ifndef _USE_32BIT_TIME_T
	/* 64-bit time_t may cause overflow. */
	if (ts->tv_sec > MAXLONGLONG / 10000000) {
		return -1;
	}
#endif

	sec_res = (unsigned long long)ts->tv_sec * 10000000;
	/* Add another 100 ns if division yields remainder. */
	nsec_res = (unsigned long long)ts->tv_nsec / 100 + !!((unsigned long long)ts->tv_nsec % 100);

#ifndef _USE_32BIT_TIME_T
	/* 64-bit time_t may cause overflow. */
	if (nsec_res > MAXLONGLONG - sec_res) {
		return -1;
	}
#endif

	return sec_res + nsec_res;
}

static inline int _thrd_util_timespec_to_milliseconds(const struct timespec *ts, unsigned long *ms)
{
	unsigned long sec_res;
	unsigned long nsec_res;

	if ((ULONG_PTR)ts->tv_sec > (INFINITE - 1) / 1000) {
		return 0;
	}

	sec_res = (unsigned long)ts->tv_sec * 1000;
	/* Add another millisecond if division yields remainder. */
	nsec_res = (unsigned long)ts->tv_nsec / 1000000 + !!((unsigned long)ts->tv_nsec % 1000000);

	if (nsec_res > INFINITE - 1 - sec_res) {
		return 0;
	}

	*ms = sec_res + nsec_res;
	return 1;
}

static inline void _thrd_util_file_time_to_timespec(unsigned long long file_time, struct timespec *ts)
{
	ts->tv_sec = file_time / 10000000;
	ts->tv_nsec = file_time % 10000000 * 100;
}
#endif

/* ---- thread management ---- */

#if defined(_WIN32)
struct _thrd_start_thunk_parameters {
	thrd_start_t func;
	void *arg;
};

static inline int _thrd_register(thrd_t thrd, HANDLE h)
{
	int res;
	struct _thrd_entry_t **curr;

	res = 0;
	_thrd_init_globals();
	EnterCriticalSection(&_thrd_list_critical_section);
	curr = &_thrd_list;
	while (*curr) {
		curr = &(*curr)->next;
	}
	*curr = LocalAlloc(LMEM_FIXED, sizeof(**curr));
	if (*curr) {
		(*curr)->thrd = thrd;
		(*curr)->h = h;
		(*curr)->next = NULL;
		res = 1;
	}
	LeaveCriticalSection(&_thrd_list_critical_section);
	return res;
}

static inline int _thrd_deregister(thrd_t thrd)
{
	int res;
	struct _thrd_entry_t *prev;
	struct _thrd_entry_t *curr;

	res = 0;
	_thrd_init_globals();
	EnterCriticalSection(&_thrd_list_critical_section);
	prev = NULL;
	curr = _thrd_list;
	while (curr) {
		if (curr->thrd == thrd) {
			if (prev) {
				prev->next = curr->next;
			} else {
				_thrd_list = NULL;
			}
			CloseHandle(curr->h);
			LocalFree(curr);
			res = 1;
			break;
		}
		prev = curr;
		curr = curr->next;
	}
	LeaveCriticalSection(&_thrd_list_critical_section);
	return res;
}

static inline void _thrd_run_tss_dtors()
{
	int ran_dtor;
	size_t i;
	struct _tss_dtor_entry_t *prev;
	struct _tss_dtor_entry_t *curr;
	struct _tss_dtor_entry_t *temp;
	void *val;

	_thrd_init_globals();
	EnterCriticalSection(&_tss_dtor_list_critical_section);
	ran_dtor = 1;
	for (i = 0; i < TSS_DTOR_ITERATIONS && ran_dtor; ++i) {
		ran_dtor = 0;
		prev = NULL;
		curr = _tss_dtor_list;
		while (curr) {
			val = TlsGetValue(curr->key);
			if (val) {
				TlsSetValue(curr->key, NULL);
				curr->dtor(val);
				ran_dtor = 1;
			} else if (GetLastError() != ERROR_SUCCESS) {
				temp = curr->next;
				LocalFree(curr);
				curr = temp;
				if (prev) {
					prev->next = curr;
				} else if (!curr) {
					/* List empty. */
					_tss_dtor_list = NULL;
					LeaveCriticalSection(&_tss_dtor_list_critical_section);
					return;
				}
				continue;
			}
			prev = curr;
			curr = curr->next;
		}
	}
	LeaveCriticalSection(&_tss_dtor_list_critical_section);
}

static inline int __stdcall _thrd_start_thunk(struct _thrd_start_thunk_parameters *start_parameters)
{
	int res;
	struct _thrd_start_thunk_parameters local_start_params;
	CopyMemory(&local_start_params, start_parameters, sizeof(struct _thrd_start_thunk_parameters));
	LocalFree(start_parameters);
	res = local_start_params.func(local_start_params.arg);
	_thrd_run_tss_dtors();
	return res;
}
#endif

static inline int thrd_create(thrd_t *thr, thrd_start_t func, void *arg)
{
#ifdef _WIN32
	HANDLE h;
	thrd_t thrd;
	DWORD error;

	struct _thrd_start_thunk_parameters *thread_start_params;

	thread_start_params = LocalAlloc(LMEM_FIXED, sizeof(*thread_start_params));
	if (!thread_start_params) {
		return thrd_nomem;
	}

	thread_start_params->func = func;
	thread_start_params->arg = arg;

	h = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)_thrd_start_thunk, thread_start_params, CREATE_SUSPENDED, &thrd);
	if (h) {
		if (_thrd_register(thrd, h)) {
			if (ResumeThread(h) != (unsigned long)-1) {
				if (thr) {
					*thr = thrd;
				}
				return thrd_success;
			}
			error = GetLastError();
		} else {
			error = ERROR_NOT_ENOUGH_MEMORY;
		}
		TerminateThread(h, 0);
		CloseHandle(h);
	} else {
		error = GetLastError();
	}

	LocalFree(thread_start_params);
	return error == ERROR_NOT_ENOUGH_MEMORY ? thrd_nomem : thrd_error;
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
#ifdef _WIN32
	_thrd_run_tss_dtors();
	ExitThread(res);
#else
	pthread_exit((void*)(intptr_t)res);
#endif
}

static inline int thrd_join(thrd_t thr, int *res)
{
#ifdef _WIN32
	int ret;
	HANDLE thread;
	DWORD wait_status;

	ret = thrd_error;
	thread = OpenThread(SYNCHRONIZE | THREAD_QUERY_INFORMATION, FALSE, thr);
	if (thread) {
		do {
			wait_status = WaitForMultipleObjectsEx(1, &thread, FALSE, INFINITE, TRUE);
		} while (wait_status == WAIT_IO_COMPLETION);

		if (wait_status == WAIT_OBJECT_0 && (!res || GetExitCodeThread(thread, (unsigned long*)res))) {
			ret = thrd_success;
		}

		CloseHandle(thread);
		_thrd_deregister(thr);
	}

	return ret;
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
#ifdef _WIN32
	return _thrd_deregister(thr) ? thrd_success : thrd_error;
#else
	return pthread_detach(thr) == 0 ? thrd_success : thrd_error;
#endif
}

static inline thrd_t thrd_current(void)
{
#ifdef _WIN32
	return GetCurrentThreadId();
#else
	return pthread_self();
#endif
}

static inline int thrd_equal(thrd_t a, thrd_t b)
{
#ifdef _WIN32
	return a == b;
#else
	return pthread_equal(a, b);
#endif
}

#ifdef _WIN32
#pragma warning(push)
#pragma warning(disable: 4701)
/*
* warning C4701: potentially uninitialized local variable 'time_start' used
* warning C4701: potentially uninitialized local variable 'perf_freq' used
*/
static inline int _thrd_sleep_impl(long long file_time_in, unsigned long long *file_time_out)
{
	HANDLE timer;
	DWORD error;
	LARGE_INTEGER due_time;
	LARGE_INTEGER perf_freq;
	LARGE_INTEGER time_start;
	DWORD wait_status;
	LARGE_INTEGER time_end;
	unsigned long long time_result;

	timer = CreateWaitableTimer(NULL, FALSE, NULL);
	if (!timer) {
		error = GetLastError();
		return error > 1 ? -(long)error : -ERROR_INTERNAL_ERROR;
	}

	due_time.QuadPart = -file_time_in;
	if (!SetWaitableTimer(timer, &due_time, 0, NULL, NULL, FALSE)) {
		error = GetLastError();
		CloseHandle(timer);
		return error > 1 ? -(long)error : -ERROR_INTERNAL_ERROR;
	}

	if (file_time_out) {
		if (!QueryPerformanceFrequency(&perf_freq)) {
			error = GetLastError();
			CloseHandle(timer);
			return error > 1 ? -(long)error : -ERROR_INTERNAL_ERROR;
		}

		if (!QueryPerformanceCounter(&time_start)) {
			error = GetLastError();
			CloseHandle(timer);
			return error > 1 ? -(long)error : -ERROR_INTERNAL_ERROR;
		}
	}

	wait_status = WaitForMultipleObjectsEx(1, &timer, FALSE, INFINITE, TRUE);
	error = GetLastError();
	CloseHandle(timer);
	if (wait_status == WAIT_OBJECT_0) {
		return 0; /* Success. */
	}

	if (wait_status == WAIT_IO_COMPLETION) {
		if (file_time_out) {
			if (!QueryPerformanceCounter(&time_end)) {
				error = GetLastError();
				return error > 1 ? -(long)error : -ERROR_INTERNAL_ERROR;
			}

			time_result = time_end.QuadPart - time_start.QuadPart;

			/* Would overflow. */
			if (time_result > MAXULONGLONG / 10000000) {
				time_result /= perf_freq.QuadPart; /* Try dividing first. */

				/* Inaccurate version would still overflow. */
				if (time_result > MAXULONGLONG / 10000000) {
					*file_time_out = 0; /* Pretend remaining time is 0. */
				} else {
					*file_time_out -= time_result * 10000000; /* Return inaccurate result. */
				}
			} else {
				*file_time_out -= time_result * 10000000 / perf_freq.QuadPart;
			}

			if (*file_time_out < 0) {
				*file_time_out = 0;
			}
		}

		return -1; /* APC queued. */
	}

	return error > 1 ? -(long)error : -ERROR_INTERNAL_ERROR;
}
#pragma warning(pop)
#endif

static inline int thrd_sleep(const struct timespec *ts_in, struct timespec *rem_out)
{
#ifdef _WIN32
	int res;
	long long file_time;

	if (!_thrd_util_is_timespec_valid(ts_in)) {
		return -ERROR_INVALID_PARAMETER;
	}

	file_time = _thrd_util_timespec_to_file_time(ts_in);
	if (file_time < 0) {
		return -ERROR_INVALID_PARAMETER;
	}

	res = _thrd_sleep_impl(file_time, rem_out ? (unsigned long long*)&file_time : NULL);

	if (res == -1 && rem_out) {
		_thrd_util_file_time_to_timespec(file_time, rem_out);
	}

	return res;
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
#ifdef _WIN32
	SwitchToThread();
#else
	sched_yield();
#endif
}

/* ---- mutexes ---- */

static inline int mtx_init(mtx_t *mtx, int type)
{
#ifdef _WIN32
	(void)type;
	InitializeCriticalSection(mtx);
	return thrd_success;
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
#ifdef _WIN32
	DeleteCriticalSection(mtx);
#else
	pthread_mutex_destroy(mtx);
#endif
}

static inline int mtx_lock(mtx_t *mtx)
{
#ifdef _WIN32
	EnterCriticalSection(mtx);
	return thrd_success;
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
#ifdef _WIN32
	return TryEnterCriticalSection(mtx) ? thrd_success : thrd_busy;
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
#ifdef _WIN32
	BOOL success;
	struct timespec ts_current;
	long long sleep_time;
	int sleep_res;

	if (!_thrd_util_is_timespec_valid(ts)) {
		return thrd_error;
	}

	success = TryEnterCriticalSection(mtx);
	while (!success) {
		if (!timespec_get(&ts_current, TIME_UTC)) {
			return thrd_error;
		}
		if (ts_current.tv_sec > ts->tv_sec || ts_current.tv_sec == ts->tv_sec && ts_current.tv_nsec >= ts_current.tv_nsec) {
			return thrd_timedout;
		}

		sleep_time = C11THREADS_TIMEDLOCK_POLL_INTERVAL / 100;
		do {
			sleep_res = _thrd_sleep_impl(sleep_time, (unsigned long long*)&sleep_time);
		} while (sleep_res == -1);
		if (sleep_res < -1) {
			return thrd_error;
		}

		success = TryEnterCriticalSection(mtx);
	}

	return thrd_success;
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
#ifdef _WIN32
	LeaveCriticalSection(mtx);
	return thrd_success;
#else
	return pthread_mutex_unlock(mtx) == 0 ? thrd_success : thrd_error;
#endif
}

/* ---- condition variables ---- */

static inline int cnd_init(cnd_t *cond)
{
#ifdef _WIN32
	InitializeConditionVariable(cond);
	return thrd_success;
#else
	return pthread_cond_init(cond, 0) == 0 ? thrd_success : thrd_error;
#endif
}

static inline void cnd_destroy(cnd_t *cond)
{
#ifdef _WIN32
	ZeroMemory(cond, sizeof(*cond));
#else
	pthread_cond_destroy(cond);
#endif
}

static inline int cnd_signal(cnd_t *cond)
{
#ifdef _WIN32
	WakeConditionVariable(cond);
	return thrd_success;
#else
	return pthread_cond_signal(cond) == 0 ? thrd_success : thrd_error;
#endif
}

static inline int cnd_broadcast(cnd_t *cond)
{
#ifdef _WIN32
	WakeAllConditionVariable(cond);
	return thrd_success;
#else
	return pthread_cond_broadcast(cond) == 0 ? thrd_success : thrd_error;
#endif
}

static inline int cnd_wait(cnd_t *cond, mtx_t *mtx)
{
#ifdef _WIN32
	return SleepConditionVariableCS(cond, mtx, INFINITE) ? thrd_success : thrd_error;
#else
	return pthread_cond_wait(cond, mtx) == 0 ? thrd_success : thrd_error;
#endif
}

static inline int cnd_timedwait(cnd_t *cond, mtx_t *mtx, const struct timespec *ts)
{
#ifdef _WIN32
	struct timespec ts_timeout;
	unsigned long timeout;

	if (!_thrd_util_is_timespec_valid(ts)) {
		return thrd_error;
	}

	if (!timespec_get(&ts_timeout, TIME_UTC)) {
		return thrd_error;
	}

	if (ts_timeout.tv_sec > ts->tv_sec || (ts_timeout.tv_sec == ts->tv_sec && ts_timeout.tv_nsec >= ts->tv_nsec)) {
		timeout = 0;
	} else {
		ts_timeout.tv_sec = ts->tv_sec - ts_timeout.tv_sec;
		ts_timeout.tv_nsec = ts->tv_nsec - ts_timeout.tv_nsec;
		if (ts_timeout.tv_nsec < 0) {
			--ts_timeout.tv_sec;
			ts_timeout.tv_nsec += 1000000000;
		}

		if (!_thrd_util_timespec_to_milliseconds(&ts_timeout, &timeout)) {
			return thrd_error;
		}
	}

	return SleepConditionVariableCS(cond, mtx, timeout) ? thrd_success : thrd_error;
#else
	int res;

	if((res = pthread_cond_timedwait(cond, mtx, ts)) != 0) {
		return res == ETIMEDOUT ? thrd_timedout : thrd_error;
	}
	return thrd_success;
#endif
}

/* ---- thread-specific data ---- */

#ifdef _WIN32
static inline int _tss_register(tss_t key, tss_dtor_t dtor) {
	int res;
	struct _tss_dtor_entry_t **curr;

	res = 0;
	_thrd_init_globals();
	EnterCriticalSection(&_tss_dtor_list_critical_section);
	curr = &_tss_dtor_list;
	while (*curr) {
		curr = &(*curr)->next;
	}
	*curr = LocalAlloc(LMEM_FIXED, sizeof(**curr));
	if (*curr) {
		(*curr)->key = key;
		(*curr)->dtor = dtor;
		(*curr)->next = NULL;
		res = 1;
	}
	LeaveCriticalSection(&_tss_dtor_list_critical_section);
	return res;
}

static inline void _tss_deregister(tss_t key) {
	struct _tss_dtor_entry_t *prev;
	struct _tss_dtor_entry_t *curr;

	_thrd_init_globals();
	EnterCriticalSection(&_tss_dtor_list_critical_section);
	prev = NULL;
	curr = _tss_dtor_list;
	while (curr) {
		if (curr->key == key) {
			if (prev) {
				prev->next = curr->next;
			} else {
				_tss_dtor_list = NULL;
			}
			LocalFree(curr);
			break;
		}
		prev = curr;
		curr = curr->next;
	}
	LeaveCriticalSection(&_tss_dtor_list_critical_section);
}
#endif

static inline int tss_create(tss_t *key, tss_dtor_t dtor)
{
#ifdef _WIN32
	*key = TlsAlloc();
	if (*key == TLS_OUT_OF_INDEXES) {
		return thrd_error;
	}
	if (dtor && !_tss_register(*key, dtor)) {
		TlsFree(*key);
		*key = 0;
		return thrd_error;
	}
	return thrd_success;
#else
	return pthread_key_create(key, dtor) == 0 ? thrd_success : thrd_error;
#endif
}

static inline void tss_delete(tss_t key)
{
#ifdef _WIN32
	_tss_deregister(key);
	TlsFree(key);
#else
	pthread_key_delete(key);
#endif
}

static inline int tss_set(tss_t key, void *val)
{
#ifdef _WIN32
	return TlsSetValue(key, val) ? thrd_success : thrd_error;
#else
	return pthread_setspecific(key, val) == 0 ? thrd_success : thrd_error;
#endif
}

static inline void *tss_get(tss_t key)
{
#ifdef _WIN32
	return TlsGetValue(key);
#else
	return pthread_getspecific(key);
#endif
}

/* ---- misc ---- */

#ifdef _WIN32
static inline int __stdcall _call_once_thunk(INIT_ONCE *init_once, void (*func)(void), void **context)
{
	(void)init_once;
	(void)context;
	func();
	return TRUE;
}
#endif

static inline void call_once(once_flag *flag, void (*func)(void))
{
#ifdef _WIN32
	InitOnceExecuteOnce(flag, (PINIT_ONCE_FN)_call_once_thunk, (void*)func, NULL);
#else
	pthread_once(flag, func);
#endif
}

#ifdef C11THREADS_NO_TIMESPEC_GET
static inline int timespec_get(struct timespec *ts, int base)
{
#ifdef _WIN32
	FILETIME file_time;
	ULARGE_INTEGER li;

	if (base != TIME_UTC) {
		return 0;
	}

	GetSystemTimeAsFileTime(&file_time);

	li.LowPart = file_time.dwLowDateTime;
	li.HighPart = file_time.dwHighDateTime;

	/* Also subtract difference between FILETIME and UNIX time epoch. It's 369 years by the way. */
	ts->tv_sec = li.QuadPart / 10000000 - 11644473600;
	ts->tv_nsec = li.QuadPart % 10000000 * 100;

	return base;
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
