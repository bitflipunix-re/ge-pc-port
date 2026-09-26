# GoldenEye benchmark harness

The port has a native benchmark mode intended for R36S/ARM64 performance work.
It runs the real game and renderer; it does not substitute a synthetic render
loop.

## One scenario

```sh
./ge007.aarch64 --benchmark --benchmark-name Dam \
  --benchmark-warmup 5 --benchmark-seconds 20 \
  --benchmark-out dam.json -level_33
```

The timer begins at the first delivered frame. The warmup interval absorbs
stage startup, shader/texture cache population and early transient work. After
warmup, every delivered frame interval is recorded. When the measurement
window finishes, the executable prints the report, writes JSON, destroys the
game subsystems normally, saves config/window state and exits.

Metrics include average FPS, 1% and 0.1% low FPS (mean of the slowest frame
tails), p50/p95/p99 and worst frametime, three stutter counters, process CPU
usage, peak RSS, CPU/GPU governor state, swappiness and output window size.

A startup no-frame watchdog and a mid-run stall watchdog prevent a broken level
from leaving the benchmark open forever.

## Suite

```sh
./tools_pc/ge_benchmark.sh
```

Default scenarios cover Dam, Facility, Runway, Silo, Jungle and Control. Each
mission is a fresh process, so one scenario cannot contaminate the next with
level state. Reports go to `benchmark-results/<timestamp>/`.

Useful overrides:

```sh
GE_BENCH_BIN=./build/arm64/ge007.aarch64 \
GE_BENCH_WARMUP=8 \
GE_BENCH_SECONDS=30 \
GE_BENCH_SCENARIOS="Dam:33 Silo:20 Control:23" \
./tools_pc/ge_benchmark.sh
```

For comparable R36S runs, keep the same firmware, governor preset, resolution,
MSAA/render scale, thermal state and benchmark durations. The report records
the live governors to make accidental configuration differences visible.
