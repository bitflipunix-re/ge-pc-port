/*
 * GoldenEye native benchmark harness.
 *
 * --benchmark turns a normal direct-level launch into a deterministic timed
 * measurement. Rendering and game simulation remain the real engine: this
 * module only observes frame boundaries and host process telemetry, then asks
 * main.c to shut the game down cleanly.
 *
 * Default run:
 *   5 s warmup after the first delivered frame
 *   20 s measurement
 *   60 s no-frame startup watchdog
 *   10 s mid-run stall watchdog
 *
 * Results are printed in human-readable form, emitted as a BENCH_TSV line for
 * scripts, and written as JSON. Frame pacing is sampled at every delivered
 * frame rather than from the once-per-second HUD FPS value.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "platform.h"
#if !defined(PLATFORM_WINDOWS)
#include <unistd.h>
#endif
#include "system.h"
#include "video.h"
#include "systemperf.h"
#include "benchmark.h"

#define BENCH_MAX_FRAMES 131072u

enum BenchStatus {
    BENCH_OK = 0,
    BENCH_NO_FRAMES = 1,
    BENCH_STALLED = 2,
    BENCH_SAMPLE_OVERFLOW = 3
};

static int g_enabled;
static volatile int g_done;
static int g_finished;
static enum BenchStatus g_status;

static int g_warmup_sec = 5;
static int g_measure_sec = 20;
static int g_no_frame_timeout_sec = 60;
static int g_stall_timeout_sec = 10;

static char g_name[128] = "benchmark";
static char g_out[512] = "ge007-benchmark.json";

static uint64_t g_init_us;
static volatile uint64_t g_first_frame_us;
static volatile uint64_t g_measure_start_us;
static volatile uint64_t g_last_frame_us;

static uint32_t g_frame_us[BENCH_MAX_FRAMES];
static volatile uint32_t g_frame_count;
static volatile uint64_t g_frame_sum_us;
static volatile uint32_t g_stutter_33;
static volatile uint32_t g_stutter_50;
static volatile uint32_t g_stutter_100;

static uint64_t g_cpu_start_ticks;
static long g_clk_tck = 0;
static long g_cpu_count = 1;
static long g_peak_rss_kb;

static int parseIntArg(const char *arg, int fallback, int lo, int hi)
{
    const char *s = sysArgGetString(arg);
    long v;
    char *end = NULL;
    if (!s || !*s) return fallback;
    v = strtol(s, &end, 10);
    if (!end || *end != 0) return fallback;
    if (v < lo) v = lo;
    if (v > hi) v = hi;
    return (int)v;
}

static void copyArg(char *dst, size_t n, const char *arg, const char *fallback)
{
    const char *s = sysArgGetString(arg);
    if (!s || !*s) s = fallback;
    snprintf(dst, n, "%s", s);
}

#if !defined(PLATFORM_WINDOWS)
static uint64_t readCpuTicks(void)
{
    FILE *f = fopen("/proc/self/stat", "r");
    char line[4096];
    char *p, *tok;
    int field = 3;
    uint64_t utime = 0, stime = 0;

    if (!f) return 0;
    if (!fgets(line, sizeof(line), f)) {
        fclose(f);
        return 0;
    }
    fclose(f);

    p = strrchr(line, ')');
    if (!p) return 0;
    ++p;

    tok = strtok(p, " ");
    while (tok) {
        if (field == 14) utime = strtoull(tok, NULL, 10);
        else if (field == 15) {
            stime = strtoull(tok, NULL, 10);
            break;
        }
        ++field;
        tok = strtok(NULL, " ");
    }
    return utime + stime;
}

static long readPeakRssKb(void)
{
    FILE *f = fopen("/proc/self/status", "r");
    char line[256];
    long rss = 0, hwm = 0;
    if (!f) return 0;
    while (fgets(line, sizeof(line), f)) {
        long v;
        if (sscanf(line, "VmHWM: %ld kB", &v) == 1) hwm = v;
        else if (sscanf(line, "VmRSS: %ld kB", &v) == 1) rss = v;
    }
    fclose(f);
    return hwm > 0 ? hwm : rss;
}
#else
static uint64_t readCpuTicks(void) { return 0; }
static long readPeakRssKb(void) { return 0; }
#endif

static const char *statusName(enum BenchStatus s)
{
    switch (s) {
        case BENCH_OK: return "ok";
        case BENCH_NO_FRAMES: return "no_frames";
        case BENCH_STALLED: return "stalled";
        case BENCH_SAMPLE_OVERFLOW: return "sample_overflow";
        default: return "unknown";
    }
}

static int cmpU32(const void *a, const void *b)
{
    uint32_t aa = *(const uint32_t *)a;
    uint32_t bb = *(const uint32_t *)b;
    return aa < bb ? -1 : aa > bb ? 1 : 0;
}

static uint32_t percentile(const uint32_t *a, uint32_t n, unsigned pct)
{
    uint64_t rank;
    if (!n) return 0;
    rank = ((uint64_t)n * pct + 99u) / 100u;
    if (rank < 1u) rank = 1u;
    if (rank > n) rank = n;
    return a[rank - 1u];
}

static double slowTailFps(const uint32_t *a, uint32_t n, uint32_t divisor)
{
    uint32_t count, i;
    uint64_t sum = 0;
    if (!n || !divisor) return 0.0;
    count = (n + divisor - 1u) / divisor;
    if (count < 1u) count = 1u;
    if (count > n) count = n;
    for (i = n - count; i < n; ++i) sum += a[i];
    if (!sum) return 0.0;
    return 1000000.0 / ((double)sum / (double)count);
}

static void jsonString(FILE *f, const char *s)
{
    const unsigned char *p = (const unsigned char *)(s ? s : "");
    fputc('"', f);
    for (; *p; ++p) {
        switch (*p) {
            case '\\': fputs("\\\\", f); break;
            case '"':  fputs("\\\"", f); break;
            case '\n': fputs("\\n", f); break;
            case '\r': fputs("\\r", f); break;
            case '\t': fputs("\\t", f); break;
            default:
                if (*p < 0x20) fprintf(f, "\\u%04x", (unsigned)*p);
                else fputc(*p, f);
                break;
        }
    }
    fputc('"', f);
}

void benchmarkInit(void)
{
    const char *name;
    if (!sysArgCheck("--benchmark")) return;

    g_enabled = 1;
    g_done = 0;
    g_finished = 0;
    g_status = BENCH_OK;
    g_frame_count = 0;
    g_frame_sum_us = 0;
    g_stutter_33 = g_stutter_50 = g_stutter_100 = 0;

    g_warmup_sec = parseIntArg("--benchmark-warmup", 5, 0, 120);
    g_measure_sec = parseIntArg("--benchmark-seconds", 20, 3, 600);
    g_no_frame_timeout_sec = parseIntArg("--benchmark-timeout", 60, 10, 300);
    g_stall_timeout_sec = parseIntArg("--benchmark-stall", 10, 3, 60);
    copyArg(g_name, sizeof(g_name), "--benchmark-name", "benchmark");
    copyArg(g_out, sizeof(g_out), "--benchmark-out", "ge007-benchmark.json");

#if !defined(PLATFORM_WINDOWS)
    g_clk_tck = sysconf(_SC_CLK_TCK);
    g_cpu_count = sysconf(_SC_NPROCESSORS_ONLN);
#endif
    if (g_clk_tck <= 0) g_clk_tck = 100;
    if (g_cpu_count <= 0) g_cpu_count = 1;

    g_init_us = sysGetMicroseconds();
    g_peak_rss_kb = readPeakRssKb();

    name = sysGetTokenString();
    sysLogPrintf(LOG_INFO,
        "benchmark: armed scenario='%s' warmup=%ds measure=%ds timeout=%ds args='%s'",
        g_name, g_warmup_sec, g_measure_sec, g_no_frame_timeout_sec, name);
}

void benchmarkFrame(void)
{
    uint64_t now, dt, since_first;
    uint32_t idx;

    if (!g_enabled || g_done) return;

    now = sysGetMicroseconds();
    if (!g_first_frame_us) {
        g_first_frame_us = now;
        g_last_frame_us = now;
        sysLogPrintf(LOG_INFO, "benchmark: first rendered frame; warmup started");
        return;
    }

    since_first = now - g_first_frame_us;
    if (since_first < (uint64_t)g_warmup_sec * 1000000ull) {
        g_last_frame_us = now;
        return;
    }

    if (!g_measure_start_us) {
        g_measure_start_us = now;
        g_last_frame_us = now;
        g_cpu_start_ticks = readCpuTicks();
        sysLogPrintf(LOG_INFO, "benchmark: measurement window started");
        return;
    }

    dt = now - g_last_frame_us;
    g_last_frame_us = now;
    if (dt == 0) return;
    if (dt > 0xffffffffull) dt = 0xffffffffull;

    idx = g_frame_count;
    if (idx < BENCH_MAX_FRAMES) {
        g_frame_us[idx] = (uint32_t)dt;
        g_frame_count = idx + 1u;
        g_frame_sum_us += dt;
        if (dt > 33333ull) ++g_stutter_33;
        if (dt > 50000ull) ++g_stutter_50;
        if (dt > 100000ull) ++g_stutter_100;
    } else {
        g_status = BENCH_SAMPLE_OVERFLOW;
        g_done = 1;
        return;
    }

    if (now - g_measure_start_us >= (uint64_t)g_measure_sec * 1000000ull) {
        g_done = 1;
    }
}

void benchmarkHostTick(void)
{
    uint64_t now;
    long rss;
    if (!g_enabled || g_done) return;

    rss = readPeakRssKb();
    if (rss > g_peak_rss_kb) g_peak_rss_kb = rss;

    now = sysGetMicroseconds();
    if (!g_first_frame_us) {
        if (now - g_init_us >= (uint64_t)g_no_frame_timeout_sec * 1000000ull) {
            g_status = BENCH_NO_FRAMES;
            g_done = 1;
        }
        return;
    }

    if (now - g_last_frame_us >= (uint64_t)g_stall_timeout_sec * 1000000ull) {
        g_status = BENCH_STALLED;
        g_done = 1;
    }
}

int benchmarkDone(void)
{
    return g_enabled && g_done;
}

void benchmarkFinish(void)
{
    uint32_t n, p50, p95, p99, worst;
    double measured_s, avg_fps, low1, low01;
    double cpu_pct = 0.0, cpu_norm = 0.0;
    uint64_t cpu_end_ticks;
    int win_w = 0, win_h = 0;
    FILE *f;

    if (!g_enabled || g_finished) return;
    g_finished = 1;

    n = g_frame_count;
    if (g_status == BENCH_OK && n == 0) g_status = BENCH_NO_FRAMES;

    measured_s = (g_measure_start_us && g_last_frame_us > g_measure_start_us)
        ? (double)(g_last_frame_us - g_measure_start_us) / 1000000.0 : 0.0;

    cpu_end_ticks = readCpuTicks();
    if (g_cpu_start_ticks && cpu_end_ticks >= g_cpu_start_ticks &&
        measured_s > 0.0 && g_clk_tck > 0) {
        cpu_pct = ((double)(cpu_end_ticks - g_cpu_start_ticks) /
                   (double)g_clk_tck) / measured_s * 100.0;
        cpu_norm = cpu_pct / (double)g_cpu_count;
    }

    if (n) qsort(g_frame_us, n, sizeof(g_frame_us[0]), cmpU32);

    p50 = percentile(g_frame_us, n, 50);
    p95 = percentile(g_frame_us, n, 95);
    p99 = percentile(g_frame_us, n, 99);
    worst = n ? g_frame_us[n - 1u] : 0;
    avg_fps = (n && g_frame_sum_us)
        ? 1000000.0 / ((double)g_frame_sum_us / (double)n) : 0.0;
    low1 = slowTailFps(g_frame_us, n, 100u);
    low01 = slowTailFps(g_frame_us, n, 1000u);
    videoGetWindowSize(&win_w, &win_h);

    fprintf(stderr,
        "\n========== GOLDENEYE BENCHMARK =========="
        "\nScenario        : %s"
        "\nStatus          : %s"
        "\nMeasured        : %.2f s / %u frames"
        "\nAverage FPS     : %.2f"
        "\n1%% low FPS      : %.2f"
        "\n0.1%% low FPS    : %.2f"
        "\nFrame p50       : %.3f ms"
        "\nFrame p95       : %.3f ms"
        "\nFrame p99       : %.3f ms"
        "\nWorst frame     : %.3f ms"
        "\n>33.3 ms frames : %u"
        "\n>50 ms frames   : %u"
        "\n>100 ms frames  : %u"
        "\nProcess CPU     : %.1f%% (%.1f%% of %ld cores)"
        "\nPeak RSS        : %.1f MiB"
        "\nWindow          : %dx%d"
        "\nCPU governor    : %s"
        "\nGPU governor    : %s"
        "\nSwappiness      : %d"
        "\n=========================================\n",
        g_name, statusName(g_status), measured_s, n, avg_fps, low1, low01,
        p50 / 1000.0, p95 / 1000.0, p99 / 1000.0, worst / 1000.0,
        g_stutter_33, g_stutter_50, g_stutter_100,
        cpu_pct, cpu_norm, g_cpu_count, g_peak_rss_kb / 1024.0,
        win_w, win_h, systemPerfCpuGovernor(), systemPerfGpuGovernor(),
        systemPerfSwappiness());

    /* Machine-readable single line consumed by tools_pc/ge_benchmark.sh. */
    fprintf(stderr,
        "BENCH_TSV\t%s\t%s\t%.3f\t%.3f\t%.3f\t%.3f\t%.3f\t%.3f\t%.3f\t%.1f\t%.1f\t%u\t%u\t%u\t%u\n",
        g_name, statusName(g_status), avg_fps, low1, low01,
        p50 / 1000.0, p95 / 1000.0, p99 / 1000.0, worst / 1000.0,
        cpu_pct, g_peak_rss_kb / 1024.0, n,
        g_stutter_33, g_stutter_50, g_stutter_100);

    f = fopen(g_out, "w");
    if (!f) {
        sysLogPrintf(LOG_WARNING, "benchmark: could not write %s", g_out);
        return;
    }

    fputs("{\n  \"scenario\": ", f); jsonString(f, g_name);
    fputs(",\n  \"status\": ", f); jsonString(f, statusName(g_status));
    fprintf(f,
        ",\n  \"measured_seconds\": %.6f"
        ",\n  \"frames\": %u"
        ",\n  \"fps_average\": %.6f"
        ",\n  \"fps_1_percent_low\": %.6f"
        ",\n  \"fps_0_1_percent_low\": %.6f"
        ",\n  \"frame_ms_p50\": %.6f"
        ",\n  \"frame_ms_p95\": %.6f"
        ",\n  \"frame_ms_p99\": %.6f"
        ",\n  \"frame_ms_worst\": %.6f"
        ",\n  \"stutter_over_33_3ms\": %u"
        ",\n  \"stutter_over_50ms\": %u"
        ",\n  \"stutter_over_100ms\": %u"
        ",\n  \"process_cpu_percent\": %.6f"
        ",\n  \"cpu_count\": %ld"
        ",\n  \"peak_rss_mib\": %.6f"
        ",\n  \"window_width\": %d"
        ",\n  \"window_height\": %d"
        ",\n  \"swappiness\": %d",
        measured_s, n, avg_fps, low1, low01,
        p50 / 1000.0, p95 / 1000.0, p99 / 1000.0, worst / 1000.0,
        g_stutter_33, g_stutter_50, g_stutter_100,
        cpu_pct, g_cpu_count, g_peak_rss_kb / 1024.0,
        win_w, win_h, systemPerfSwappiness());
    fputs(",\n  \"cpu_governor\": ", f); jsonString(f, systemPerfCpuGovernor());
    fputs(",\n  \"gpu_governor\": ", f); jsonString(f, systemPerfGpuGovernor());
    fputs(",\n  \"args\": ", f); jsonString(f, sysGetTokenString());
    fputs("\n}\n", f);
    fclose(f);

    sysLogPrintf(LOG_INFO, "benchmark: report written to %s", g_out);
}
