obj = test.o
bin = test

CFLAGS = -std=gnu11 -pedantic -Wall -g -I..
LDFLAGS = -lpthread

$(bin): $(obj)
	$(CC) -o $@ $(obj) $(LDFLAGS)

test.o: test.c ../c11threads.h

.PHONY: clean
clean:
	$(RM) $(obj) $(bin) *.wo ../*.wo *.exe


test.exe: test.wo ../c11threads_win32.wo
	$(CC) -o $@ $^

.SUFFIXES: .wo
.c.wo:
	$(CC) -o $@ -c $< $(CFLAGS)

.PHONY: cross
cross:
	make CC=i686-w64-mingw32-gcc test.exe
