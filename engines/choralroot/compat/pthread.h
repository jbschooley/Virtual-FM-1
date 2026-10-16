/* compat/pthread.h -- POSIX threads where the platform has them; on Windows (no <pthread.h>) the little the
 * ChoralRoot core names (its emulator's lock owner, cr_cpu1.h's optional second-core thread, never started here) */
#ifndef _WIN32
#include_next <pthread.h>
#else
#ifndef CR_COMPAT_PTHREAD_H
#define CR_COMPAT_PTHREAD_H
typedef unsigned long pthread_t;
typedef struct { int unused; } pthread_mutex_t, pthread_cond_t;
#define PTHREAD_MUTEX_INITIALIZER {0}
#define PTHREAD_COND_INITIALIZER {0}
static inline pthread_t pthread_self(void) { return 1; }
static inline int pthread_equal(pthread_t a, pthread_t b) { return a == b; }
static inline int pthread_mutex_lock(pthread_mutex_t *m) { (void)m; return 0; }
static inline int pthread_mutex_unlock(pthread_mutex_t *m) { (void)m; return 0; }
static inline int pthread_cond_wait(pthread_cond_t *c, pthread_mutex_t *m) { (void)c; (void)m; return 0; }
static inline int pthread_cond_signal(pthread_cond_t *c) { (void)c; return 0; }
static inline int pthread_cond_broadcast(pthread_cond_t *c) { (void)c; return 0; }
static inline int pthread_create(pthread_t *t, const void *a, void *(*f)(void *), void *arg) { (void)t; (void)a; (void)f; (void)arg; return 1; }
static inline int pthread_join(pthread_t t, void **r) { (void)t; (void)r; return 0; }
#endif
#endif
