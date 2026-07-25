/*
 * clock.h – Single‑header monotonic timing library (strict C89)
 *
 * Provides:
 *   clock_time          - double precision seconds
 *   clock_ticks         - high‑resolution tick counter (double)
 *   clock_init()        - one‑time initialisation (optional, lazy‑init)
 *   clock_monotonic()   - seconds since clock_init() (never jumps)
 *   clock_ticks_now()   - raw platform ticks (double)
 *   clock_ticks_to_seconds() / clock_seconds_to_ticks()
 *
 * Platforms:
 *   Windows  (MSVC, MinGW, Clang‑CL)  – QueryPerformanceCounter
 *   Linux    (GCC, Clang)              – clock_gettime(CLOCK_MONOTONIC)
 *   macOS    (Apple Clang)             – mach_absolute_time
 *   Generic  (POSIX)                   – gettimeofday fallback
 *
 * Intended for game engines and real‑time applications where
 * wall‑clock time (epoch) is not needed and can cause jumps.
 */

#ifndef CLOCK_H
#define CLOCK_H

#ifdef __cplusplus
extern "C" {
#endif

/* Universal time type – double precision seconds */
typedef double clock_time;

/* High‑resolution tick counter – stored as double for C89 portability */
typedef double clock_ticks;

/* ------------------------------------------------------------------
   Initialisation – call once before any other function (optional,
   all functions self‑initialise). Safe to call multiple times.
   ------------------------------------------------------------------ */
void clock_init(void);

/* ------------------------------------------------------------------
   clock_monotonic() – seconds since clock_init() was first called.
   Always non‑decreasing; ideal for delta‑time and game loops.
   ------------------------------------------------------------------ */
clock_time clock_monotonic(void);

/* ------------------------------------------------------------------
   High‑resolution tick counter for profiling.
   Usage:
       clock_ticks start = clock_ticks_now();
       // … code to profile …
       clock_ticks end   = clock_ticks_now();
       clock_time elapsed = clock_ticks_to_seconds(end - start);
   ------------------------------------------------------------------ */
clock_ticks clock_ticks_now(void);
clock_time  clock_ticks_to_seconds(clock_ticks ticks);
clock_ticks clock_seconds_to_ticks(clock_time seconds);

/* ------------------------------------------------------------------
   Macros for converting between seconds and milliseconds.
   ------------------------------------------------------------------ */
#define CLOCK_MS_TO_SEC(ms)  ((clock_time)(ms) / 1000.0)
#define CLOCK_SEC_TO_MS(s)   ((clock_time)(s) * 1000.0)

#ifdef __cplusplus
}
#endif

/* Platform‑specific includes */
#if defined(_WIN32) || defined(_WIN64)
  #ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
  #endif
  #include <windows.h>
#elif defined(__APPLE__) && defined(__MACH__)
  #include <mach/mach_time.h>
  #include <time.h>
  #include <sys/time.h>
#elif defined(__linux__) || defined(__unix__)
  #include <time.h>
  #include <sys/time.h>
  #include <unistd.h>
#else
  /* Generic POSIX fallback */
  #include <time.h>
  #include <sys/time.h>
  #include <unistd.h>
#endif

/* ------------------------------------------------------------------
   Internal state
   ------------------------------------------------------------------ */
static int   clock_g_init           = 0;   /* non‑zero after init */
static double clock_g_ticks_per_sec = 0.0; /* ticks to seconds multiplier */

/* Platform‑specific zero point */
#if defined(_WIN32) || defined(_WIN64)
  static double clock_g_qpc_freq  = 0.0;
  static double clock_g_qpc_start = 0.0;
#elif defined(__APPLE__) && defined(__MACH__)
  static double clock_g_mach_timebase_numer = 0.0;
  static double clock_g_mach_timebase_denom = 0.0;
  static double clock_g_mach_start          = 0.0;
#else
  static int clock_g_use_clock_gettime = 0;
  static struct timespec clock_g_ts_start;
  static struct timeval  clock_g_tv_start;
#endif

/* ------------------------------------------------------------------
   Internal: raw monotonic seconds since init (no offset needed)
   ------------------------------------------------------------------ */
static double clock_raw_monotonic_secs(void)
{
#if defined(_WIN32) || defined(_WIN64)
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    return ((double)now.QuadPart - clock_g_qpc_start) / clock_g_qpc_freq;
#elif defined(__APPLE__) && defined(__MACH__)
    double now = (double)mach_absolute_time();
    double elapsed = now - clock_g_mach_start;
    return elapsed * clock_g_mach_timebase_numer /
           (clock_g_mach_timebase_denom * 1000000000.0);
#else
    if (clock_g_use_clock_gettime) {
        struct timespec ts;
        clock_gettime(CLOCK_MONOTONIC, &ts);
        return (double)(ts.tv_sec  - clock_g_ts_start.tv_sec) +
               (double)(ts.tv_nsec - clock_g_ts_start.tv_nsec) / 1000000000.0;
    } else {
        struct timeval tv;
        gettimeofday(&tv, NULL);
        return (double)(tv.tv_sec  - clock_g_tv_start.tv_sec) +
               (double)(tv.tv_usec - clock_g_tv_start.tv_usec) / 1000000.0;
    }
#endif
}

/* ------------------------------------------------------------------
   clock_init()
   ------------------------------------------------------------------ */
void clock_init(void)
{
    if (clock_g_init) return;

#if defined(_WIN32) || defined(_WIN64)
    {
        LARGE_INTEGER freq, qpc;
        if (QueryPerformanceFrequency(&freq) && freq.QuadPart != 0)
            clock_g_qpc_freq = (double)freq.QuadPart;
        else
            clock_g_qpc_freq = 1.0;  /* safety fallback */
        QueryPerformanceCounter(&qpc);
        clock_g_qpc_start = (double)qpc.QuadPart;
        clock_g_ticks_per_sec = clock_g_qpc_freq;
    }
#elif defined(__APPLE__) && defined(__MACH__)
    {
        mach_timebase_info_data_t info;
        mach_timebase_info(&info);
        clock_g_mach_timebase_numer = (double)info.numer;
        clock_g_mach_timebase_denom = (double)info.denom;
        clock_g_mach_start = (double)mach_absolute_time();
        clock_g_ticks_per_sec = 1000000000.0 * clock_g_mach_timebase_denom /
                                clock_g_mach_timebase_numer;
    }
#else
    #if defined(_POSIX_TIMERS) && (_POSIX_TIMERS > 0) && defined(CLOCK_MONOTONIC)
        {
            struct timespec ts;
            if (clock_gettime(CLOCK_MONOTONIC, &ts) == 0) {
                clock_g_use_clock_gettime = 1;
                clock_g_ts_start = ts;
                clock_g_ticks_per_sec = 1000000000.0;
            } else {
                clock_g_use_clock_gettime = 0;
                gettimeofday(&clock_g_tv_start, NULL);
                clock_g_ticks_per_sec = 1000000.0;
            }
        }
    #else
        clock_g_use_clock_gettime = 0;
        gettimeofday(&clock_g_tv_start, NULL);
        clock_g_ticks_per_sec = 1000000.0;
    #endif
#endif

    clock_g_init = 1;
}

/* ------------------------------------------------------------------
   clock_monotonic()
   ------------------------------------------------------------------ */
clock_time clock_monotonic(void)
{
    if (!clock_g_init) clock_init();
    return clock_raw_monotonic_secs();
}

/* ------------------------------------------------------------------
   High‑resolution tick counter
   ------------------------------------------------------------------ */
clock_ticks clock_ticks_now(void)
{
    if (!clock_g_init) clock_init();

#if defined(_WIN32) || defined(_WIN64)
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    return (clock_ticks)(now.QuadPart);
#elif defined(__APPLE__) && defined(__MACH__)
    return (clock_ticks)mach_absolute_time();
#else
    if (clock_g_use_clock_gettime) {
        struct timespec ts;
        clock_gettime(CLOCK_MONOTONIC, &ts);
        return (clock_ticks)ts.tv_sec * 1000000000.0 + (clock_ticks)ts.tv_nsec;
    } else {
        struct timeval tv;
        gettimeofday(&tv, NULL);
        return (clock_ticks)tv.tv_sec * 1000000.0 + (clock_ticks)tv.tv_usec;
    }
#endif
}

/* ------------------------------------------------------------------
   Convert ticks to seconds.
   ------------------------------------------------------------------ */
clock_time clock_ticks_to_seconds(clock_ticks ticks)
{
    if (!clock_g_init) clock_init();
    return ticks / clock_g_ticks_per_sec;
}

/* ------------------------------------------------------------------
   Convert seconds to ticks.
   ------------------------------------------------------------------ */
clock_ticks clock_seconds_to_ticks(clock_time seconds)
{
    if (!clock_g_init) clock_init();
    return seconds * clock_g_ticks_per_sec;
}

#endif /* CLOCK_H */