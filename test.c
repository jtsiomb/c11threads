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

#define CHK_THRD(a) assert_thrd_success(a, __FILE__, __LINE__, #a)
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
	puts("stop timed mutex test\n");

	puts("start thread-specific storage test");
	run_tss_test();
	puts("stop thread-specific storage test\n");

	puts("start call once test");
	run_call_once_test();
	puts("stop call once test\n");

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

void assert_thrd_success(int thrd_status, const char *file, unsigned int line, const char *func)
{
	const char *result;

	if (thrd_status != thrd_success) {
		fflush(stdout);

		switch (thrd_status)
		{
		case thrd_timedout:	result = "thrd_timedout"; break;
		case thrd_busy:		result = "thrd_busy"; break;
		case thrd_error:	result = "thrd_error"; break;
		case thrd_nomem:	result = "thrd_nomem"; break;
		default:
			fprintf(stderr, "%s:%u: %s: thrd status = %d\n", file, line, func, thrd_status);
			abort();
		}

		fprintf(stderr, "%s:%u: %s: %s\n", file, line, func, result);
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
	if (thrd_sleep(&dur, NULL)) {
		fprintf(stderr, "%s:%u: thrd_sleep() failed\n", __FILE__, __LINE__);
		abort();
	}

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
	if (thrd_sleep(&dur, NULL)) {
		fprintf(stderr, "%s:%u: thrd_sleep() failed\n", __FILE__, __LINE__);
		abort();
	}

	CHK_THRD(mtx_unlock(&mtx));

	return 0;
}

void run_timed_mtx_test(void)
{
	thrd_t thread;
	struct timespec ts;
	struct timespec dur;
	int thrd_status;

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

	if (!timespec_get(&ts, TIME_UTC)) {
		fprintf(stderr, "%s:%u: timespec_get() failed\n", __FILE__, __LINE__);
		abort();
	}
	ts.tv_sec = ts.tv_sec + 2;
	if ((thrd_status = mtx_timedlock(&mtx,&ts)) == thrd_timedout) {
		puts("thread has locked mutex & we timed out waiting for it");
	} else {
		fprintf(stderr, "%s:%u: expected to time out, but received result %d\n", __FILE__, __LINE__, thrd_status);
		abort();
	}

	dur.tv_sec = 4;
	dur.tv_nsec = 0;
	if (thrd_sleep(&dur, NULL)) {
		fprintf(stderr, "%s:%u: thrd_sleep() failed\n", __FILE__, __LINE__);
		abort();
	}

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
	if ((size_t)arg != 42) {
		fprintf(stderr, "%s:%u: wrong tss content: %zu\n", __FILE__, __LINE__, (size_t)arg);
		abort();
	}
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
	if ((size_t)tss_content != 42) {
		fprintf(stderr, "%s:%u: wrong tss content: %zu\n", __FILE__, __LINE__, (size_t)tss_content);
		abort();
	}
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

	if (flag != 1) {
		fprintf(stderr, "%s:%u: wrong flag content: %d\n", __FILE__, __LINE__, flag);
		abort();
	}
}
