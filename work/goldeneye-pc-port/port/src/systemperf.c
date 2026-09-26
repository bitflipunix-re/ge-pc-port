/*
 * ARM-GE per-game performance telemetry/control metadata.
 *
 * Privileged CPU/GPU/VM writes are intentionally NOT performed by the game
 * process. The PortMaster launcher applies fixed, validated values before
 * launch and restores the original sysfs/proc values on exit. This module
 * only registers config values, reports the live kernel state, and provides
 * a safe allocator-trim action.
 */
#include <stdio.h>
#include <string.h>

#include "platform.h"
#include "config.h"
#include "systemperf.h"

#if !defined(PLATFORM_WINDOWS)
#include <dirent.h>
#if defined(__GLIBC__)
#include <malloc.h>
#endif
#endif

static int cfgCpuGovernor = 0; /* 0 system, 1 schedutil, 2 performance, 3 powersave */
static int cfgGpuGovernor = 0; /* 0 system, 1 simple_ondemand, 2 performance, 3 powersave */
static int cfgRamProfile  = 0; /* 0 system, 1 low-swap, 2 balanced */

PD_CONSTRUCTOR static void systemPerfConfigInit(void)
{
    configRegisterInt("System.CpuGovernor", &cfgCpuGovernor, 0, 3);
    configRegisterInt("System.GpuGovernor", &cfgGpuGovernor, 0, 3);
    configRegisterInt("System.RamProfile",  &cfgRamProfile,  0, 2);
}

static int readLine(const char *path, char *out, size_t n)
{
    FILE *f;
    size_t len;
    if (!out || n == 0) return 0;
    out[0] = 0;
    f = fopen(path, "r");
    if (!f) return 0;
    if (!fgets(out, (int)n, f)) {
        fclose(f);
        out[0] = 0;
        return 0;
    }
    fclose(f);
    len = strlen(out);
    while (len && (out[len - 1] == '\n' || out[len - 1] == '\r' ||
                   out[len - 1] == ' '  || out[len - 1] == '\t')) {
        out[--len] = 0;
    }
    return out[0] != 0;
}

const char *systemPerfCpuGovernor(void)
{
    static char value[48];
#if defined(PLATFORM_WINDOWS)
    snprintf(value, sizeof(value), "N/A");
#else
    if (!readLine("/sys/devices/system/cpu/cpufreq/policy0/scaling_governor",
                  value, sizeof(value)) &&
        !readLine("/sys/devices/system/cpu/cpu0/cpufreq/scaling_governor",
                  value, sizeof(value))) {
        snprintf(value, sizeof(value), "UNAVAILABLE");
    }
#endif
    return value;
}

const char *systemPerfGpuGovernor(void)
{
    static char value[48];
#if defined(PLATFORM_WINDOWS)
    snprintf(value, sizeof(value), "N/A");
#else
    DIR *d = opendir("/sys/class/devfreq");
    struct dirent *de;
    value[0] = 0;
    if (d) {
        while ((de = readdir(d)) != NULL) {
            char path[512];
            if (de->d_name[0] == '.') continue;
            if (!strstr(de->d_name, "gpu") && !strstr(de->d_name, "mali")) continue;
            snprintf(path, sizeof(path), "/sys/class/devfreq/%s/governor", de->d_name);
            if (readLine(path, value, sizeof(value))) break;
        }
        closedir(d);
    }
    if (!value[0]) snprintf(value, sizeof(value), "UNAVAILABLE");
#endif
    return value;
}

int systemPerfSwappiness(void)
{
#if defined(PLATFORM_WINDOWS)
    return -1;
#else
    char value[32];
    if (!readLine("/proc/sys/vm/swappiness", value, sizeof(value))) return -1;
    return atoi(value);
#endif
}

int systemPerfTrimMemory(void)
{
#if !defined(PLATFORM_WINDOWS) && defined(__GLIBC__)
    return malloc_trim(0) != 0;
#else
    return 0;
#endif
}

const char *systemPerfTuneStatus(void)
{
    if (cfgCpuGovernor || cfgGpuGovernor || cfgRamProfile)
        return "APPLIES NEXT LAUNCH";
    return "SYSTEM DEFAULT";
}
