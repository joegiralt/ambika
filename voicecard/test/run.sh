#!/bin/sh
# Host-side tests for voicecard DSP. Usage: sh voicecard/test/run.sh
set -e
cd "$(dirname "$0")/../.."
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
# avrlib/op.h always enables its AVR asm; use its portable C path instead
# (and fix the upstream typo in that path's U24Sub).
mkdir -p "$tmp/avrlib"
sed -e '/#define USE_OPTIMIZED_OP/d' \
    -e '/uint32_t difference = av - bv;/{n;s/sum/difference/;n;s/sum/difference/;}' \
    avrlib/op.h > "$tmp/avrlib/op.h"
g++ -std=gnu++11 -O1 -g -w -fsanitize=address,undefined -fno-sanitize-recover=undefined -DFM_NO_PHASE_DITHER -I"$tmp" -Ivoicecard/test -I. \
    voicecard/test/voice_test.cc voicecard/voice.cc voicecard/oscillator.cc voicecard/resources.cc \
    avrlib/random.cc -o "$tmp/voice_test"
"$tmp/voice_test"
