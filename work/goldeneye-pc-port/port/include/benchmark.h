#ifndef PORT_BENCHMARK_H
#define PORT_BENCHMARK_H

#ifdef __cplusplus
extern "C" {
#endif

/* Native benchmark mode. All functions are cheap no-ops unless --benchmark
 * is present on the command line. */
void benchmarkInit(void);
void benchmarkFrame(void);
void benchmarkHostTick(void);
int  benchmarkDone(void);
void benchmarkFinish(void);

#ifdef __cplusplus
}
#endif

#endif /* PORT_BENCHMARK_H */
