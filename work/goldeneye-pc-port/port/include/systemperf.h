#ifndef PORT_SYSTEMPERF_H
#define PORT_SYSTEMPERF_H

#ifdef __cplusplus
extern "C" {
#endif

/* Read-only live telemetry. Returned strings remain valid until next call. */
const char *systemPerfCpuGovernor(void);
const char *systemPerfGpuGovernor(void);
int         systemPerfSwappiness(void);

/* Best-effort in-process allocator trim. Returns non-zero when memory was released. */
int         systemPerfTrimMemory(void);

/* Human-readable status for requested per-game tuning. */
const char *systemPerfTuneStatus(void);

#ifdef __cplusplus
}
#endif
#endif
