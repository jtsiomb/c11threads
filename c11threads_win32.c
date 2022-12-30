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

#ifdef _MSC_VER
/* Map debug malloc and free functions for debug builds. DO NOT CHANGE THE INCLUDE ORDER! */
#define _CRTDBG_MAP_ALLOC
#include <stdlib.h>
#include <crtdbg.h>
#endif

#include "c11threads.h"

#include <assert.h>
#include <malloc.h>
#include <stddef.h>

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>


/* ---- library ---- */

struct _c11threads_win32_thrd_entry_t {
	struct _c11threads_win32_thrd_entry_t *next;
	void *h;
	thrd_t thrd;
};

struct _c11threads_win32_tss_dtor_entry_t {
	struct _c11threads_win32_tss_dtor_entry_t *next;
	tss_dtor_t dtor;
	tss_t key;
};

static once_flag _c11threads_win32_initialized = ONCE_FLAG_INIT;
static CRITICAL_SECTION _c11threads_win32_thrd_list_critical_section;
static struct _c11threads_win32_thrd_entry_t *_c11threads_win32_thrd_list = NULL;
static CRITICAL_SECTION _c11threads_win32_tss_dtor_list_critical_section;
static struct _c11threads_win32_tss_dtor_entry_t *_c11threads_win32_tss_dtor_list = NULL;

static void _c11threads_win32_init(void)
{
#ifdef _MSC_VER
#pragma warning(suppress: 28125) /* Warning C28125: The function 'InitializeCriticalSection' must be called from within a try/except block. */
#endif
	InitializeCriticalSection(&_c11threads_win32_thrd_list_critical_section);
#ifdef _MSC_VER
#pragma warning(suppress: 28125) /* Warning C28125: The function 'InitializeCriticalSection' must be called from within a try/except block. */
#endif
	InitializeCriticalSection(&_c11threads_win32_tss_dtor_list_critical_section);
}

static void _c11threads_win32_ensure_initialized(void)
{
	call_once(&_c11threads_win32_initialized, _c11threads_win32_init);
}

void c11threads_win32_destroy(void)
{
	struct _c11threads_win32_tss_dtor_entry_t *tss_dtor_entry;
	struct _c11threads_win32_tss_dtor_entry_t *tss_dtor_entry_temp;
	struct _c11threads_win32_thrd_entry_t *thrd_entry;
	struct _c11threads_win32_thrd_entry_t *thrd_entry_temp;

	if (_c11threads_win32_initialized.ptr) {
		assert(!_c11threads_win32_tss_dtor_list);
		tss_dtor_entry = _c11threads_win32_tss_dtor_list;
		while (tss_dtor_entry) {
			tss_dtor_entry_temp = tss_dtor_entry->next;
			free(tss_dtor_entry);
			tss_dtor_entry = tss_dtor_entry_temp;
		}

		assert(!_c11threads_win32_thrd_list);
		thrd_entry = _c11threads_win32_thrd_list;
		while (thrd_entry) {
			thrd_entry_temp = thrd_entry->next;
			CloseHandle(thrd_entry->h);
			free(thrd_entry);
			thrd_entry = thrd_entry_temp;
		}

		_c11threads_win32_tss_dtor_list = NULL;
		_c11threads_win32_thrd_list = NULL;
		_c11threads_win32_initialized.ptr = NULL;

		DeleteCriticalSection(&_c11threads_win32_tss_dtor_list_critical_section);
		DeleteCriticalSection(&_c11threads_win32_thrd_list_critical_section);
	}
}

/* ---- utilities ---- */

static _Bool _c11threads_win32_util_is_timespec32_valid(const struct _c11threads_win32_timespec32_t *ts)
{
	return ts->tv_sec >= 0 && ts->tv_nsec >= 0 && ts->tv_nsec <= 999999999;
}

static _Bool _c11threads_win32_util_is_timespec64_valid(const struct _c11threads_win32_timespec64_t *ts)
{
	return ts->tv_sec >= 0 && ts->tv_nsec >= 0 && ts->tv_nsec <= 999999999;
}

/* Precondition: 'ts' validated. */
static long long _c11threads_win32_util_timespec32_to_file_time(const struct _c11threads_win32_timespec32_t *ts)
{
	unsigned long long sec_res;
	unsigned long long nsec_res;

	sec_res = (unsigned long long)ts->tv_sec * 10000000ULL;

	/* Add another 100 ns if division yields remainder. */
	nsec_res = (unsigned long)ts->tv_nsec / 100UL + !!((unsigned long)ts->tv_nsec % 100UL);

	return sec_res + nsec_res;
}

/* Precondition: 'ts' validated. */
static long long _c11threads_win32_util_timespec64_to_file_time(const struct _c11threads_win32_timespec64_t *ts, size_t *periods)
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
static _Bool _c11threads_win32_util_timespec32_to_milliseconds(const struct _c11threads_win32_timespec32_t *ts, unsigned long *ms)
{
	unsigned long sec_res;
	unsigned long nsec_res;

	/* Overflow. */
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
static _Bool _c11threads_win32_util_timespec64_to_milliseconds(const struct _c11threads_win32_timespec64_t *ts, unsigned long *ms)
{
	unsigned long sec_res;
	unsigned long nsec_res;

	/* Overflow. */
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

/* Precondition: 'current_time' and 'end_time' validated. */
static unsigned long _c11threads_win32_util_timepoint_to_millisecond_timespan32(const struct _c11threads_win32_timespec32_t *current_time, const struct _c11threads_win32_timespec32_t *end_time, _Bool *clamped) {
	unsigned long wait_time;
	struct _c11threads_win32_timespec32_t ts;

	*clamped = 0;
	if (current_time->tv_sec > end_time->tv_sec || (current_time->tv_sec == end_time->tv_sec && current_time->tv_nsec >= end_time->tv_nsec)) {
		wait_time = 0;
	} else {
		ts.tv_sec = end_time->tv_sec - current_time->tv_sec;
		ts.tv_nsec = end_time->tv_nsec - current_time->tv_nsec;
		if (ts.tv_nsec < 0) {
			--ts.tv_sec;
			ts.tv_nsec += 1000000000;
		}

		if (!_c11threads_win32_util_timespec32_to_milliseconds(&ts, &wait_time)) {
			/* Clamp wait_time. Pretend we've had a spurious wakeup if expired. */
			wait_time = INFINITE - 1;
			*clamped = 1;
		}
	}

	return wait_time;
}

/* Precondition: 'current_time' and 'end_time' validated. */
static unsigned long _c11threads_win32_util_timepoint_to_millisecond_timespan64(const struct _c11threads_win32_timespec64_t *current_time, const struct _c11threads_win32_timespec64_t *end_time, _Bool *clamped) {
	unsigned long wait_time;
	struct _c11threads_win32_timespec64_t ts;

	*clamped = 0;
	if (current_time->tv_sec > end_time->tv_sec || (current_time->tv_sec == end_time->tv_sec && current_time->tv_nsec >= end_time->tv_nsec)) {
		wait_time = 0;
	} else {
		ts.tv_sec = end_time->tv_sec - current_time->tv_sec;
		ts.tv_nsec = end_time->tv_nsec - current_time->tv_nsec;
		if (ts.tv_nsec < 0) {
			--ts.tv_sec;
			ts.tv_nsec += 1000000000;
		}

		if (!_c11threads_win32_util_timespec64_to_milliseconds(&ts, &wait_time)) {
			/* Clamp wait_time. Pretend we've had a spurious wakeup if expired. */
			wait_time = INFINITE - 1;
			*clamped = 1;
		}
	}

	return wait_time;
}

#if defined(C11THREADS_NO_TIMESPEC_GET) || !defined(_MSC_VER)
int _c11threads_win32_timespec32_get(struct _c11threads_win32_timespec32_t *ts, int base)
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

int _c11threads_win32_timespec64_get(struct _c11threads_win32_timespec64_t *ts, int base)
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
int _c11threads_win32_timespec32_get(struct _c11threads_win32_timespec32_t *ts, int base)
{
	return _timespec32_get((struct _timespec32*)ts, base);
}

int _c11threads_win32_timespec64_get(struct _c11threads_win32_timespec64_t *ts, int base)
{
	return _timespec64_get((struct _timespec64*)ts, base);
}
#endif

static int _c11threads_win32_util_sleep(long long file_time_in)
{
	void *timer;
	unsigned long error;
	LARGE_INTEGER due_time;

	assert(file_time_in >= 0);

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

	if (WaitForSingleObject(timer, INFINITE) == WAIT_FAILED) {
		error = GetLastError();
		CloseHandle(timer);
		return error > 1 ? -(long)error : -ERROR_INTERNAL_ERROR;
	}

	CloseHandle(timer);
	return 0; /* Success. */
}

/* ---- thread management ---- */

static _Bool _c11threads_win32_thrd_register(thrd_t thrd, HANDLE h)
{
	_Bool allocated;
	struct _c11threads_win32_thrd_entry_t **curr;

	allocated = 0;
	_c11threads_win32_ensure_initialized();
	EnterCriticalSection(&_c11threads_win32_thrd_list_critical_section);
	curr = &_c11threads_win32_thrd_list;
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
	LeaveCriticalSection(&_c11threads_win32_thrd_list_critical_section);
	return allocated;
}

static _Bool _c11threads_win32_thrd_deregister(thrd_t thrd)
{
	_Bool found;
	struct _c11threads_win32_thrd_entry_t *prev;
	struct _c11threads_win32_thrd_entry_t *curr;

	found = 0;
	_c11threads_win32_ensure_initialized();
	EnterCriticalSection(&_c11threads_win32_thrd_list_critical_section);
	prev = NULL;
	curr = _c11threads_win32_thrd_list;
	while (curr) {
		if (curr->thrd == thrd) {
			if (prev) {
				prev->next = curr->next;
			} else {
				_c11threads_win32_thrd_list = curr->next;
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
	LeaveCriticalSection(&_c11threads_win32_thrd_list_critical_section);
	return found;
}

static void _c11threads_win32_thrd_run_tss_dtors(void)
{
	_Bool ran_dtor;
	size_t i;
	struct _c11threads_win32_tss_dtor_entry_t *prev;
	struct _c11threads_win32_tss_dtor_entry_t *curr;
	struct _c11threads_win32_tss_dtor_entry_t *temp;
	void *val;

	_c11threads_win32_ensure_initialized();
	EnterCriticalSection(&_c11threads_win32_tss_dtor_list_critical_section);
	ran_dtor = 1;
	for (i = 0; i < TSS_DTOR_ITERATIONS && ran_dtor; ++i) {
		ran_dtor = 0;
		prev = NULL;
		curr = _c11threads_win32_tss_dtor_list;
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
					_c11threads_win32_tss_dtor_list = NULL;
					LeaveCriticalSection(&_c11threads_win32_tss_dtor_list_critical_section);
					return;
				}
				continue;
			}
			prev = curr;
			curr = curr->next;
		}
	}
	LeaveCriticalSection(&_c11threads_win32_tss_dtor_list_critical_section);
}

int c11threads_win32_thrd_self_register(void)
{
	void *process;
	void *thread;

	process = GetCurrentProcess();
	thread = GetCurrentThread();
	if (!DuplicateHandle(process, thread, process, &thread, STANDARD_RIGHTS_REQUIRED, 0, 0)) {
		return thrd_error;
	}
	if (!_c11threads_win32_thrd_register(GetCurrentThreadId(), thread)) {
		CloseHandle(thread);
		return thrd_nomem;
	}
	return thrd_success;
}

int c11threads_win32_thrd_register(unsigned long win32_thread_id)
{
	void *h;

	h = OpenThread(STANDARD_RIGHTS_REQUIRED, 0, win32_thread_id);
	if (!h) {
		return thrd_error;
	}
	if (!_c11threads_win32_thrd_register(win32_thread_id, h)) {
		CloseHandle(h);
		return thrd_nomem;
	}
	return thrd_success;
}

struct _c11threads_win32_thrd_start_thunk_parameters_t {
	thrd_start_t func;
	void *arg;
};

static int __stdcall _c11threads_win32_thrd_start_thunk(struct _c11threads_win32_thrd_start_thunk_parameters_t *start_parameters)
{
	int res;
	struct _c11threads_win32_thrd_start_thunk_parameters_t local_start_params;
	local_start_params = *start_parameters;
	free(start_parameters);
	res = local_start_params.func(local_start_params.arg);
	_c11threads_win32_thrd_run_tss_dtors();
	return res;
}

int thrd_create(thrd_t *thr, thrd_start_t func, void *arg)
{
	void *h;
	thrd_t thrd;
	unsigned long error;

	struct _c11threads_win32_thrd_start_thunk_parameters_t *thread_start_params;

	thread_start_params = malloc(sizeof(*thread_start_params));
	if (!thread_start_params) {
		return thrd_nomem;
	}

	thread_start_params->func = func;
	thread_start_params->arg = arg;

	h = CreateThread(NULL, 0, (PTHREAD_START_ROUTINE)_c11threads_win32_thrd_start_thunk, thread_start_params, CREATE_SUSPENDED, &thrd);
	if (h) {
		if (_c11threads_win32_thrd_register(thrd, h)) {
			if (ResumeThread(h) != (unsigned long)-1) {
				if (thr) {
					*thr = thrd;
				}
				return thrd_success;
			}
			error = GetLastError();
			_c11threads_win32_thrd_deregister(thrd);
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
	_c11threads_win32_thrd_run_tss_dtors();
	ExitThread(res);
}

int thrd_join(thrd_t thr, int *res)
{
	int ret;
	void *h;

	ret = thrd_success;
	h = OpenThread(SYNCHRONIZE | THREAD_QUERY_INFORMATION, 0, thr);
	if (h) {
		if (WaitForSingleObject(h, INFINITE) == WAIT_FAILED || (res && !GetExitCodeThread(h, (unsigned long*)res))) {
			ret = thrd_error;
		}

		CloseHandle(h);
		_c11threads_win32_thrd_deregister(thr);
	}

	return ret;
}

int thrd_detach(thrd_t thr)
{
	return _c11threads_win32_thrd_deregister(thr) ? thrd_success : thrd_error;
}

thrd_t thrd_current(void)
{
	return GetCurrentThreadId();
}

int _c11threads_win32_thrd_sleep32(const struct _c11threads_win32_timespec32_t *ts_in, struct _c11threads_win32_timespec32_t *rem_out)
{
	int res;
	long long file_time;

	(void)rem_out;

	if (!_c11threads_win32_util_is_timespec32_valid(ts_in)) {
		return -ERROR_INVALID_PARAMETER;
	}

	file_time = _c11threads_win32_util_timespec32_to_file_time(ts_in);
	if (file_time < 0) {
		return -ERROR_INVALID_PARAMETER;
	}

	res = _c11threads_win32_util_sleep(file_time);

	return res;
}

int _c11threads_win32_thrd_sleep64(const struct _c11threads_win32_timespec64_t *ts_in, struct _c11threads_win32_timespec64_t *rem_out)
{
	int res;
	long long file_time;
	size_t periods;

	(void)rem_out;

	if (!_c11threads_win32_util_is_timespec64_valid(ts_in)) {
		return -ERROR_INVALID_PARAMETER;
	}

	file_time = _c11threads_win32_util_timespec64_to_file_time(ts_in, &periods);
	if (file_time < 0) {
		return -ERROR_INVALID_PARAMETER;
	}

restart_sleep:
	res = _c11threads_win32_util_sleep(file_time);

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
	InitializeCriticalSection((PCRITICAL_SECTION)mtx);
	return thrd_success;
}

void mtx_destroy(mtx_t *mtx)
{
	DeleteCriticalSection((PCRITICAL_SECTION)mtx);
}

int mtx_lock(mtx_t *mtx)
{
	EnterCriticalSection((PCRITICAL_SECTION)mtx);
	return thrd_success;
}

int mtx_trylock(mtx_t *mtx)
{
	return TryEnterCriticalSection((PCRITICAL_SECTION)mtx) ? thrd_success : thrd_busy;
}

int _c11threads_win32_mtx_timedlock32(mtx_t *mtx, const struct _c11threads_win32_timespec32_t *ts)
{
	int success;
	struct _c11threads_win32_timespec32_t ts_current;

	if (!_c11threads_win32_util_is_timespec32_valid(ts)) {
		return thrd_error;
	}

	success = TryEnterCriticalSection((PCRITICAL_SECTION)mtx);
	while (!success) {
		if (!_c11threads_win32_timespec32_get(&ts_current, TIME_UTC)) {
			return thrd_error;
		}

		if (ts_current.tv_sec > ts->tv_sec || (ts_current.tv_sec == ts->tv_sec && ts_current.tv_nsec >= ts->tv_nsec)) {
			return thrd_timedout;
		}

		if (_c11threads_win32_util_sleep(C11THREADS_TIMEDLOCK_POLL_INTERVAL / 100)) {
			return thrd_error;
		}

		success = TryEnterCriticalSection((PCRITICAL_SECTION)mtx);
	}

	return thrd_success;
}

int _c11threads_win32_mtx_timedlock64(mtx_t *mtx, const struct _c11threads_win32_timespec64_t *ts)
{
	int success;
	struct _c11threads_win32_timespec64_t ts_current;

	if (!_c11threads_win32_util_is_timespec64_valid(ts)) {
		return thrd_error;
	}

	success = TryEnterCriticalSection((PCRITICAL_SECTION)mtx);
	while (!success) {
		if (!_c11threads_win32_timespec64_get(&ts_current, TIME_UTC)) {
			return thrd_error;
		}

		if (ts_current.tv_sec > ts->tv_sec || (ts_current.tv_sec == ts->tv_sec && ts_current.tv_nsec >= ts->tv_nsec)) {
			return thrd_timedout;
		}

		if (_c11threads_win32_util_sleep(C11THREADS_TIMEDLOCK_POLL_INTERVAL / 100)) {
			return thrd_error;
		}

		success = TryEnterCriticalSection((PCRITICAL_SECTION)mtx);
	}

	return thrd_success;
}

int mtx_unlock(mtx_t *mtx)
{
	LeaveCriticalSection((PCRITICAL_SECTION)mtx);
	return thrd_success;
}

/* ---- condition variables ---- */

#if _WIN32_WINNT >= 0x0600 /* Windows Vista */
int cnd_init(cnd_t *cond)
{
	InitializeConditionVariable((PCONDITION_VARIABLE)cond);
	return thrd_success;
}

void cnd_destroy(cnd_t *cond)
{
	(void)cond;
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

int _c11threads_win32_cnd_timedwait_common(cnd_t *cond, mtx_t *mtx, unsigned long wait_time, _Bool clamped)
{
	if (SleepConditionVariableCS((PCONDITION_VARIABLE)cond, (PCRITICAL_SECTION)mtx, wait_time)) {
		return thrd_success;
	}

	if (GetLastError() == ERROR_TIMEOUT) {
		return clamped ? thrd_success : thrd_timedout;
	}

	return thrd_error;
}

int _c11threads_win32_cnd_timedwait32(cnd_t *cond, mtx_t *mtx, const struct _c11threads_win32_timespec32_t *ts)
{
	struct _c11threads_win32_timespec32_t current_time;
	unsigned long wait_time;
	_Bool clamped;

	if (!_c11threads_win32_util_is_timespec32_valid(ts)) {
		return thrd_error;
	}

	if (!_c11threads_win32_timespec32_get(&current_time, TIME_UTC)) {
		return thrd_error;
	}

	wait_time = _c11threads_win32_util_timepoint_to_millisecond_timespan32(&current_time, ts, &clamped);

	return _c11threads_win32_cnd_timedwait_common(cond, mtx, wait_time, clamped);
}

int _c11threads_win32_cnd_timedwait64(cnd_t *cond, mtx_t *mtx, const struct _c11threads_win32_timespec64_t *ts)
{
	struct _c11threads_win32_timespec64_t current_time;
	unsigned long wait_time;
	_Bool clamped;

	if (!_c11threads_win32_util_is_timespec64_valid(ts)) {
		return thrd_error;
	}

	if (!_c11threads_win32_timespec64_get(&current_time, TIME_UTC)) {
		return thrd_error;
	}

	wait_time = _c11threads_win32_util_timepoint_to_millisecond_timespan64(&current_time, ts, &clamped);

	return _c11threads_win32_cnd_timedwait_common(cond, mtx, wait_time, clamped);
}
#else
struct _c11threads_win32_cnd_t {
	CRITICAL_SECTION critical_section;
	void *signal_event;
	void *broadcast_event;
	size_t wait_count;
};

int cnd_init(cnd_t *cond)
{
	struct _c11threads_win32_cnd_t *cnd;

	cnd = malloc(sizeof(struct _c11threads_win32_cnd_t));
	if (!cnd) {
		return thrd_nomem;
	}

#ifdef _MSC_VER
#pragma warning(suppress: 28125) /* Warning C28125: The function 'InitializeCriticalSection' must be called from within a try/except block. */
#endif
	InitializeCriticalSection(&cnd->critical_section);

	cnd->signal_event = CreateEventW(NULL, 0, 0, NULL);
	if (cnd->signal_event) {
		cnd->broadcast_event = CreateEventW(NULL, 1, 0, NULL);
		if (cnd->broadcast_event) {
			cnd->wait_count = 0;
			cond->ptr = cnd;
			return thrd_success;
		}
		CloseHandle(cnd->signal_event);
	}

	DeleteCriticalSection(&cnd->critical_section);
	free(cnd);
	return thrd_error;
}

void cnd_destroy(cnd_t *cond)
{
	struct _c11threads_win32_cnd_t *cnd;
	cnd = cond->ptr;
	assert(!cnd->wait_count);
	CloseHandle(cnd->broadcast_event);
	CloseHandle(cnd->signal_event);
	DeleteCriticalSection(&cnd->critical_section);
	free(cnd);
}

int cnd_signal(cnd_t *cond)
{
	struct _c11threads_win32_cnd_t *cnd;
	int success;

	cnd = cond->ptr;
	success = 1;

	EnterCriticalSection(&cnd->critical_section);
	if (cnd->wait_count) {
		success = SetEvent(cnd->signal_event);
	}
	LeaveCriticalSection(&cnd->critical_section);

	return success ? thrd_success : thrd_error;
}

int cnd_broadcast(cnd_t *cond)
{
	struct _c11threads_win32_cnd_t *cnd;
	int success;

	cnd = cond->ptr;
	success = 1;

	EnterCriticalSection(&cnd->critical_section);
	if (cnd->wait_count) {
		success = SetEvent(cnd->broadcast_event);
	}
	LeaveCriticalSection(&cnd->critical_section);

	return success ? thrd_success : thrd_error;
}

int _c11threads_win32_cnd_wait_common(cnd_t *cond, mtx_t *mtx, unsigned long wait_time, _Bool clamped)
{
	struct _c11threads_win32_cnd_t *cnd;
	unsigned long wait_status;
	int res;

	cnd = cond->ptr;

	EnterCriticalSection(&cnd->critical_section);
	++cnd->wait_count;
	LeaveCriticalSection(&cnd->critical_section);

	LeaveCriticalSection((PCRITICAL_SECTION)mtx);
	wait_status = WaitForMultipleObjects(2, &cnd->signal_event, 0, wait_time);

	EnterCriticalSection(&cnd->critical_section);
	--cnd->wait_count;
	if (cnd->wait_count) {
		if (wait_status == WAIT_OBJECT_0 + 1 /* broadcast_event */) {
			/* Wait for the other threads to unblock. */
			do {
				LeaveCriticalSection(&cnd->critical_section);
				Sleep(0);
				EnterCriticalSection(&cnd->critical_section);
			} while (cnd->wait_count);
		}
	} else {
		ResetEvent(cnd->broadcast_event);
	}
	LeaveCriticalSection(&cnd->critical_section);

	res = thrd_success;
	if (wait_status == WAIT_FAILED) {
		res = thrd_error;
	} else if (!clamped && wait_status == WAIT_TIMEOUT) {
		res = thrd_timedout;
	}

	EnterCriticalSection((PCRITICAL_SECTION)mtx);
	return res;
}

int cnd_wait(cnd_t *cond, mtx_t *mtx)
{
	return _c11threads_win32_cnd_wait_common(cond, mtx, INFINITE, 0);
}

int _c11threads_win32_cnd_timedwait32(cnd_t *cond, mtx_t *mtx, const struct _c11threads_win32_timespec32_t *ts)
{
	struct _c11threads_win32_timespec32_t current_time;
	unsigned long wait_time;
	_Bool clamped;

	if (!_c11threads_win32_util_is_timespec32_valid(ts)) {
		return thrd_error;
	}

	if (!_c11threads_win32_timespec32_get(&current_time, TIME_UTC)) {
		return thrd_error;
	}

	wait_time = _c11threads_win32_util_timepoint_to_millisecond_timespan32(&current_time, ts, &clamped);

	return _c11threads_win32_cnd_wait_common(cond, mtx, wait_time, clamped);
}

int _c11threads_win32_cnd_timedwait64(cnd_t *cond, mtx_t *mtx, const struct _c11threads_win32_timespec64_t *ts)
{
	struct _c11threads_win32_timespec64_t current_time;
	unsigned long wait_time;
	_Bool clamped;

	if (!_c11threads_win32_util_is_timespec64_valid(ts)) {
		return thrd_error;
	}

	if (!_c11threads_win32_timespec64_get(&current_time, TIME_UTC)) {
		return thrd_error;
	}

	wait_time = _c11threads_win32_util_timepoint_to_millisecond_timespan64(&current_time, ts, &clamped);

	return _c11threads_win32_cnd_wait_common(cond, mtx, wait_time, clamped);
}
#endif

/* ---- thread-specific data ---- */

static int _c11threads_win32_tss_register(tss_t key, tss_dtor_t dtor) {
	int res;
	struct _c11threads_win32_tss_dtor_entry_t **curr;

	res = 0;
	_c11threads_win32_ensure_initialized();
	EnterCriticalSection(&_c11threads_win32_tss_dtor_list_critical_section);
	curr = &_c11threads_win32_tss_dtor_list;
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
	LeaveCriticalSection(&_c11threads_win32_tss_dtor_list_critical_section);
	return res;
}

static void _c11threads_win32_tss_deregister(tss_t key) {
	struct _c11threads_win32_tss_dtor_entry_t *prev;
	struct _c11threads_win32_tss_dtor_entry_t *curr;

	_c11threads_win32_ensure_initialized();
	EnterCriticalSection(&_c11threads_win32_tss_dtor_list_critical_section);
	prev = NULL;
	curr = _c11threads_win32_tss_dtor_list;
	while (curr) {
		if (curr->key == key) {
			if (prev) {
				prev->next = curr->next;
			} else {
				_c11threads_win32_tss_dtor_list = curr->next;
			}
			free(curr);
			break;
		}
		prev = curr;
		curr = curr->next;
	}
	LeaveCriticalSection(&_c11threads_win32_tss_dtor_list_critical_section);
}

int tss_create(tss_t *key, tss_dtor_t dtor)
{
	*key = TlsAlloc();
	if (*key == TLS_OUT_OF_INDEXES) {
		return thrd_error;
	}
	if (dtor && !_c11threads_win32_tss_register(*key, dtor)) {
		TlsFree(*key);
		*key = 0;
		return thrd_error;
	}
	return thrd_success;
}

void tss_delete(tss_t key)
{
	_c11threads_win32_tss_deregister(key);
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
static int __stdcall _c11threads_win32_call_once_thunk(INIT_ONCE *init_once, void (*func)(void), void **context)
{
	(void)init_once;
	(void)context;
	func();
	return 1;
}

void call_once(once_flag *flag, void (*func)(void))
{
	InitOnceExecuteOnce((PINIT_ONCE)flag, (PINIT_ONCE_FN)_c11threads_win32_call_once_thunk, (void*)func, NULL);
}
#else
void call_once(once_flag *flag, void (*func)(void))
{
	if (InterlockedCompareExchangePointerAcquire(&flag->ptr, (void*)1, (void*)0) == (void*)0) {
		(func)();
		InterlockedExchangePointer(&flag->ptr, (void*)2);
	} else {
		while (flag->ptr == (void*)1) {
			if (_c11threads_win32_util_sleep(C11THREADS_CALLONCE_POLL_INTERVAL / 100)) {
				SwitchToThread();
			}
		}
	}
}
#endif

#endif
