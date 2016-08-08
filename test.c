#include <stdio.h>
#include "c11threads.h"

int tfunc(void *arg);

int main(void)
{
	int i;
	thrd_t threads[4];

	for(i=0; i<4; i++) {
		thrd_create(threads + i, tfunc, (void*)(long)i);
	}

	for(i=0; i<4; i++) {
		thrd_join(threads[i], 0);
	}

	return 0;
}

int tfunc(void *arg)
{
	int num = (long)arg;
	struct timespec dur;

	printf("hello from thread %d\n", num);

	dur.tv_sec = 4;
	dur.tv_nsec = 0;
	thrd_sleep(&dur, 0);

	printf("thread %d done\n", num);
	return 0;
}
