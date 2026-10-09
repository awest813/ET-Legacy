/*
 * execinfo.h stub for Emscripten builds.
 * Emscripten's libc has no backtrace support; provide the symbols
 * sys_unix.c expects so the code compiles and degrades gracefully.
 */
#ifndef STUB_EXECINFO_H
#define STUB_EXECINFO_H

#include <stddef.h>

typedef void *addr_t_stub;
#define backtrace_addr_t void

static inline int backtrace(void **buffer, int size)
{
	(void) buffer;
	(void) size;
	return 0;
}

static inline char **backtrace_symbols(void *const *buffer, int size)
{
	(void) buffer;
	(void) size;
	return NULL;
}

static inline void backtrace_symbols_fd(void *const *buffer, int size, int fd)
{
	(void) buffer;
	(void) size;
	(void) fd;
}

#endif /* STUB_EXECINFO_H */
