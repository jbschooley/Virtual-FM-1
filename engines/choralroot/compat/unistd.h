/* compat/unistd.h -- the platform's, or on Windows the names upstream/tests/hostsim.c's own test program uses
 * (never called by the plugin's core) */
#ifndef _WIN32
#include_next <unistd.h>
#else
#ifndef CR_COMPAT_UNISTD_H
#define CR_COMPAT_UNISTD_H
typedef int pid_t;
static inline pid_t fork(void) { return -1; }
static inline int usleep(unsigned us) { (void)us; return 0; }
#endif
#endif
