obj = test.o
bin = test

CFLAGS = -pedantic -Wall -g
LDFLAGS = -lpthread

$(bin): $(obj)
	$(CC) -o $@ $(obj) $(LDFLAGS)

test.o: test.c c11threads.h

.PHONY: clean
clean:
	rm -f $(obj) $(bin)
