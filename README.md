Trivial C11 threads.h implementation over POSIX threads, and not-so-trivial
implementation over Win32 threads.

Rationale
---------
Even though GCC provides the threading features required by the C11 standard
(like atomics, and thread-local storage), GNU libc, still does not implement the
necessary library functions of the standard C thread API. Other popular
platforms similarly lack support for the C11 thread functions, like Microsoft's
C runtime library on Windows.

If you're starting a new multithreaded project in C right now, it would make
sense to use the standard C way of using threads instead of a mishmash of
various platform-specific APIs. So until the system libc adds support for it,
we need a stopgap that works exactly as the C standard describes.

How to use
----------

### With POSIX threads
On UNIX systems, or Windows with a 3rd party pthread implementation, c11threads
is implemented as a thin wrapper of static inline functions over pthreads.

No installation or compilation necessary; just drop `c11threads.h` into your
project source tree, and don't forget to link with "-lpthread". On some
platforms it might be required to also pass the `-pthread` flag to the compiler.

If your compiler does not support the inline keyword, define `C11THREADS_INLINE`
to whatever equivalent keyword the compiler provides, or to the empty string to
make all the functions simply static.

If you wish to use the pthreads implementation on Windows, in preference to the
native win32 one, you need to define `C11THREADS_PTHREAD_WIN32`.

### With Win32 threads
To use C11 threads over the Windows threads API, beyond adding `c11threads.h` to
your project, you also need to compile `c11threads_win32.c` as part of your
build. Additionally, if you're in a situation where letting c11threads keep
resources for the duration of the process lifetime is not desirable, you can
call `c11threads_destroy_win32()` to free them manually at any point, when
you're done with it.

License
-------
Authors:
  - John Tsiombikas <nuclear@member.fsf.org>: original POSIX threads wrapper
  - Oliver Old <oliver.old@outlook.com>: win32 implementation

I place this piece of code in the public domain. Feel free to use as you see
fit. I'd appreciate it if you keep my name at the top of the code somewhere, but
whatever.

Main project site: https://github.com/jtsiomb/c11threads

Feel free to send corrections, patches, trendy social pull requests,
pictures of your cat wearing santa hats, any good porn links, or investment
opportunities with Nigerian ex-royals... It's all good.
