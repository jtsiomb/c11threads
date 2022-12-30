/* Test program for c11threads. */

/* Needed for memory leak detection. */
#ifdef _WIN32
#define _CRTDBG_MAP_ALLOC
#include <stdlib.h>
#include <crtdbg.h>
#endif

#include "c11threads.h"

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>

mtx_t mtx;
mtx_t mtx2;
cnd_t cnd;
tss_t tss;
once_flag once = ONCE_FLAG_INIT;
int flag;

#define CHK_THRD_EXPECTED(a, b) assert_thrd_expected(a, b, __FILE__, __LINE__, #a, #b)
#define CHK_THRD(a) CHK_THRD_EXPECTED(a, thrd_success)
#define CHK_EXPECTED(a, b) assert_expected(a, b, __FILE__, __LINE__, #a, #b)
#define NUM_THREADS 4

void run_thread_test(void);
void run_timed_mtx_test(void);
void run_tss_test(void);
void run_call_once_test(void);

int main(void)
{
	puts("start thread test");
	run_thread_test();
	puts("end thread test\n");

	puts("start timed mutex test");
	run_timed_mtx_test();
	puts("end timed mutex test\n");

	puts("start thread-specific storage test");
	run_tss_test();
	puts("end thread-specific storage test\n");

	puts("start call once test");
	run_call_once_test();
	puts("end call once test\n");

#if defined(_WIN32) && !defined(C11THREADS_PTHREAD_WIN32)
	c11threads_win32_destroy();
#endif

#ifdef _WIN32
	if (_CrtDumpMemoryLeaks()) {
		abort();
	}
#endif

	puts("tests finished");
}

void assert_thrd_expected(int thrd_status, int expected, const char *file, unsigned int line, const char *expr, const char *expected_str)
{
	const char *thrd_status_str;

	if (thrd_status != expected) {
		fflush(stdout);

		switch (thrd_status)
		{
		case thrd_success:	thrd_status_str = "thrd_success"; break;
		case thrd_timedout:	thrd_status_str = "thrd_timedout"; break;
		case thrd_busy:		thrd_status_str = "thrd_busy"; break;
		case thrd_error:	thrd_status_str = "thrd_error"; break;
		case thrd_nomem:	thrd_status_str = "thrd_nomem"; break;
		default:
			fprintf(stderr, "%s:%u: %s: error %d, expected %s\n", file, line, expr, thrd_status, expected_str);
			abort();
		}

		fprintf(stderr, "%s:%u: %s: error %s, expected %s\n", file, line, expr, thrd_status_str, expected_str);
		abort();
	}
}

void assert_expected(int res, int expected, const char *file, unsigned int line, const char *expr, const char *expected_str)
{
	if (res != expected) {
		fflush(stdout);
		fprintf(stderr, "%s:%u: %s: error %d, expected %s\n", file, line, expr, res, expected_str);
		abort();
	}
}

int tfunc(void *arg)
{
	size_t num = (size_t)arg;
	struct timespec dur;

	printf("hello from thread %zu\n", num);

	dur.tv_sec = 4;
	dur.tv_nsec = 0;
	CHK_EXPECTED(thrd_sleep(&dur, NULL), 0);

	printf("thread %zu done\n", num);
	return 0;
}

void run_thread_test(void)
{
	size_t i;
	thrd_t threads[NUM_THREADS];

	for (i = 0; i < NUM_THREADS; i++) {
		CHK_THRD(thrd_create(threads + i, tfunc, (void*)i));
	}
	for (i = 0; i < NUM_THREADS; i++) {
		CHK_THRD(thrd_join(threads[i], NULL));
	}
}

#if !defined(_WIN32) || defined(C11THREADS_PTHREAD_WIN32) || !defined(C11THREADS_OLD_WIN32API)
int hold_mutex_three_seconds(void* arg)
{
	(void)arg;
	struct timespec dur;

	CHK_THRD(mtx_lock(&mtx));

	CHK_THRD(mtx_lock(&mtx2));
	flag = 1;
	CHK_THRD(cnd_signal(&cnd));
	CHK_THRD(mtx_unlock(&mtx2));

	dur.tv_sec = 3;
	dur.tv_nsec = 0;
	CHK_EXPECTED(thrd_sleep(&dur, NULL), 0);

	CHK_THRD(mtx_unlock(&mtx));

	return 0;
}

void run_timed_mtx_test(void)
{
	thrd_t thread;
	struct timespec ts;
	struct timespec dur;

	CHK_THRD(mtx_init(&mtx, mtx_timed));
	CHK_THRD(mtx_init(&mtx2, mtx_plain));
	CHK_THRD(cnd_init(&cnd));
	flag = 0;

	CHK_THRD(thrd_create(&thread, hold_mutex_three_seconds, NULL));

	CHK_THRD(mtx_lock(&mtx2));
	while (!flag) {
		CHK_THRD(cnd_wait(&cnd, &mtx2));
	}
	CHK_THRD(mtx_unlock(&mtx2));
	cnd_destroy(&cnd);
	mtx_destroy(&mtx2);

	CHK_EXPECTED(timespec_get(&ts, TIME_UTC), TIME_UTC);
	ts.tv_sec = ts.tv_sec + 2;
	CHK_THRD_EXPECTED(mtx_timedlock(&mtx, &ts), thrd_timedout);
	puts("thread has locked mutex & we timed out waiting for it");

	dur.tv_sec = 4;
	dur.tv_nsec = 0;
	CHK_EXPECTED(thrd_sleep(&dur, NULL), 0);

	CHK_THRD(mtx_timedlock(&mtx, &ts));
	puts("thread no longer has mutex & we grabbed it");
	CHK_THRD(mtx_unlock(&mtx));
	mtx_destroy(&mtx);
	CHK_THRD(thrd_join(thread, NULL));
}
#endif

void my_tss_dtor(void *arg)
{
	printf("dtor: content of tss: %zu\n", (size_t)arg);
	CHK_EXPECTED((int)(size_t)arg, 42);
}

int my_tss_thread_func(void *arg)
{
	(void)arg;
	void *tss_content;

	tss_content = tss_get(tss);
	printf("thread func: initial content of tss: %zu\n", (size_t)tss_content);
	CHK_THRD(tss_set(tss, (void*)42));
	tss_content = tss_get(tss);
	printf("thread func: initial content of tss: %zu\n", (size_t)tss_content);
	CHK_EXPECTED((int)(size_t)tss_content, 42);
	return 0;
}

void run_tss_test(void)
{
	thrd_t thread;

	CHK_THRD(tss_create(&tss, my_tss_dtor));
	CHK_THRD(thrd_create(&thread, my_tss_thread_func, NULL));
	CHK_THRD(thrd_join(thread, NULL));
	tss_delete(tss);
}

void my_call_once_func(void)
{
	puts("my_call_once_func() was called");
	++flag;
}

int my_call_once_thread_func(void *arg)
{
	(void)arg;
	puts("my_call_once_thread_func() was called");
	call_once(&once, my_call_once_func);
	return 0;
}

void run_call_once_test(void)
{
	thrd_t thread1;
	thrd_t thread2;
	thrd_t thread3;

	flag = 0;

	CHK_THRD(thrd_create(&thread1, my_call_once_thread_func, NULL));
	CHK_THRD(thrd_create(&thread2, my_call_once_thread_func, NULL));
	CHK_THRD(thrd_create(&thread3, my_call_once_thread_func, NULL));
	CHK_THRD(thrd_join(thread1, NULL));
	CHK_THRD(thrd_join(thread2, NULL));
	CHK_THRD(thrd_join(thread3, NULL));

	printf("content of flag: %d\n", flag);

	CHK_EXPECTED(flag, 1);
}
