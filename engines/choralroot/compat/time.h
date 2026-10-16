/* compat/time.h -- <time.h>, and on Windows (no clock_gettime) the monotonic clock ChoralRoot's emulator file
 * reads: there it only times its statistics (the plugin runs the core on simulated time), so C11's
 * timespec_get stands in */
#include_next <time.h>
#if defined(_WIN32) && !defined(CLOCK_MONOTONIC)
#define CLOCK_MONOTONIC 1
static inline int clock_gettime(int clock, struct timespec *ts)
{
    (void)clock;
    return timespec_get(ts, TIME_UTC) == TIME_UTC ? 0 : -1;
}
#endif
