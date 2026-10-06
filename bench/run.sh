#!/bin/sh
# usage: sh bench/run.sh [patch_sine|patch_classic|patch_fm4|patch_lately|patch_dyno] [note] [extra -D...]
# Builds the bench in docker (image avr-bench), runs it in simavr, prints cycles per 40-sample block.
set -e
P=${1:-patch_sine}; N=${2:-57}; X=${3:-}; B=${BUILD_ROOT:-build/}
docker run --rm -v "$PWD":/w -w /w avr-bench sh -c "
  mkdir -p $B && rm -rf ${B}bench && make -s -f bench/makefile AVRLIB_TOOLS_PATH=/usr/bin/ BUILD_ROOT=$B BENCH_DEFINES='-DBENCH_PATCH=$P -DBENCH_NOTE=$N $X' ${B}bench/bench.elf >/dev/null 2>${B}bench_err.txt || { cat ${B}bench_err.txt; exit 1; }
  simavr -m atmega328p -f 20000000 -g ${B}bench/bench.elf >/dev/null 2>&1 &
  sleep 1
  timeout -s INT 60 avr-gdb -q -batch -ex 'target remote :1234' -ex 'break bench_done' -ex 'continue' -ex 'print/d done' -ex 'print/d dbg_vca' -ex 'print/d dbg_env2' -ex 'print/d dbg_engine' -ex 'print/d dbg_rx' -ex 'print/d isr_count' -ex 'print/d underruns' -ex 'bt 6' -ex 'print/d cycles' -ex 'print/d isr_per_block' -ex 'print/d marks' -ex 'print/d optest_cases' -ex 'print/d optest_mismatches' -ex 'print/d optest_first' -ex 'print/d fmtest_blocks' -ex 'print/d fmtest_mismatches' -ex 'print/d fmtest_first' ${B}bench/bench.elf 2>&1 | grep -v '^Reading\|^Remote\|^0x\|^Breakpoint 1 at' ; true
  kill %1 2>/dev/null; true
  chown -R $(id -u):$(id -g) $B
"
