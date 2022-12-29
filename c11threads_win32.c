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

#ifdef C11THREADS_NO_COND_WIN32
#define WINVER 0x0400 /* Windows NT 4.0 */
#define _WIN32_WINNT WINVER
#endif
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>


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

void c11threads_init_win32(void)
{
	assert(!_c11threads_initialized_win32);
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

void c11threads_destroy_win32(void)
{
	_c11threads_assert_initialized_win32();
	_c11threads_initialized_win32 = 0;
	assert(!_c11threads_tss_dtor_list_win32);
	assert(!_c11threads_thrd_list_win32);
	DeleteCriticalSection(&_c11threads_tss_dtor_list_critical_section_win32);
	DeleteCriticalSection(&_c11threads_thrd_list_critical_section_win32);
}

/* ---- utilities ---- */

static _Bool _c11threads_util_is_timespec32_valid_win32(const struct _c11threads_timespec32_win32_t *ts)
{
	return ts->tv_sec >= 0 && ts->tv_nsec >= 0 && ts->tv_nsec <= 999999999;
}

static _Bool _c11threads_util_is_timespec64_valid_win32(const struct _c11threads_timespec64_win32_t *ts)
{
	return ts->tv_sec >= 0 && ts->tv_nsec >= 0 && ts->tv_nsec <= 999999999;
}

/* Precondition: 'ts' validated. */
static long long _c11threads_util_timespec32_to_file_time_win32(const struct _c11threads_timespec32_win32_t *ts)
{
	unsigned long long sec_res;
	unsigned long long nsec_res;

	sec_res = (unsigned long long)ts->tv_sec * 10000000ULL;

	/* Add another 100 ns if division yields remainder. */
	nsec_res = (unsigned long)ts->tv_nsec / 100UL + !!((unsigned long)ts->tv_nsec % 100UL);

	return sec_res + nsec_res;
}

/* Precondition: 'ts' validated. */
static long long _c11threads_util_timespec64_to_file_time_win32(const struct _c11threads_timespec64_win32_t *ts, size_t *periods)
{
	unsigned long long sec_res;
	unsigned long long nsec_res;
	unsigned long long res;

	*periods = (unsigned long)((unsigned long long)ts->tv_sec / 922337203685ULL);
	sec_res = ((unsigned long long)ts->tv_sec % 922337203685ULL) * 10000000ULL;

	/* Add another 100 ns if division yields remainder. */
	nsec_res = (unsigned long)ts->tv_nsec / 100UL + !!((unsigned long)ts->tv_nsec % 100UL);

	/* 64-bit time_t may cause overflow. */
	if (nsec_res > (unsigned long long) - 1 - sec_res) {
		++*periods;
		nsec_res -= (unsigned long long) - 1 - sec_res;
		sec_res = 0;
	}

	res = sec_res + nsec_res;

	if (*periods && !res) {
		--*periods;
		return 9223372036850000000LL;
	}

	return res;
}

/* Precondition: 'ts' validated. */
static _Bool _c11threads_util_timespec32_to_milliseconds_win32(const struct _c11threads_timespec32_win32_t *ts, unsigned long *ms)
{
	unsigned long sec_res;
	unsigned long nsec_res;

	if ((unsigned long)ts->tv_sec > (INFINITE - 1UL) / 1000UL) {
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

/* Precondition: 'ts' validated. */
static _Bool _c11threads_util_timespec64_to_milliseconds_win32(const struct _c11threads_timespec64_win32_t *ts, unsigned long *ms)
{
	unsigned long sec_res;
	unsigned long nsec_res;

	if ((unsigned long long)ts->tv_sec > (INFINITE - 1UL) / 1000UL) {
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
static void _c11threads_util_file_time_to_timespec32_win32(unsigned long long file_time, struct _c11threads_timespec32_win32_t *ts)
{
	ts->tv_sec = (long)(file_time / 10000000ULL);
	ts->tv_nsec = (file_time % 10000000ULL) * 100ULL;
}

/* Precondition: 'file_time' accumulated with 'periods' does not overflow. */
static void _c11threads_util_file_time_to_timespec64_win32(unsigned long long file_time, unsigned long long periods, struct _c11threads_timespec64_win32_t *ts)
{
	ts->tv_sec = file_time / 10000000ULL;
	ts->tv_sec += periods * 922337203685ULL;
	ts->tv_nsec = (file_time % 10000000ULL) * 100ULL;
}

#ifdef C11THREADS_NO_TIMESPEC_GET
int _c11threads_timespec32_get_win32(struct _c11threads_timespec32_win32_t *ts, int base)
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
	ts->tv_sec = (long)(li.QuadPart / 10000000ULL - 11644473600ULL);
	ts->tv_nsec = (li.QuadPart % 10000000ULL) * 100ULL;

	return base;
}

int _c11threads_timespec64_get_win32(struct _c11threads_timespec64_win32_t *ts, int base)
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
#else
int _c11threads_timespec32_get_win32(struct _c11threads_timespec32_win32_t *ts, int base)
{
	return _timespec32_get((struct _timespec32*)ts, base);
}

int _c11threads_timespec64_get_win32(struct _c11threads_timespec64_win32_t *ts, int base)
{
	return _timespec64_get((struct _timespec64*)ts, base);
}
#endif

static int _c11threads_util_sleep_win32(long long file_time_in, long long *file_time_out)
{
	void *timer;
	unsigned long error;
	LARGE_INTEGER due_time;
	LARGE_INTEGER time_start;
	unsigned long wait_status;
	LARGE_INTEGER time_end;
	unsigned long long time_result;

	assert(file_time_in >= 0);

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

/* ---- thread management ---- */

static _Bool _thrd_register_win32(thrd_t thrd, HANDLE h)
{
	_Bool allocated;
	struct _c11threads_thrd_entry_win32_t **curr;

	allocated = 0;
	_c11threads_assert_initialized_win32();
	EnterCriticalSection(&_c11threads_thrd_list_critical_section_win32);
	curr = &_c11threads_thrd_list_win32;
	while (*curr) {
		assert((*curr)->thrd != thrd);
		curr = &(*curr)->next;
	}
	*curr = malloc(sizeof(**curr));
	if (*curr) {
		(*curr)->thrd = thrd;
		(*curr)->h = h;
		(*curr)->next = NULL;
		allocated = 1;
	}
	LeaveCriticalSection(&_c11threads_thrd_list_critical_section_win32);
	return allocated;
}

static _Bool _thrd_deregister_win32(thrd_t thrd)
{
	_Bool found;
	struct _c11threads_thrd_entry_win32_t *prev;
	struct _c11threads_thrd_entry_win32_t *curr;

	found = 0;
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
			found = 1;
			break;
		}
		prev = curr;
		curr = curr->next;
	}
	assert(found);
	LeaveCriticalSection(&_c11threads_thrd_list_critical_section_win32);
	return found;
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

int c11threads_thrd_self_register_win32(void)
{
	void *process;
	void *thread;

	process = GetCurrentProcess();
	thread = GetCurrentThread();
	if (!DuplicateHandle(process, thread, process, &thread, STANDARD_RIGHTS_REQUIRED, 0, 0)) {
		return thrd_error;
	}
	if (!_thrd_register_win32(GetCurrentThreadId(), thread)) {
		CloseHandle(thread);
		return thrd_nomem;
	}
	return thrd_success;
}

int c11threads_thrd_register_win32(unsigned long win32_thread_id)
{
	void *h;

	h = OpenThread(STANDARD_RIGHTS_REQUIRED, 0, win32_thread_id);
	if (!h) {
		return thrd_error;
	}
	if (!_thrd_register_win32(win32_thread_id, h)) {
		CloseHandle(h);
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

int thrd_create(thrd_t *thr, thrd_start_t func, void *arg)
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

void thrd_exit(int res)
{
	_thrd_run_tss_dtors_win32();
	ExitThread(res);
}

int thrd_join(thrd_t thr, int *res)
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

int thrd_detach(thrd_t thr)
{
	return _thrd_deregister_win32(thr) ? thrd_success : thrd_error;
}

thrd_t thrd_current(void)
{
	return GetCurrentThreadId();
}

int _thrd_sleep32_win32(const struct _c11threads_timespec32_win32_t *ts_in, struct _c11threads_timespec32_win32_t *rem_out)
{
	int res;
	long long file_time;

	if (!_c11threads_util_is_timespec32_valid_win32(ts_in)) {
		return -ERROR_INVALID_PARAMETER;
	}

	file_time = _c11threads_util_timespec32_to_file_time_win32(ts_in);
	if (file_time < 0) {
		return -ERROR_INVALID_PARAMETER;
	}

	res = _c11threads_util_sleep_win32(file_time, rem_out ? &file_time : NULL);

	if (res == -1 && rem_out) {
		_c11threads_util_file_time_to_timespec32_win32(file_time, rem_out);
	}

	return res;
}

int _thrd_sleep64_win32(const struct _c11threads_timespec64_win32_t *ts_in, struct _c11threads_timespec64_win32_t *rem_out)
{
	int res;
	long long file_time;
	size_t periods;

	if (!_c11threads_util_is_timespec64_valid_win32(ts_in)) {
		return -ERROR_INVALID_PARAMETER;
	}

	file_time = _c11threads_util_timespec64_to_file_time_win32(ts_in, &periods);
	if (file_time < 0) {
		return -ERROR_INVALID_PARAMETER;
	}

restart_sleep:
	res = _c11threads_util_sleep_win32(file_time, rem_out ? &file_time : NULL);

	if (res == -1 && rem_out) {
		_c11threads_util_file_time_to_timespec64_win32(file_time, periods, rem_out);
	}

	if (!res && periods) {
		--periods;
		file_time = 9223372036850000000LL;
		goto restart_sleep;
	}

	return res;
}

void thrd_yield(void)
{
	SwitchToThread();
}

/* ---- mutexes ---- */

int mtx_init(mtx_t *mtx, int type)
{
	(void)type;
#ifdef _MSC_VER
#pragma warning(suppress: 28125) /* Warning C28125: The function 'InitializeCriticalSection' must be called from within a try/except block. */
#endif
	InitializeCriticalSection((LPCRITICAL_SECTION)mtx);
	return thrd_success;
}

void mtx_destroy(mtx_t *mtx)
{
	DeleteCriticalSection((LPCRITICAL_SECTION)mtx);
}

int mtx_lock(mtx_t *mtx)
{
	EnterCriticalSection((LPCRITICAL_SECTION)mtx);
	return thrd_success;
}

int mtx_trylock(mtx_t *mtx)
{
	return TryEnterCriticalSection((LPCRITICAL_SECTION)mtx) ? thrd_success : thrd_busy;
}

int _mtx_timedlock32_win32(mtx_t *mtx, const struct _c11threads_timespec32_win32_t *ts)
{
	int success;
	struct _c11threads_timespec32_win32_t ts_current;
	long long sleep_time;
	int sleep_res;

	if (!_c11threads_util_is_timespec32_valid_win32(ts)) {
		return thrd_error;
	}

	success = TryEnterCriticalSection((LPCRITICAL_SECTION)mtx);
	while (!success) {
		if (!_c11threads_timespec32_get_win32(&ts_current, TIME_UTC)) {
			return thrd_error;
		}
		if (ts_current.tv_sec > ts->tv_sec || (ts_current.tv_sec == ts->tv_sec && ts_current.tv_nsec >= ts->tv_nsec)) {
			return thrd_timedout;
		}

		sleep_time = C11THREADS_TIMEDLOCK_POLL_INTERVAL / 100;
		do {
			sleep_res = _c11threads_util_sleep_win32(sleep_time, &sleep_time);
		} while (sleep_res == -1);
		if (sleep_res < -1) {
			return thrd_error;
		}

		success = TryEnterCriticalSection((LPCRITICAL_SECTION)mtx);
	}

	return thrd_success;
}

int _mtx_timedlock64_win32(mtx_t *mtx, const struct _c11threads_timespec64_win32_t *ts)
{
	int success;
	struct _c11threads_timespec64_win32_t ts_current;
	long long sleep_time;
	int sleep_res;

	if (!_c11threads_util_is_timespec64_valid_win32(ts)) {
		return thrd_error;
	}

	success = TryEnterCriticalSection((LPCRITICAL_SECTION)mtx);
	while (!success) {
		if (!_c11threads_timespec64_get_win32(&ts_current, TIME_UTC)) {
			return thrd_error;
		}
		if (ts_current.tv_sec > ts->tv_sec || (ts_current.tv_sec == ts->tv_sec && ts_current.tv_nsec >= ts->tv_nsec)) {
			return thrd_timedout;
		}

		sleep_time = C11THREADS_TIMEDLOCK_POLL_INTERVAL / 100;
		do {
			sleep_res = _c11threads_util_sleep_win32(sleep_time, &sleep_time);
		} while (sleep_res == -1);
		if (sleep_res < -1) {
			return thrd_error;
		}

		success = TryEnterCriticalSection((LPCRITICAL_SECTION)mtx);
	}

	return thrd_success;
}

int mtx_unlock(mtx_t *mtx)
{
	LeaveCriticalSection((LPCRITICAL_SECTION)mtx);
	return thrd_success;
}

/* ---- condition variables ---- */

#if _WIN32_WINNT >= 0x0600 /* Windows Vista */
int cnd_init(cnd_t *cond)
{
	InitializeConditionVariable((PCONDITION_VARIABLE)cond);
	return thrd_success;
}

int cnd_signal(cnd_t *cond)
{
	WakeConditionVariable((PCONDITION_VARIABLE)cond);
	return thrd_success;
}

int cnd_broadcast(cnd_t *cond)
{
	WakeAllConditionVariable((PCONDITION_VARIABLE)cond);
	return thrd_success;
}

int cnd_wait(cnd_t *cond, mtx_t *mtx)
{
	return SleepConditionVariableCS((PCONDITION_VARIABLE)cond, (PCRITICAL_SECTION)mtx, INFINITE) ? thrd_success : thrd_error;
}

int _cnd_timedwait32_win32(cnd_t *cond, mtx_t *mtx, const struct _c11threads_timespec32_win32_t *ts)
{
	struct _c11threads_timespec32_win32_t end_time;
	unsigned long wait_time;
	_Bool clamped;

	if (!_c11threads_util_is_timespec32_valid_win32(ts)) {
		return thrd_error;
	}

	if (!_c11threads_timespec32_get_win32(&end_time, TIME_UTC)) {
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

		if (!_c11threads_util_timespec32_to_milliseconds_win32(&end_time, &wait_time)) {
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

int _cnd_timedwait64_win32(cnd_t *cond, mtx_t *mtx, const struct _c11threads_timespec64_win32_t *ts)
{
	struct _c11threads_timespec64_win32_t end_time;
	unsigned long wait_time;
	_Bool clamped;

	if (!_c11threads_util_is_timespec64_valid_win32(ts)) {
		return thrd_error;
	}

	if (!_c11threads_timespec64_get_win32(&end_time, TIME_UTC)) {
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

		if (!_c11threads_util_timespec64_to_milliseconds_win32(&end_time, &wait_time)) {
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

int tss_create(tss_t *key, tss_dtor_t dtor)
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

void tss_delete(tss_t key)
{
	_tss_deregister_win32(key);
	TlsFree(key);
}

int tss_set(tss_t key, void *val)
{
	return TlsSetValue(key, val) ? thrd_success : thrd_error;
}

void *tss_get(tss_t key)
{
	return TlsGetValue(key);
}

/* ---- misc ---- */

#if _WIN32_WINNT >= 0x0600 /* Windows Vista */
static int __stdcall _call_once_thunk_win32(INIT_ONCE *init_once, void (*func)(void), void **context)
{
	(void)init_once;
	(void)context;
	func();
	return 1;
}

void call_once(once_flag *flag, void (*func)(void))
{
	InitOnceExecuteOnce((PINIT_ONCE)flag, (PINIT_ONCE_FN)_call_once_thunk_win32, (void*)func, NULL);
}
#else
void call_once(once_flag *flag, void (*func)(void))
{
	long long sleep_time;
	int sleep_res;

	if (InterlockedCompareExchangePointerAcquire(&flag->ptr, (void*)1, (void*)0) == (void*)0) {
		(func)();
		InterlockedExchangePointer(&flag->ptr, (void*)2);
	} else {
		while (flag->ptr == (void*)1) {
			sleep_time = C11THREADS_CALLONCE_POLL_INTERVAL / 100;
			do {
				sleep_res = _c11threads_util_sleep_win32(sleep_time, &sleep_time);
			} while (sleep_res == -1);
			if (sleep_res < -1) {
				SwitchToThread();
			}
		}
	}
}
#endif

#endif
