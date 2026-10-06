#!/usr/bin/python3
"""Drive the Ambika over MIDI with amidi (hw:1,0,0, channel 1).

usage: ambika_midi.py load_sine | load_classic | load_fm4 | note_on 57 127 |
       note_off 57 | all_off | nrpn <address> <value>
load_* send a whole 144-byte patch by sysex to part 1 (NRPN addresses are patch
byte offsets, but bytes 104-111 - feedback, engine, transpose - are not mapped).
Capture: pw-record --target <MOTU input node> --channels 20 --rate 48000
--format s32 x.wav ; the Ambika is on input 5 (channel index 4).
"""
import subprocess, sys, time
PORT = 'hw:1,0,0'
def send(*b):
    subprocess.run(['amidi', '-p', PORT, '-S', ' '.join('%02X' % x for x in b)], check=True)
def cc(c, v): send(0xB0, c, v)
def nrpn(addr, val):
    cc(99, 1 if addr & 0x80 else 0); cc(98, addr & 0x7F)
    cc(6, 1 if val & 0x80 else 0); cc(38, val & 0x7F)
    time.sleep(0.004)
def note_on(n, v=100): send(0x90, n, v)
def note_off(n): send(0x80, n, 0)
def all_off(): cc(123, 0); cc(120, 0)
def bare_sine(level=127, cutoff=127):
    """Algo 1, op1 only (sine, ratio 1.0, full level), ops 2-4 off, filter open."""
    for a, v in {0: 7, 1: 0, 2: 4, 3: 0, 4: 0, 5: 0, 6: 4, 7: 0, 8: 4, 9: 0, 10: 4, 11: 0,
                 12: level, 13: 0, 14: 0, 15: 0, 16: cutoff, 17: 0, 18: 0, 22: 0, 23: 0}.items():
        nrpn(a, v)
    # env2 (bytes 32-39): instant on, full sustain; env5-8 (extra, 112-143) same
    for base in (24, 32, 112, 120, 128, 136):
        for off, v in ((0, 0), (1, 0), (2, 127), (3, 20)):
            nrpn(base + off, v)
    # mod slot 0: env2 -> VCA full; clear slots 1-13
    nrpn(50, 1); nrpn(51, 18); nrpn(52, 63)
    for s in range(1, 14):
        nrpn(50 + 3 * s, 0); nrpn(51 + 3 * s, 0); nrpn(52 + 3 * s, 0)

def sysex(cmd, arg, data):
    """Ambika sysex object transfer: nibblised data + checksum."""
    body = []
    for b in data: body += [b >> 4, b & 15]
    cs = sum(data) & 0xFF
    send(0xF0, 0x00, 0x21, 0x02, 0x00, 0x04, cmd, arg, *body, cs >> 4, cs & 15, 0xF7)
def sine_patch(level=127, cutoff=127, ratio=4, wave=0, transpose=0):
    sys.path.insert(0, '/home/carcosa/dev/ambika')
    import make_patches as m
    p = m.make_patch(); m.set_engine(p, m.ENGINE_FM4OP)
    p[0] = 7; p[1] = 0
    for r in (2, 6, 8, 10): p[r] = ratio
    p[4] = wave; p[12] = level
    m.set_filter(p, cutoff=cutoff)
    m.set_env(p, 1, attack=0, decay=0, sustain=127, release=20)
    m.set_env(p, 3, attack=0, decay=0, sustain=127, release=20)
    m.set_mod(p, 0, m.MOD_SRC_ENV_2, m.MOD_DST_VCA, 63)
    p[107] = transpose & 0xFF
    return bytes(p)
def load_sine(level=127, cutoff=127, ratio=4, wave=0):
    sysex(0x01, 1, sine_patch(level, cutoff, ratio, wave))   # patch -> part 1
    time.sleep(0.2)
    nrpn(144, 127); nrpn(145, 0); nrpn(146, 0)                 # part volume, octave, tuning


def classic_patch(cutoff=127):
    sys.path.insert(0, '/home/carcosa/dev/ambika')
    import make_patches as m
    p = m.make_patch(); m.set_engine(p, m.ENGINE_CLASSIC)
    p[0] = m.WAVEFORM_SINE; p[2] = 0; p[4] = m.WAVEFORM_NONE; p[8] = 0
    m.set_filter(p, cutoff=cutoff)
    m.set_env(p, 1, attack=0, decay=0, sustain=127, release=20)
    m.set_mod(p, 0, m.MOD_SRC_ENV_2, m.MOD_DST_VCA, 63)
    return bytes(p)
def load_classic(cutoff=127):
    sysex(0x01, 1, classic_patch(cutoff)); time.sleep(0.2)
    nrpn(144, 127); nrpn(145, 0); nrpn(146, 0)
def fm4_patch(level=100):
    p = bytearray(sine_patch(level))
    p[1] = 7                                   # algo 8: 1+2+3+4
    p[2], p[6], p[8], p[10] = 4, 8, 13, 25     # ratios 1, 2, 3, 4  (index: 256,512,768,1024... 13=1024? check)
    for b in (12, 13, 14, 15): p[b] = level
    for e in (3, 4, 5, 6):
        import make_patches as m; m.set_env(p, e, attack=0, decay=0, sustain=127, release=20)
    return bytes(p)
def load_fm4(level=100):
    sysex(0x01, 1, fm4_patch(level)); time.sleep(0.2)
    nrpn(144, 127); nrpn(145, 0); nrpn(146, 0)

import sys
if __name__ == '__main__':
    globals()[sys.argv[1]](*[int(x) for x in sys.argv[2:]])
