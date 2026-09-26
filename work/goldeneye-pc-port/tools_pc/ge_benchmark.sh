#!/bin/bash
# GoldenEye benchmark suite: launches real game scenarios one-by-one. Each
# invocation uses the native --benchmark mode, records real frame boundaries,
# writes JSON, closes cleanly, then advances to the next mission.
set -u

cd "$(dirname "$0")/.."

BIN="${GE_BENCH_BIN:-}"
WARMUP="${GE_BENCH_WARMUP:-5}"
SECS="${GE_BENCH_SECONDS:-20}"
TIMEOUT="${GE_BENCH_TIMEOUT:-60}"
SCENARIOS="${GE_BENCH_SCENARIOS:-Dam:33 Facility:34 Runway:35 Silo:20 Jungle:37 Control:23}"

if [ -z "$BIN" ]; then
    for candidate in ./build/arm64/ge007.aarch64 ./ge007.aarch64 ./build-pc/ge007.x86_64.exe; do
        if [ -x "$candidate" ]; then BIN="$candidate"; break; fi
    done
fi

if [ -z "$BIN" ] || [ ! -x "$BIN" ]; then
    echo "benchmark: executable not found"
    echo "set GE_BENCH_BIN=/path/to/ge007.aarch64"
    exit 2
fi

STAMP=$(date +%Y%m%d-%H%M%S)
OUTDIR="${GE_BENCH_OUT:-benchmark-results/$STAMP}"
mkdir -p "$OUTDIR"
SUMMARY="$OUTDIR/summary.tsv"
printf 'scenario\tstatus\tavg_fps\t1pct_low\t0.1pct_low\tp50_ms\tp95_ms\tp99_ms\tworst_ms\tcpu_pct\tpeak_rss_mib\tframes\tstutter33\tstutter50\tstutter100\n' > "$SUMMARY"

echo "GoldenEye benchmark suite"
echo "binary   : $BIN"
echo "warmup   : ${WARMUP}s"
echo "measure  : ${SECS}s"
echo "results  : $OUTDIR"
echo

overall=0
for entry in $SCENARIOS; do
    name=${entry%%:*}
    num=${entry##*:}
    log="$OUTDIR/${name}.log"
    json="$OUTDIR/${name}.json"

    echo "== $name (-level_$num) =="
    set +e
    "$BIN" --benchmark \
        --benchmark-name "$name" \
        --benchmark-warmup "$WARMUP" \
        --benchmark-seconds "$SECS" \
        --benchmark-timeout "$TIMEOUT" \
        --benchmark-out "$json" \
        "-level_$num" 2>&1 | tee "$log"
    rc=${PIPESTATUS[0]}
    set -e

    line=$(grep '^BENCH_TSV' "$log" | tail -1 || true)
    if [ -n "$line" ]; then
        printf '%s\n' "${line#BENCH_TSV	}" >> "$SUMMARY"
    else
        printf '%s\t%s\n' "$name" "runner_error_rc_$rc" >> "$SUMMARY"
        overall=1
    fi
    echo
done

echo "================ SUITE SUMMARY ================"
awk -F '\t' '
NR==1 {
  printf "%-12s %-15s %8s %8s %8s %8s %9s %9s\n",
         "scenario","status","avg","1% low","p99 ms","CPU %","RSS MiB","frames";
  next
}
{
  printf "%-12s %-15s %8s %8s %8s %8s %9s %9s\n",
         $1,$2,$3,$4,$8,$10,$11,$12
}' "$SUMMARY"
echo "================================================"
echo "TSV : $SUMMARY"
echo "JSON: $OUTDIR/*.json"

exit "$overall"
