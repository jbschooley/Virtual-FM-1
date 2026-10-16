/* compat/sys/wait.h -- the platform's, or on Windows what upstream/tests/hostsim.c's own test program names */
#ifndef _WIN32
#include_next <sys/wait.h>
#else
#ifndef CR_COMPAT_SYS_WAIT_H
#define CR_COMPAT_SYS_WAIT_H
#include "../unistd.h"
static inline pid_t waitpid(pid_t p, int *st, int o) { (void)p; if (st) *st = 0; (void)o; return -1; }
#define WIFEXITED(s) 1
#define WEXITSTATUS(s) (s)
#endif
#endif
