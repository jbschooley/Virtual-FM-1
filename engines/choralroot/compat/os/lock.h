/* compat/os/lock.h -- the plugin's stand-in for macOS's <os/lock.h> (choralroot_core.c). ChoralRoot's emulator
 * (upstream/tools/emu/emu_hal_fw.h) takes an unfair lock between its audio, timer and UI threads; in the plugin one
 * instance runs on one thread at a time (FeluccaEngine::bind holds its mutex), so the lock is never contended: a
 * lock that is always free, on every platform. */
#ifndef CR_COMPAT_OS_LOCK_H
#define CR_COMPAT_OS_LOCK_H
typedef struct { int unused; } os_unfair_lock;
#define OS_UNFAIR_LOCK_INIT {0}
static inline void os_unfair_lock_lock(os_unfair_lock *l) { (void)l; }
static inline int os_unfair_lock_trylock(os_unfair_lock *l) { (void)l; return 1; }
static inline void os_unfair_lock_unlock(os_unfair_lock *l) { (void)l; }
#endif
