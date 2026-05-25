/*
 * cpu_threads.h - Runtime CPU thread detection
 *
 * Detects the number of logical processors at runtime.
 * Works on Windows, Linux, macOS, and other POSIX systems.
 */

#ifndef CPU_THREADS_H
#define CPU_THREADS_H

#include "vectors.h"

/* Feature test macros for POSIX functions */
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#ifndef _XOPEN_SOURCE
#define _XOPEN_SOURCE 500
#endif


#ifdef __cplusplus
extern "C" {
#endif

/* Platform-specific headers */
#if defined(_WIN32) || defined(_WIN64)
#include <windows.h>
#elif defined(__APPLE__) && defined(__MACH__)
#include <sys/sysctl.h>
#include <stdlib.h>
#elif defined(__linux__) || defined(__unix__)
#include <unistd.h>
#include <stdio.h>
#endif

/* Get the number of logical processors available to the process.
 * Returns at least 1 on success. */
static i32 get_logical_thread_count(void)
{
#if defined(_WIN32) || defined(_WIN64)
    /* Windows: use GetSystemInfo (works on Windows XP and later) */
    SYSTEM_INFO sysinfo;
    GetSystemInfo(&sysinfo);
    i32 count = (i32)sysinfo.dwNumberOfProcessors;
    /* Respect process affinity mask if set */
    DWORD_PTR process_affinity, system_affinity;
    if (GetProcessAffinityMask(GetCurrentProcess(), &process_affinity, &system_affinity)) {
        i32 affinity_count = 0;
        for (DWORD_PTR mask = process_affinity; mask; mask >>= 1) {
            affinity_count += (mask & 1);
        }
        if (affinity_count > 0) count = affinity_count;
    }
    return count;

#elif defined(__APPLE__) && defined(__MACH__)
    /* macOS: use sysctl */
    int count = 0;
    size_t size = sizeof(count);
    int name[2] = { CTL_HW, HW_AVAILCPU };
    /* Fallback to HW_NCPU if HW_AVAILCPU is unavailable */
    if (sysctl(name, 2, &count, &size, NULL, 0) != 0 || count < 1) {
        name[1] = HW_NCPU;
        if (sysctl(name, 2, &count, &size, NULL, 0) != 0 || count < 1) {
            count = 1;
        }
    }
    return (i32)count;

#elif defined(__linux__) || defined(__unix__)
    /* Linux/Unix: use sysconf */
    long count = sysconf(_SC_NPROCESSORS_ONLN);
    if (count > 0) return (i32)count;
    count = sysconf(_SC_NPROCESSORS_CONF);
    if (count > 0) return (i32)count;
    return 1;

#else
    /* Generic POSIX fallback */
    long count = sysconf(_SC_NPROCESSORS_ONLN);
    return (count > 0) ? (i32)count : 1;
#endif
}

/* Get the number of physical cores (not logical processors).
 * Returns at least 1 on success. */
static i32 get_physical_core_count(void)
{
#if defined(_WIN32) || defined(_WIN64)
    /* Windows: use GetLogicalProcessorInformation (Vista and later) */
    #include <windows.h>
    #include <malloc.h>
    PSYSTEM_LOGICAL_PROCESSOR_INFORMATION buffer = NULL;
    DWORD buffer_size = 0;
    
    GetLogicalProcessorInformation(NULL, &buffer_size);
    buffer = (PSYSTEM_LOGICAL_PROCESSOR_INFORMATION)malloc(buffer_size);
    if (!buffer) return 1;
    
    if (!GetLogicalProcessorInformation(buffer, &buffer_size)) {
        free(buffer);
        return 1;
    }
    
    i32 physical_cores = 0;
    DWORD num_entries = buffer_size / sizeof(SYSTEM_LOGICAL_PROCESSOR_INFORMATION);
    for (DWORD i = 0; i < num_entries; i++) {
        if (buffer[i].Relationship == RelationProcessorCore) {
            physical_cores++;
        }
    }
    
    free(buffer);
    return physical_cores > 0 ? physical_cores : 1;

#elif defined(__APPLE__) && defined(__MACH__)
    /* macOS: use sysctl */
    #include <sys/sysctl.h>
    #include <stdlib.h>
    int count = 0;
    size_t size = sizeof(count);
    
    /* Try HW_PHYSICALCPU first */
    if (sysctlbyname("hw.physicalcpu", &count, &size, NULL, 0) != 0 || count < 1) {
        /* Fallback to HW_PHYSCPU */
        if (sysctlbyname("hw.physcpu", &count, &size, NULL, 0) != 0 || count < 1) {
            count = 1;
        }
    }
    return (i32)count;

#elif defined(__linux__) || defined(__unix__)
    /* Linux: read /proc/cpuinfo for core count */
    #include <stdio.h>
    
    /* Try to read cpu cores directly */
    FILE *fp = fopen("/proc/cpuinfo", "r");
    int cores = 0;
    
    if (fp) {
        char line[256];
        while (fgets(line, sizeof(line), fp)) {
            if (line[0] == 'c' && line[1] == 'p' && line[2] == 'u' && 
                line[3] == ' ' && line[4] == 'c' && line[5] == 'o' &&
                line[6] == 'r' && line[7] == 'e' && line[8] == 's') {
                int core_val;
                if (sscanf(line, "cpu cores : %d", &core_val) == 1 && core_val > 0) {
                    cores = core_val;
                    break;
                }
            }
        }
        fclose(fp);
    }
    
    if (cores > 0) return cores;
    
    /* Fallback: count unique (physical_package_id, core_id) pairs via sysfs */
    int seen_cores[256] = {0};
    int core_count = 0;
    char path[256];
    
    int cpu;
    for (cpu = 0; cpu < 256; cpu++) {
        snprintf(path, sizeof(path), "/sys/devices/system/cpu/cpu%d/topology/core_id", cpu);
        FILE *cfp = fopen(path, "r");
        if (!cfp) continue;
        
        int core_id = 0;
        if (fscanf(cfp, "%d", &core_id) == 1) {
            /* Read package id */
            char pkg_path[256];
            snprintf(pkg_path, sizeof(pkg_path), "/sys/devices/system/cpu/cpu%d/topology/physical_package_id", cpu);
            FILE *pfp = fopen(pkg_path, "r");
            int pkg_id = 0;
            if (pfp) {
                fscanf(pfp, "%d", &pkg_id);
                fclose(pfp);
            }
            int key = (pkg_id << 8) | (core_id & 0xFF);
            if (key >= 0 && key < 256 && !seen_cores[key]) {
                seen_cores[key] = 1;
                core_count++;
            }
        }
        fclose(cfp);
    }
    
    if (core_count > 0) return core_count;
    
    /* Final fallback to logical count */
    return get_logical_thread_count();

#else
    /* Generic fallback: assume 1:1 logical to physical ratio */
    i32 logical = get_logical_thread_count();
    return logical > 1 ? (logical > 4 ? logical / 2 : logical) : logical;
#endif
}

/* Recommended thread count for rasterization.
 * For CPU-bound rendering, use physical cores.
 * For memory-bound rendering, may benefit from hyperthreading. */
static i32 get_optimal_thread_count(void)
{
    i32 physical = get_physical_core_count();
    i32 logical = get_logical_thread_count();
    
    /* If we have hyperthreading, use physical cores (avoid cache contention) */
    if (logical >= physical * 2 && physical >= 2) {
        return physical;
    }
    
    /* No hyperthreading or single core: use all logical threads */
    return logical;
}

#ifdef __cplusplus
}
#endif

#endif /* CPU_THREADS_H */