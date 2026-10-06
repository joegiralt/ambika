#!/bin/sh
# Host-side analysis for the voicecard engines.
#
#   sh voicecard/test/analyze.sh noise [-DFM_PHASE_DITHER]
#       Spur and SNR of one sine operator, measured with coherent sampling.
#
#   sh voicecard/test/analyze.sh render OUT.wav PATCH.PAT NOTE [PATCH NOTE ...]
#       Render real factory patches to a WAV through Voice::ProcessBlock.
#       Patch files are the .PAT written by make_patches.py.
#
# Both build the real firmware sources, so what you measure is what runs on
# the ATmega328p - minus the analog filter, VCA and output stage, which are
# hardware and are not modelled.
set -e
cd "$(dirname "$0")/../.."
mode=$1; shift
tmp=$(mktemp -d); trap 'rm -rf "$tmp"' EXIT
# avrlib/op.h always enables its AVR asm; use its portable C path instead
# (and fix the upstream typo in that path's U24Sub).
mkdir -p "$tmp/avrlib"
sed -e '/#define USE_OPTIMIZED_OP/d' \
    -e '/uint32_t difference = av - bv;/{n;s/sum/difference/;n;s/sum/difference/;}' \
    avrlib/op.h > "$tmp/avrlib/op.h"
case "$mode" in
  noise)  src=voicecard/test/fm_noise.cc;    extra="$*"; set -- ;;
  render) src=voicecard/test/render_patch.cc; extra= ;;
  *) echo "usage: $0 noise|render ..." >&2; exit 2 ;;
esac
g++ -std=gnu++11 -O2 -w $extra -I"$tmp" -Ivoicecard/test -I. \
    "$src" voicecard/voice.cc voicecard/oscillator.cc voicecard/resources.cc \
    avrlib/random.cc -o "$tmp/a"
"$tmp/a" "$@"
