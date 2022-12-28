/*
Win32 implementation for c11threads.

Authors:
John Tsiombikas <nuclear@member.fsf.org>
Oliver Old <oliver.old@outlook.com>

I place this piece of code in the public domain. Feel free to use as you see
fit. I'd appreciate it if you keep my name at the top of the code somewhere, but
whatever.

Main project site: https://github.com/jtsiomb/c11threads
*/

#if defined(_WIN32) && !defined(C11THREADS_PTHREAD_WIN32)

/* Map debug malloc and free functions for debug builds. DO NOT CHANGE THE INCLUDE ORDER! */
#define _CRTDBG_MAP_ALLOC
#include <stdlib.h>
#include <crtdbg.h>

#include "c11threads.h"

#include <assert.h>
#include <malloc.h>
#include <stddef.h>

/* Condition variables and one-time callables need at least Windows Vista. */
#ifdef C11THREADS_SUPPORT_WINNT_OLDER_THAN_VISTA
#define WINVER 0x0400 /* Windows NT 4.0 */
#define _WIN32_WINNT WINVER
#endif
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

#if !defined(C11THREADS_SUPPORT_WINNT_OLDER_THAN_VISTA) && _WIN32_WINNT < 0x0600 /* Windows Vista */
#error c11threads: Cannot support condition variables and call once on Windows older than Vista; define C11THREADS_SUPPORT_WINNT_OLDER_THAN_VISTA or C11THREADS_PTHREAD_WIN32 (use libpthread)
#endif


/* ---- library ---- */

struct _c11threads_thrd_entry_win32_t {
	thrd_t thrd;
	HANDLE h;
	struct _c11threads_thrd_entry_win32_t *next;
};

struct _c11threads_tss_dtor_entry_win32_t {
	tss_t key;
	tss_dtor_t dtor;
	struct _c11threads_tss_dtor_entry_win32_t *next;
};

static _Bool _c11threads_initialized_win32 = 0;
static LARGE_INTEGER _c11threads_perf_freq_win32;
static CRITICAL_SECTION _c11threads_thrd_list_critical_section_win32;
static struct _c11threads_thrd_entry_win32_t *_c11threads_thrd_list_win32 = NULL;
static CRITICAL_SECTION _c11threads_tss_dtor_list_critical_section_win32;
static struct _c11threads_tss_dtor_entry_win32_t *_c11threads_tss_dtor_list_win32 = NULL;

static void _c11threads_assert_initialized_win32(void)
{
	assert(_c11threads_initialized_win32);
}

void _c11threads_init_win32(void)
{
	QueryPerformanceFrequency(&_c11threads_perf_freq_win32);
#ifdef _MSC_VER
#pragma warning(suppress: 28125) /* Warning C28125: The function 'InitializeCriticalSection' must be called from within a try/except block. */
#endif
	InitializeCriticalSection(&_c11threads_thrd_list_critical_section_win32);
#ifdef _MSC_VER
#pragma warning(suppress: 28125) /* Warning C28125: The function 'InitializeCriticalSection' must be called from within a try/except block. */
#endif
	InitializeCriticalSection(&_c11threads_tss_dtor_list_critical_section_win32);
	_c11threads_initialized_win32 = 1;
}

void _c11threads_destroy_win32(void)
{
	_c11threads_assert_initialized_win32();
	if (_c11threads_initialized_win32) {
		_c11threads_initialized_win32 = 0;
		assert(!_c11threads_tss_dtor_list_win32);
		assert(!_c11threads_thrd_list_win32);
		DeleteCriticalSection(&_c11threads_tss_dtor_list_critical_section_win32);
		DeleteCriticalSection(&_c11threads_thrd_list_critical_section_win32);
	}
}

/* ---- utilities ---- */

static _Bool _c11threads_util_is_timespec_valid_win32(const struct timespec *ts)
{
	return ts->tv_sec >= 0 && ts->tv_nsec >= 0 && ts->tv_nsec <= 999999999;
}

/* Precondition: 'ts' validated. */
static long long _c11threads_util_timespec_to_file_time_win32(
	const struct timespec *ts,
#ifndef _USE_32BIT_TIME_T
	size_t *periods
#endif
)
{
	unsigned long long sec_res;
	unsigned long long nsec_res;

#ifdef _USE_32BIT_TIME_T
	sec_res = (unsigned long long)ts->tv_sec * 10000000ULL;
#else
	unsigned long long res;

	*periods = (unsigned long)((unsigned long long)ts->tv_sec / 922337203685ULL);
	sec_res = ((unsigned long long)ts->tv_sec % 922337203685ULL) * 10000000ULL;
#endif

	/* Add another 100 ns if division yields remainder. */
	nsec_res = (unsigned long)ts->tv_nsec / 100UL + !!((unsigned long)ts->tv_nsec % 100UL);

#ifdef _USE_32BIT_TIME_T
	return sec_res + nsec_res;
#else
	/* 64-bit time_t may cause overflow. */
	if (nsec_res > (unsigned long long)-1 - sec_res) {
		++*periods;
		nsec_res -= (unsigned long long)-1 - sec_res;
		sec_res = 0;
	}

	res = sec_res + nsec_res;

	if (*periods && !res) {
		--*periods;
		return 9223372036850000000LL;
	}

	return res;
#endif
}

/* Precondition: 'ts' validated. */
static _Bool _c11threads_util_timespec_to_milliseconds_win32(const struct timespec *ts, unsigned long *ms)
{
	unsigned long sec_res;
	unsigned long nsec_res;

	if (
#ifdef _USE_32BIT_TIME_T
		(unsigned long)ts->tv_sec > (INFINITE - 1UL) / 1000UL
#else
		(unsigned long long)ts->tv_sec > (INFINITE - 1UL) / 1000UL
#endif
	) {
		return 0;
	}

	sec_res = (unsigned long)ts->tv_sec * 1000UL;
	/* Add another millisecond if division yields remainder. */
	nsec_res = (unsigned long)ts->tv_nsec / 1000000UL + !!((unsigned long)ts->tv_nsec % 1000000UL);

	/* Overflow. */
	if (nsec_res > INFINITE - 1UL - sec_res) {
		return 0;
	}

	*ms = sec_res + nsec_res;
	return 1;
}

/* Precondition: 'file_time' accumulated with 'periods' does not overflow. */
static void _c11threads_util_file_time_to_timespec_win32(
	unsigned long long file_time,
#ifndef _USE_32BIT_TIME_T
	unsigned long long periods,
#endif
	struct timespec *ts
)
{
	ts->tv_sec = file_time / 10000000ULL;
#ifndef _USE_32BIT_TIME_T
	ts->tv_sec += periods * 922337203685ULL;
#endif
	ts->tv_nsec = (file_time % 10000000ULL) * 100ULL;
}

/* ---- thread management ---- */

static _Bool _thrd_register_win32(thrd_t thrd, HANDLE h)
{
	_Bool res;
	struct _c11threads_thrd_entry_win32_t **curr;

	res = 0;
	_c11threads_assert_initialized_win32();
	EnterCriticalSection(&_c11threads_thrd_list_critical_section_win32);
	curr = &_c11threads_thrd_list_win32;
	while (*curr) {
		curr = &(*curr)->next;
	}
	*curr = malloc(sizeof(**curr));
	if (*curr) {
		(*curr)->thrd = thrd;
		(*curr)->h = h;
		(*curr)->next = NULL;
		res = 1;
	}
	LeaveCriticalSection(&_c11threads_thrd_list_critical_section_win32);
	return res;
}

static _Bool _thrd_deregister_win32(thrd_t thrd)
{
	_Bool res;
	struct _c11threads_thrd_entry_win32_t *prev;
	struct _c11threads_thrd_entry_win32_t *curr;

	res = 0;
	_c11threads_assert_initialized_win32();
	EnterCriticalSection(&_c11threads_thrd_list_critical_section_win32);
	prev = NULL;
	curr = _c11threads_thrd_list_win32;
	while (curr) {
		if (curr->thrd == thrd) {
			if (prev) {
				prev->next = curr->next;
			} else {
				_c11threads_thrd_list_win32 = curr->next;
			}
			CloseHandle(curr->h);
			free(curr);
			res = 1;
			break;
		}
		prev = curr;
		curr = curr->next;
	}
	LeaveCriticalSection(&_c11threads_thrd_list_critical_section_win32);
	return res;
}

static void _thrd_run_tss_dtors_win32(void)
{
	_Bool ran_dtor;
	size_t i;
	struct _c11threads_tss_dtor_entry_win32_t *prev;
	struct _c11threads_tss_dtor_entry_win32_t *curr;
	struct _c11threads_tss_dtor_entry_win32_t *temp;
	void *val;

	_c11threads_assert_initialized_win32();
	EnterCriticalSection(&_c11threads_tss_dtor_list_critical_section_win32);
	ran_dtor = 1;
	for (i = 0; i < TSS_DTOR_ITERATIONS && ran_dtor; ++i) {
		ran_dtor = 0;
		prev = NULL;
		curr = _c11threads_tss_dtor_list_win32;
		while (curr) {
			val = TlsGetValue(curr->key);
			if (val) {
				TlsSetValue(curr->key, NULL);
				curr->dtor(val);
				ran_dtor = 1;
			} else if (GetLastError() != ERROR_SUCCESS) {
				temp = curr->next;
				free(curr);
				curr = temp;
				if (prev) {
					prev->next = curr;
				} else if (!curr) {
					/* List empty. */
					_c11threads_tss_dtor_list_win32 = NULL;
					LeaveCriticalSection(&_c11threads_tss_dtor_list_critical_section_win32);
					return;
				}
				continue;
			}
			prev = curr;
			curr = curr->next;
		}
	}
	LeaveCriticalSection(&_c11threads_tss_dtor_list_critical_section_win32);
}

static int _thrd_sleep_internal_win32(long long file_time_in, long long *file_time_out)
{
	void *timer;
	unsigned long error;
	LARGE_INTEGER due_time;
	LARGE_INTEGER time_start;
	unsigned long wait_status;
	LARGE_INTEGER time_end;
	unsigned long long time_result;

	_c11threads_assert_initialized_win32();

	timer = CreateWaitableTimerW(NULL, 1, NULL);
	if (!timer) {
		error = GetLastError();
		return error > 1 ? -(long)error : -ERROR_INTERNAL_ERROR;
	}

	due_time.QuadPart = -file_time_in;
	if (!SetWaitableTimer(timer, &due_time, 0, NULL, NULL, 0)) {
		error = GetLastError();
		CloseHandle(timer);
		return error > 1 ? -(long)error : -ERROR_INTERNAL_ERROR;
	}

	if (file_time_out) {
		if (!QueryPerformanceCounter(&time_start)) {
			error = GetLastError();
			CloseHandle(timer);
			return error > 1 ? -(long)error : -ERROR_INTERNAL_ERROR;
		}

		wait_status = WaitForMultipleObjectsEx(1, &timer, 0, INFINITE, 1);
		if (wait_status == WAIT_OBJECT_0) {
			CloseHandle(timer);
			return 0; /* Success. */
		}
		if (wait_status == WAIT_IO_COMPLETION) {
			CloseHandle(timer);

			if (!QueryPerformanceCounter(&time_end)) {
				error = GetLastError();
				return error > 1 ? -(long)error : -ERROR_INTERNAL_ERROR;
			}

			time_result = (unsigned long long)time_end.QuadPart - (unsigned long long)time_start.QuadPart;

			/* Would overflow. */
			if (time_result > (unsigned long long)-1 / 10000000ULL) {
				time_result /= (unsigned long long)_c11threads_perf_freq_win32.QuadPart; /* Try dividing first. */

				/* Inaccurate version would still overflow. */
				if (time_result > (unsigned long long)-1 / 10000000ULL) {
					*file_time_out = 0; /* Pretend remaining time is 0. */
				} else {
					*file_time_out = (unsigned long long)file_time_in - time_result * 10000000ULL; /* Return inaccurate result. */
				}
			} else {
				*file_time_out = (unsigned long long)file_time_in - time_result * 10000000ULL / (unsigned long long)_c11threads_perf_freq_win32.QuadPart;
			}

			if (*file_time_out < 0) {
				*file_time_out = 0;
			}

			return -1; /* APC queued. */
		}
		error = GetLastError();
	} else {
		wait_status = WaitForMultipleObjectsEx(1, &timer, 0, INFINITE, 1);
		if (wait_status == WAIT_OBJECT_0) {
			CloseHandle(timer);
			return 0; /* Success. */
		}
		if (wait_status == WAIT_IO_COMPLETION) {
			CloseHandle(timer);
			return -1; /* APC queued. */
		}
		error = GetLastError();
	}

	return error > 1 ? -(long)error : -ERROR_INTERNAL_ERROR;
}

int _c11threads_thrd_self_register_win32(void)
{
	void *process;
	void *thread;

	process = GetCurrentProcess();
	thread = GetCurrentThread();
	if (!DuplicateHandle(process, thread, process, &thread, 0, 0, DUPLICATE_SAME_ACCESS)) {
		return thrd_error;
	}
	if (!_thrd_register_win32(GetCurrentThreadId(), thread)) {
		CloseHandle(thread);
		return thrd_nomem;
	}
	return thrd_success;
}

struct _thrd_start_thunk_parameters_win32_t {
	thrd_start_t func;
	void *arg;
};

static int __stdcall _thrd_start_thunk_win32(struct _thrd_start_thunk_parameters_win32_t *start_parameters)
{
	int res;
	struct _thrd_start_thunk_parameters_win32_t local_start_params;
	local_start_params = *start_parameters;
	free(start_parameters);
	res = local_start_params.func(local_start_params.arg);
	_thrd_run_tss_dtors_win32();
	return res;
}

int _thrd_create_win32(thrd_t *thr, thrd_start_t func, void *arg)
{
	void *h;
	thrd_t thrd;
	unsigned long error;

	struct _thrd_start_thunk_parameters_win32_t *thread_start_params;

	thread_start_params = malloc(sizeof(*thread_start_params));
	if (!thread_start_params) {
		return thrd_nomem;
	}

	thread_start_params->func = func;
	thread_start_params->arg = arg;

	h = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)_thrd_start_thunk_win32, thread_start_params, CREATE_SUSPENDED, &thrd);
	if (h) {
		if (_thrd_register_win32(thrd, h)) {
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

	free(thread_start_params);
	return error == ERROR_NOT_ENOUGH_MEMORY ? thrd_nomem : thrd_error;
}

void _thrd_exit_win32(int res)
{
	_thrd_run_tss_dtors_win32();
	ExitThread(res);
}

int _thrd_join_win32(thrd_t thr, int *res)
{
	int ret;
	void *h;
	unsigned long wait_status;

	ret = thrd_error;
	h = OpenThread(SYNCHRONIZE | THREAD_QUERY_INFORMATION, 0, thr);
	if (h) {
		do {
			wait_status = WaitForMultipleObjectsEx(1, &h, 0, INFINITE, 1);
		} while (wait_status == WAIT_IO_COMPLETION);

		if (wait_status == WAIT_OBJECT_0 && (!res || GetExitCodeThread(h, (unsigned long*)res))) {
			ret = thrd_success;
		}

		CloseHandle(h);
		_thrd_deregister_win32(thr);
	}

	return ret;
}

int _thrd_detach_win32(thrd_t thr)
{
	return _thrd_deregister_win32(thr) ? thrd_success : thrd_error;
}

thrd_t _thrd_current_win32(void)
{
	return GetCurrentThreadId();
}

int _thrd_sleep_win32(const struct timespec *ts_in, struct timespec *rem_out)
{
	int res;
	long long file_time;
#ifndef _USE_32BIT_TIME_T
	size_t periods;
#endif

	if (!_c11threads_util_is_timespec_valid_win32(ts_in)) {
		return -ERROR_INVALID_PARAMETER;
	}

	file_time = _c11threads_util_timespec_to_file_time_win32(
		ts_in,
#ifndef _USE_32BIT_TIME_T
		&periods
#endif
	);
	if (file_time < 0) {
		return -ERROR_INVALID_PARAMETER;
	}


#ifndef _USE_32BIT_TIME_T
restart_sleep:
#endif
	res = _thrd_sleep_internal_win32(file_time, rem_out ? &file_time : NULL);

	if (res == -1 && rem_out) {
		_c11threads_util_file_time_to_timespec_win32(
			file_time,
#ifndef _USE_32BIT_TIME_T
			periods,
#endif
			rem_out
		);
	}

#ifndef _USE_32BIT_TIME_T
	if (!res && periods) {
		--periods;
		file_time = 9223372036850000000LL;
		goto restart_sleep;
	}
#endif

	return res;
}

void _thrd_yield_win32(void)
{
	SwitchToThread();
}

/* ---- mutexes ---- */

int _mtx_init_win32(mtx_t *mtx, int type)
{
	(void)type;
#ifdef _MSC_VER
#pragma warning(suppress: 28125) /* Warning C28125: The function 'InitializeCriticalSection' must be called from within a try/except block. */
#endif
	InitializeCriticalSection((LPCRITICAL_SECTION)mtx);
	return thrd_success;
}

void _mtx_destroy_win32(mtx_t *mtx)
{
	DeleteCriticalSection((LPCRITICAL_SECTION)mtx);
}

int _mtx_lock_win32(mtx_t *mtx)
{
	EnterCriticalSection((LPCRITICAL_SECTION)mtx);
	return thrd_success;
}

int _mtx_trylock_win32(mtx_t *mtx)
{
	return TryEnterCriticalSection((LPCRITICAL_SECTION)mtx) ? thrd_success : thrd_busy;
}

int _mtx_timedlock_win32(mtx_t *mtx, const struct timespec *ts)
{
	int success;
	struct timespec ts_current;
	long long sleep_time;
	int sleep_res;

	if (!_c11threads_util_is_timespec_valid_win32(ts)) {
		return thrd_error;
	}

	success = TryEnterCriticalSection((LPCRITICAL_SECTION)mtx);
	while (!success) {
		if (!timespec_get(&ts_current, TIME_UTC)) {
			return thrd_error;
		}
		if (ts_current.tv_sec > ts->tv_sec || (ts_current.tv_sec == ts->tv_sec && ts_current.tv_nsec >= ts->tv_nsec)) {
			return thrd_timedout;
		}

		sleep_time = C11THREADS_TIMEDLOCK_POLL_INTERVAL / 100;
		do {
			sleep_res = _thrd_sleep_internal_win32(sleep_time, &sleep_time);
		} while (sleep_res == -1);
		if (sleep_res < -1) {
			return thrd_error;
		}

		success = TryEnterCriticalSection((LPCRITICAL_SECTION)mtx);
	}

	return thrd_success;
}

int _mtx_unlock_win32(mtx_t *mtx)
{
	LeaveCriticalSection((LPCRITICAL_SECTION)mtx);
	return thrd_success;
}

/* ---- condition variables ---- */

#ifndef C11THREADS_SUPPORT_WINNT_OLDER_THAN_VISTA
int _cnd_init_win32(cnd_t *cond)
{
	InitializeConditionVariable((PCONDITION_VARIABLE)cond);
	return thrd_success;
}

int _cnd_signal_win32(cnd_t *cond)
{
	WakeConditionVariable((PCONDITION_VARIABLE)cond);
	return thrd_success;
}

int _cnd_broadcast_win32(cnd_t *cond)
{
	WakeAllConditionVariable((PCONDITION_VARIABLE)cond);
	return thrd_success;
}

int _cnd_wait_win32(cnd_t *cond, mtx_t *mtx)
{
	return SleepConditionVariableCS((PCONDITION_VARIABLE)cond, (PCRITICAL_SECTION)mtx, INFINITE) ? thrd_success : thrd_error;
}

int _cnd_timedwait_win32(cnd_t *cond, mtx_t *mtx, const struct timespec *ts)
{
	struct timespec end_time;
	unsigned long wait_time;
	_Bool clamped;

	if (!_c11threads_util_is_timespec_valid_win32(ts)) {
		return thrd_error;
	}

	if (!timespec_get(&end_time, TIME_UTC)) {
		return thrd_error;
	}

	clamped = 0;
	if (end_time.tv_sec > ts->tv_sec || (end_time.tv_sec == ts->tv_sec && end_time.tv_nsec >= ts->tv_nsec)) {
		wait_time = 0;
	} else {
		end_time.tv_sec = ts->tv_sec - end_time.tv_sec;
		end_time.tv_nsec = ts->tv_nsec - end_time.tv_nsec;
		if (end_time.tv_nsec < 0) {
			--end_time.tv_sec;
			end_time.tv_nsec += 1000000000;
		}

		if (!_c11threads_util_timespec_to_milliseconds_win32(&end_time, &wait_time)) {
			/* Clamp wait_time. Pretend we've had a spurious wakeup if expired. */
			wait_time = INFINITE - 1;
			clamped = 1;
		}
	}

	if (SleepConditionVariableCS((PCONDITION_VARIABLE)cond, (PCRITICAL_SECTION)mtx, wait_time)) {
		return thrd_success;
	} else if (GetLastError() == ERROR_TIMEOUT) {
		if (clamped) {
			return thrd_success;
		}
		return thrd_timedout;
	}

	return thrd_error;
}
#endif

/* ---- thread-specific data ---- */

static int _tss_register_win32(tss_t key, tss_dtor_t dtor) {
	int res;
	struct _c11threads_tss_dtor_entry_win32_t **curr;

	res = 0;
	_c11threads_assert_initialized_win32();
	EnterCriticalSection(&_c11threads_tss_dtor_list_critical_section_win32);
	curr = &_c11threads_tss_dtor_list_win32;
	while (*curr) {
		curr = &(*curr)->next;
	}
	*curr = malloc(sizeof(**curr));
	if (*curr) {
		(*curr)->key = key;
		(*curr)->dtor = dtor;
		(*curr)->next = NULL;
		res = 1;
	}
	LeaveCriticalSection(&_c11threads_tss_dtor_list_critical_section_win32);
	return res;
}

static void _tss_deregister_win32(tss_t key) {
	struct _c11threads_tss_dtor_entry_win32_t *prev;
	struct _c11threads_tss_dtor_entry_win32_t *curr;

	_c11threads_assert_initialized_win32();
	EnterCriticalSection(&_c11threads_tss_dtor_list_critical_section_win32);
	prev = NULL;
	curr = _c11threads_tss_dtor_list_win32;
	while (curr) {
		if (curr->key == key) {
			if (prev) {
				prev->next = curr->next;
			} else {
				_c11threads_tss_dtor_list_win32 = curr->next;
			}
			free(curr);
			break;
		}
		prev = curr;
		curr = curr->next;
	}
	LeaveCriticalSection(&_c11threads_tss_dtor_list_critical_section_win32);
}

int _tss_create_win32(tss_t *key, tss_dtor_t dtor)
{
	*key = TlsAlloc();
	if (*key == TLS_OUT_OF_INDEXES) {
		return thrd_error;
	}
	if (dtor && !_tss_register_win32(*key, dtor)) {
		TlsFree(*key);
		*key = 0;
		return thrd_error;
	}
	return thrd_success;
}

void _tss_delete_win32(tss_t key)
{
	_tss_deregister_win32(key);
	TlsFree(key);
}

int _tss_set_win32(tss_t key, void *val)
{
	return TlsSetValue(key, val) ? thrd_success : thrd_error;
}

void *_tss_get_win32(tss_t key)
{
	return TlsGetValue(key);
}

/* ---- misc ---- */

#ifndef C11THREADS_SUPPORT_WINNT_OLDER_THAN_VISTA
static int __stdcall _call_once_thunk_win32(INIT_ONCE *init_once, void (*func)(void), void **context)
{
	(void)init_once;
	(void)context;
	func();
	return 1;
}

void _call_once_win32(once_flag *flag, void (*func)(void))
{
	InitOnceExecuteOnce((PINIT_ONCE)flag, (PINIT_ONCE_FN)_call_once_thunk_win32, (void*)func, NULL);
}
#endif

int _timespec_get_win32(struct timespec *ts, int base)
{
	FILETIME file_time;
	ULARGE_INTEGER li;

	if (base != TIME_UTC) {
		return 0;
	}

	GetSystemTimeAsFileTime(&file_time);

	li.LowPart = file_time.dwLowDateTime;
	li.HighPart = file_time.dwHighDateTime;

	/* Also subtract difference between FILETIME and UNIX time epoch. It's 369 years by the way. */
	ts->tv_sec = li.QuadPart / 10000000ULL - 11644473600ULL;
	ts->tv_nsec = (li.QuadPart % 10000000ULL) * 100ULL;

	return base;
}

#endif
