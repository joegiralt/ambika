#!/usr/bin/env python3
"""Generate Carcosa factory patches for the Ambika polysynth."""

import struct
import os
import sys

# Carcosa v2.0 Patch layout (144 bytes)
PATCH_SIZE = 144

# Waveform enums
WAVEFORM_NONE = 0
WAVEFORM_SAW = 1
WAVEFORM_SQUARE = 2
WAVEFORM_TRIANGLE = 3
WAVEFORM_SINE = 4
WAVEFORM_QUAD_SAW_PAD = 5
WAVEFORM_FILTERED_NOISE = 6
WAVEFORM_FM4OP = 7
WAVEFORM_KS_PLUCK = 8
WAVEFORM_WAVESHAPE = 9
WAVEFORM_WESTCOAST = 10

# FM waveforms (TX81Z W1-W8)

# Mod sources (Carcosa v2.0)
MOD_SRC_ENV_1 = 0
MOD_SRC_ENV_2 = 1
MOD_SRC_ENV_3 = 2
MOD_SRC_LFO_4 = 10
MOD_SRC_VELOCITY = 16
MOD_SRC_PITCH_BEND = 18
MOD_SRC_WHEEL = 19
MOD_SRC_NOTE = 22

# Mod destinations
MOD_DST_PARAMETER_1 = 0
MOD_DST_OSC_1_2_COARSE = 4
MOD_DST_OSC_1_2_FINE = 5
MOD_DST_OP_LEVEL = {1: 9, 2: 8, 3: 10, 4: 11}  # FM mode: mixer slots
MOD_DST_FILTER_CUTOFF = 12
MOD_DST_VCA = 18

# Envelope curve
ENV_EXP = 0
ENV_LIN = 1
LFO_TRI = 0


def make_patch():
    """Return a zeroed 144-byte patch."""
    return bytearray(PATCH_SIZE)


ENGINE_CLASSIC = 0
ENGINE_FM4OP = 1
ENGINE_KS_PLUCK = 2
ENGINE_WESTCOAST = 3

def set_engine(patch, engine):
    """Set the synthesis engine type at padding[2] (offset 106)."""
    patch[106] = engine


def set_filter(patch, cutoff=127, resonance=0, mode=0, env_amt=0, lfo_amt=0):
    """Set filter parameters."""
    patch[16] = cutoff
    patch[17] = resonance
    patch[18] = mode
    patch[22] = env_amt
    patch[23] = lfo_amt


def set_env(patch, env_idx, attack=0, decay=40, sustain=20, release=60,
            lfo_shape=LFO_TRI, lfo_rate=0, curve=ENV_EXP, retrigger=0):
    """Set envelope/LFO parameters. env_idx 0-2 = env_lfo, 3-6 = extra."""
    if env_idx < 3:
        base = 24 + env_idx * 8
    else:
        base = 112 + (env_idx - 3) * 8
    patch[base] = attack
    patch[base + 1] = decay
    patch[base + 2] = sustain
    patch[base + 3] = release
    patch[base + 4] = lfo_shape
    patch[base + 5] = lfo_rate
    patch[base + 6] = curve
    patch[base + 7] = retrigger


def set_mod(patch, slot, source, destination, amount):
    """Set a modulation matrix slot (0-13)."""
    base = 50 + slot * 3
    patch[base] = source
    patch[base + 1] = destination
    patch[base + 2] = amount


def make_riff_patch(patch_data, name):
    """Wrap patch data in Ambika RIFF format."""
    name_bytes = name.encode('ascii')[:16].ljust(16, b'\x00')
    name_chunk = struct.pack('<4sI', b'name', 16) + name_bytes
    position = struct.pack('<BBBB', 1, 0, 0, 0)
    obj_chunk = struct.pack('<4sI', b'obj ', len(patch_data) + 4) + position + patch_data
    body = b'MBKS' + name_chunk + obj_chunk
    return struct.pack('<4sI', b'RIFF', len(body)) + body


# Part settings for generated programs, copied from a stock Ambika factory
# program. A program is a patch plus the part settings it is played with.
PART_DATA = bytes.fromhex('78ff0000000000000001000a0100000000000000')


def make_riff_program(patch_data, part_data, name):
    """Wrap patch + part data in Ambika RIFF program format.

    Byte 0 of a chunk's position word is the wire object id: 1 is a patch and
    3 is a part (2 is the removed sequence slot, which Storage::RIFFWriteObject
    shifts past). Storage::ForEachObject writes a program as patch then part.
    """
    name_bytes = name.encode('ascii')[:16].ljust(16, b'\x00')
    name_chunk = struct.pack('<4sI', b'name', 16) + name_bytes
    patch_chunk = (struct.pack('<4sI', b'obj ', len(patch_data) + 4)
                   + struct.pack('<BBBB', 1, 0, 0, 0) + patch_data)
    part_chunk = (struct.pack('<4sI', b'obj ', len(part_data) + 4)
                  + struct.pack('<BBBB', 3, 0, 0, 0) + part_data)
    body = b'MBKS' + name_chunk + patch_chunk + part_chunk
    return struct.pack('<4sI', b'RIFF', len(body)) + body


def save_patch(outdir, bank, slot, name, patch_data):
    """Write the patch to /PATCH/BANK and the same sound to /PROGRAM/BANK.

    The library page opens on programs - Library::location_ is initialised to
    STORAGE_OBJECT_PROGRAM - so content shipped only as patches does not show
    up until the user presses S1 to change what is browsed. Ship both.
    """
    bank_dir = os.path.join(outdir, 'PATCH', 'BANK', bank)
    os.makedirs(bank_dir, exist_ok=True)
    with open(os.path.join(bank_dir, f'{slot:03d}.PAT'), 'wb') as f:
        f.write(make_riff_patch(patch_data, name))

    prog_dir = os.path.join(outdir, 'PROGRAM', 'BANK', bank)
    os.makedirs(prog_dir, exist_ok=True)
    with open(os.path.join(prog_dir, f'{slot:03d}.PRO'), 'wb') as f:
        f.write(make_riff_program(patch_data, PART_DATA, name))

    print(f'  {bank}{slot:02d} - {name.strip()}')


# --- FM Patches: TX81Z factory voices ---
#
# The TX81Z's 128 ROM voices (tx81z_factory.py) converted to the FM engine.
# Algorithm, feedback, waveforms, ratios and levels map 1:1 onto the engine,
# which matches the TX81Z's operator path. Envelopes (timed like ymfm's OPZ,
# rate scaling fixed at middle C), LFO, keyboard level scaling and
# per-operator velocity are approximated with Carcosa's ADSRs, voice LFO and
# note/velocity mod-matrix routes to operator levels. Not supported:
# fixed-frequency operators (they track pitch here) and LFO amplitude
# modulation.

HERE = os.path.dirname(os.path.abspath(__file__))
CONTROL_RATE = 20e6 / 510 / 40  # voicecard blocks per second


def read_table(path, name):
    """Numbers of a C table `name` in a firmware source file."""
    import re
    src = open(os.path.join(HERE, path)).read()
    body = src[src.index(name):]
    body = body[body.index('{') + 1:body.index('};')]
    body = re.sub(r'//.*', '', body)
    return [int(x) for x in re.findall(r'\d+', body)]


TX81Z_RATIOS = [r / 256 for r in read_table('voicecard/voice.cc', 'tx81z_ratios_[]')]
ENV_INCREMENTS = read_table('voicecard/resources.cc', 'lut_res_env_portamento_increments[]')
LFO_INCREMENTS = read_table('voicecard/resources.cc', 'lut_res_lfo_increments[]')


def nearest(values, target):
    """Index of the value closest to target, in log terms."""
    import math
    return min(range(len(values)),
               key=lambda i: abs(math.log(max(values[i], 1e-9) / target)))


def env_value(seconds):
    """Carcosa envelope stage value (0-127) lasting about `seconds`."""
    stage = [65536 / inc / CONTROL_RATE for inc in ENV_INCREMENTS]
    return nearest(stage, max(seconds, 1e-4))


# OPZ envelope increments per rate (ymfm's attenuation_increment table).
EG_INCREMENTS = ([0, 0, 0x10101010, 0x10101010, 0x10101010, 0x10101010,
                  0x11101110, 0x11101110] +
                 [0x10101010, 0x10111010, 0x11101110, 0x11111110] * 10 +
                 [0x11111111, 0x21112111, 0x21212121, 0x22212221,
                  0x22222222, 0x42224222, 0x42424242, 0x44424442,
                  0x44444444, 0x84448444, 0x84848484, 0x88848884] +
                 [0x88888888] * 4)
OPZ_EG_TICK = 3 * 64 / 3579545  # the EG clocks every 3 samples at 55.9 kHz


def eg_ticks(rate, attack):
    """EG clocks for a full attack (96 dB up) or decay (96 dB down), as ymfm
    clocks the OPZ envelope."""
    att, tick = 1023 if attack else 0, 0
    shift = rate >> 2
    while (att > 0) if attack else (att < 1023):
        tick += 1
        counter = tick << shift
        if counter & 0x7FF:
            continue
        inc = (EG_INCREMENTS[rate] >> (4 * ((counter >> max(11, shift)) & 7))) & 15
        if attack:
            att -= ((att + 1) * inc + 15) >> 4
        else:
            att += inc
    return tick


def lfo_value(hz):
    """Voice LFO rate (0-127) closest to `hz`."""
    return nearest([inc * CONTROL_RATE / 65536 for inc in LFO_INCREMENTS], hz)


def eg_seconds(rate, attack=False):
    """Seconds for a full OPZ attack or 96 dB decay at effective rate 0-63."""
    rate = min(rate, 63)
    if rate < 2:
        return 1000.0
    return eg_ticks(rate, attack) * OPZ_EG_TICK


# Rate scaling: patches aren't per key, so envelopes are timed for the note
# that sounds middle C (OPZ keycode 15).
EG_KEYCODE = (60 - 13) // 3


def eg_rate(raw, rs):
    """Effective OPZ rate: raw rate plus key rate scaling (RS 0-3)."""
    return 0 if raw == 0 else raw + (EG_KEYCODE >> (rs ^ 3))


# Modulator level trim in 0.75 dB steps (see tx81z_to_patch).
MOD_TRIM = 6

# Keyboard level scaling starts at this MIDI note (see tx81z_to_patch).
LS_BREAKPOINT = 36

# Operators that reach the output, per algorithm (panel numbers).
CARRIERS = [[1], [1], [1], [1], [1, 3], [1, 2, 3], [1, 2, 3], [1, 2, 3, 4]]
# TX81Z LFO wave (saw up, square, triangle, S/H) -> Carcosa LFO shape.
LFO_SHAPE = [3, 1, 0, 2]
# TX81Z pitch modulation sensitivity 0-7, in cents at full depth.
PMS_CENTS = [0, 5, 10, 20, 50, 100, 400, 700]


def tx81z_to_patch(rom):
    """Convert a 78-byte TX81Z ROM voice record to a Carcosa FM patch."""
    import math
    v = list(rom[:67]) + [99, 99, 99, 50, 50, 50] + list(rom[67:])
    p = make_patch()
    set_engine(p, ENGINE_FM4OP)
    alg = v[40] & 7
    p[1] = alg
    p[104] = ((v[40] >> 3) & 7) * 16          # feedback: knob 16 per FB step
    p[107] = (v[46] - 24) & 0xFF               # transpose (padding[3])

    # Bulk-dump operator order is OP4, OP2, OP3, OP1.
    offsets = {4: 0, 2: 10, 3: 20, 1: 30}
    extras = {4: 73, 2: 75, 3: 77, 1: 79}
    # Patch bytes per panel operator: (ratio, fine, level); waves are nibbles.
    fields = {1: (2, 3, 12), 2: (6, 7, 13), 3: (8, 9, 14), 4: (10, 11, 15)}
    releases = {}
    for op in (1, 2, 3, 4):
        o = v[offsets[op]:offsets[op] + 10]
        ar, d1r, d2r, rr, d1l, out, crs = o[0], o[1], o[2], o[3], o[4], o[7], o[8]
        det = o[9] & 7
        rs = (o[9] >> 3) & 3
        fine = v[extras[op] + 1] & 15
        wave = (v[extras[op] + 1] >> 4) & 7

        ratio = TX81Z_RATIOS[crs] * (1 + fine / 16)
        idx = nearest(TX81Z_RATIOS, ratio)
        detune = round(256 * (ratio / TX81Z_RATIOS[idx] - 1)) + int((det - 3) / 2)
        r_byte, f_byte, l_byte = fields[op]
        p[r_byte] = idx
        p[f_byte] = max(-64, min(63, detune)) & 0xFF
        p[l_byte] = 0 if (out == 0 or ar == 0) else min(127, out + 28)
        if op == 1:
            p[4] = (p[4] & 0xF0) | wave
        elif op == 2:
            p[4] = (p[4] & 0x0F) | (wave << 4)
        elif op == 3:
            p[5] = (p[5] & 0xF0) | wave
        else:
            p[5] = (p[5] & 0x0F) | (wave << 4)

        attack = env_value(eg_seconds(eg_rate(2 * ar, rs), attack=True))
        drop_db = (15 - d1l) * 3
        if d1r and drop_db:
            decay = env_value(eg_seconds(eg_rate(2 * d1r, rs)) * drop_db / 96)
            sustain = round(127 * 10 ** (-drop_db / 20))
        elif d2r:
            decay, sustain = env_value(eg_seconds(eg_rate(2 * d2r, rs)) / 2), 0
        else:
            decay, sustain = 0, 127
        release = env_value(eg_seconds(eg_rate(4 * rr + 2, rs)) / 2)
        releases[op] = release
        # Keyboard level scaling: the operator gets quieter up the keyboard,
        # from full level at LS_BREAKPOINT. The note source is centered on E4
        # and amount -63 is about -9 dB per octave, so the stored level is
        # lowered to read OUT at the breakpoint. ponytail: linear guess at the
        # TX81Z's LS curve, which is undocumented; tune by ear.
        ls = o[5]
        if ls and p[l_byte]:
            amount = round(63 * ls / 99)
            drop = round(amount * (64 - LS_BREAKPOINT) / 64)
            p[l_byte] = max(1, p[l_byte] - drop)
            set_mod(p, 3 + op, MOD_SRC_NOTE, MOD_DST_OP_LEVEL[op],
                    -amount & 0xFF)
        # Key velocity sensitivity: softer notes turn the operator down,
        # about 5.2 dB per KVS step at velocity 0 (fit to the SOUL TX81Z
        # model's curves). Velocity reaches 254, so full velocity reads OUT.
        kvs = o[6] & 7
        if kvs and p[l_byte]:
            amount = round(3.5 * kvs)
            p[l_byte] = max(1, p[l_byte] - round(amount * 254 / 128))
            set_mod(p, 7 + op, MOD_SRC_VELOCITY, MOD_DST_OP_LEVEL[op], amount)
        set_env(p, 2 + op, attack=attack, decay=decay, sustain=sustain,
                release=release)

    # The carriers' envelopes shape the sound; the VCA just gates it.
    carriers = CARRIERS[alg]
    # Voicing choice, not the TX81Z: modulators a little softer, since the
    # voices sound harsh without the TX81Z's output stage. 0 = exact levels.
    for op in (1, 2, 3, 4):
        if op not in carriers and p[fields[op][2]]:
            p[fields[op][2]] = max(1, p[fields[op][2]] - MOD_TRIM)
    set_env(p, 1, attack=0, decay=0, sustain=127,
            release=max(releases[op] for op in carriers))
    set_filter(p, cutoff=127)
    set_mod(p, 0, MOD_SRC_ENV_2, MOD_DST_VCA, 63)
    set_mod(p, 2, MOD_SRC_PITCH_BEND, MOD_DST_OSC_1_2_COARSE, 32)

    # Vibrato: voice LFO -> pitch.
    cents = PMS_CENTS[(v[45] >> 4) & 7] * v[43] / 99
    if cents >= 1:
        hz = 0.06 * 2 ** (v[41] / 10)
        p[48] = LFO_SHAPE[v[45] & 3]
        p[49] = lfo_value(hz)
        if cents <= 50:
            set_mod(p, 3, MOD_SRC_LFO_4, MOD_DST_OSC_1_2_FINE,
                    min(63, round(63 * cents / 50)))
        else:
            set_mod(p, 3, MOD_SRC_LFO_4, MOD_DST_OSC_1_2_COARSE,
                    min(63, round(63 * cents / 400)))
    return p


def tx81z_voices():
    """(slot, name, patch) for the 128 TX81Z factory voices."""
    sys.path.insert(0, HERE)
    import tx81z_factory
    for slot, data in tx81z_factory.VOICES:
        rom = bytes.fromhex(data)
        yield slot, rom[57:67].decode('ascii'), tx81z_to_patch(rom)


# Bank C: TX81Z voices standing in for the classic FM sounds.
BANK_C = ['C15', 'A11', 'B02', 'C27', 'B26', 'A18']


# --- Banks M and I: movement ---
#
# Patches that use the voicecard's own modulators (voice LFO, looping
# envelopes, velocity, note, random) to move the FM, Karplus-Strong and West
# Coast engines: bold in Bank M, understated and IDM-ish in Bank I. The part
# LFOs 1-3 are left free for the player.

ENV_LOOP = 2          # looping envelope curves
ENV_LOOP_LINEAR = 3
LFO_TRI, LFO_SQUARE, LFO_SH, LFO_RAMP = 0, 1, 2, 3
SRC_ENV_1, SRC_ENV_3 = 0, 2
SRC_NOTE, SRC_RANDOM = 22, 25
DST = dict(param1=0, param2=1, osc1=2, fine=5, balance=6, mix_param=7,
           noise=8, sub=9, fuzz=10, crush=11)
FM_OP = {1: 'sub', 2: 'noise', 3: 'fuzz', 4: 'crush'}  # op level destinations


def loop_env(p, idx, period, depth=0, linear=True):
    """Envelope idx as an LFO: rises and falls once per `period` seconds,
    between full and `depth` (0-127)."""
    half = env_value(period / 2)
    set_env(p, idx, attack=half, decay=half, sustain=depth, release=half,
            curve=ENV_LOOP_LINEAR if linear else ENV_LOOP)


def voice_lfo(p, shape, hz):
    p[48], p[49] = shape, lfo_value(hz)


def mod(p, slot, source, dst, amount):
    set_mod(p, slot, source, DST[dst], amount & 0xFF)


def ks(damping, color, exc=0, position=30, decay=0, body=0,
       ens=(0, 0, 0, 0), stiff=0, sustain=0, release=40):
    p = make_patch()
    set_engine(p, ENGINE_KS_PLUCK)
    # Softer than the raw settings: a little more damping, less bright pluck.
    damping, color = min(127, damping + 10), round(color * 0.85)
    p[1], p[4], p[5], p[6], p[7] = damping, exc, color, decay, position
    p[8], (p[9], p[10], p[11], p[12]) = body, ens
    p[13], p[14] = stiff, sustain
    set_env(p, 1, attack=0, decay=0, sustain=127, release=release)
    set_mod(p, 0, MOD_SRC_ENV_2, MOD_DST_VCA, 63)
    set_filter(p, cutoff=127)
    return p


def wc(fold, color, env_fold=0, fm_depth=0, fm_ratio=2, sub=0, sync=0,
       bias=64, sym=64, wave=0, fold_decay=30, attack=0, release=30):
    p = make_patch()
    set_engine(p, ENGINE_WESTCOAST)
    # Softer than the raw settings: less fold and a darker color filter.
    fold, color = round(fold * 0.8), round(color * 0.8)
    p[1], p[4], p[5], p[6], p[7] = fold, wave, sym, fm_depth, fm_ratio & 0xFF
    p[8], p[10], p[13], p[14], p[15] = bias, color, env_fold, sub, sync
    set_env(p, 0, attack=0, decay=fold_decay, sustain=0, release=20)
    set_env(p, 1, attack=attack, decay=0, sustain=127, release=release)
    set_mod(p, 0, MOD_SRC_ENV_2, MOD_DST_VCA, 63)
    set_filter(p, cutoff=127)
    return p


def bank_m(voices):
    """(name, patch) for the 24 movement patches."""
    out = []

    def fm(slot, name):
        p = bytearray(voices[slot][1])
        out.append((name, p))
        return p

    # FM: TX81Z voices, moved by looping envelopes 1 and 3 (slots 1 and 12
    # are free in converted voices; envelopes 4-7 belong to the operators).
    p = fm('C15', 'FM WahBass')             # op2 brightness dips in 8ths
    loop_env(p, 0, 0.26)
    mod(p, 1, SRC_ENV_1, FM_OP[2], -10)
    p = fm('A11', 'FM TineDrift')           # the tine swells and fades
    loop_env(p, 0, 3.0)
    mod(p, 1, SRC_ENV_1, FM_OP[4], 10)
    loop_env(p, 2, 5.0)
    mod(p, 12, SRC_ENV_3, FM_OP[2], -6)
    p = fm('B02', 'FM BrassSwell')          # brightness swells in
    set_env(p, 0, attack=env_value(1.2), decay=0, sustain=127, release=30)
    p[15] = max(1, p[15] - 24)
    mod(p, 1, SRC_ENV_1, FM_OP[4], 12)
    p = fm('B26', 'FM StringMorph')         # two slow, unsynced timbre loops
    loop_env(p, 0, 4.0)
    mod(p, 1, SRC_ENV_1, FM_OP[3], 16)
    loop_env(p, 2, 6.5)
    mod(p, 12, SRC_ENV_3, FM_OP[4], -12)
    p = fm('D26', 'FM BellRandom')          # every strike a different bell
    mod(p, 1, SRC_RANDOM, 'mix_param', 3)
    p = fm('A18', 'FM Leslie')              # tremolo on two drawbars + vibrato
    loop_env(p, 0, 1 / 6.2)
    mod(p, 1, SRC_ENV_1, FM_OP[2], -14)
    loop_env(p, 2, 1 / 5.1)
    mod(p, 12, SRC_ENV_3, FM_OP[3], -14)
    voice_lfo(p, LFO_TRI, 6.2)
    set_mod(p, 3, MOD_SRC_LFO_4, MOD_DST_OSC_1_2_FINE, 4)
    p = fm('C22', 'FM SyncSweep')           # slow sync-style sweep
    loop_env(p, 0, 2.0)
    mod(p, 1, SRC_ENV_1, FM_OP[2], 12)
    p = fm('C30', 'FM PulsePad')            # rhythmic brightness pulses
    set_env(p, 0, attack=0, decay=env_value(0.25), sustain=0,
            release=10, curve=ENV_LOOP)
    mod(p, 1, SRC_ENV_1, FM_OP[4], 14)

    # Karplus-Strong: envelopes 1 and 3-7 and the voice LFO are free.
    p = ks(damping=25, color=100, position=64, body=40, ens=(20, 60, 40, 90))
    voice_lfo(p, LFO_TRI, 0.5)              # color drifts through metallic
    mod(p, 1, 10, 'param2', 28)
    out.append(('KS ShimmerHarp', p))
    p = ks(damping=20, color=80, sustain=100)
    loop_env(p, 0, 0.8)                     # damping opens and closes
    mod(p, 1, SRC_ENV_1, 'param1', 20)
    out.append(('KS TalkString', p))
    p = ks(damping=30, color=110, exc=2, position=20, stiff=20)
    set_env(p, 2, attack=0, decay=env_value(0.08), sustain=0, release=0)
    mod(p, 1, SRC_ENV_3, 'osc1', 12)        # pitch falls into the note
    out.append(('KS PitchDrop', p))
    p = ks(damping=35, color=80, ens=(20, 30, 40, 0), sustain=60)
    set_env(p, 2, attack=env_value(2.0), decay=0, sustain=127, release=40)
    mod(p, 1, SRC_ENV_3, 'sub', 25)         # chorus fades in
    mod(p, 4, SRC_ENV_3, 'mix_param', 20)
    out.append(('KS ChorusSwell', p))
    p = ks(damping=15, color=105, position=40, sustain=80)
    set_env(p, 2, attack=env_value(0.3), decay=env_value(0.6), sustain=40,
            release=20)
    mod(p, 1, SRC_ENV_3, 'osc1', 4)         # bends up after the pluck
    voice_lfo(p, LFO_TRI, 5.0)
    mod(p, 4, 10, 'osc1', 1)                # and a little vibrato
    out.append(('KS KotoBend', p))
    p = ks(damping=30, color=90)
    mod(p, 1, SRC_RANDOM, 'param1', 10)     # every pluck differs
    mod(p, 4, SRC_RANDOM, 'balance', 20)
    mod(p, 5, SRC_RANDOM, 'param2', 15)
    out.append(('KS RandomPluck', p))
    p = ks(damping=50, color=90, exc=2, stiff=30, sustain=40)
    mod(p, 1, MOD_SRC_VELOCITY, 'param1', -12)  # harder = brighter
    mod(p, 4, MOD_SRC_VELOCITY, 'param2', 15)
    out.append(('KS VeloSteel', p))
    p = ks(damping=10, color=120, stiff=40, sustain=120, body=40)
    voice_lfo(p, LFO_TRI, 0.2)              # body breathes
    mod(p, 1, 10, 'balance', 40)
    loop_env(p, 0, 3.0)                     # stiffness drifts
    mod(p, 4, SRC_ENV_1, 'noise', 20)
    out.append(('KS SitarBreath', p))

    # West Coast: envelope 1 is the fold envelope; 3-7 and the LFO are free.
    p = wc(fold=30, color=100, attack=env_value(0.6), release=60)
    voice_lfo(p, LFO_TRI, 0.3)
    mod(p, 1, 10, 'param1', 30)
    out.append(('WC FoldPad', p))
    p = wc(fold=70, color=110)
    voice_lfo(p, LFO_TRI, 3.0)
    mod(p, 1, 10, 'param2', 60)
    out.append(('WC SymWobble', p))
    p = wc(fold=70, color=110, bias=20)
    loop_env(p, 2, 2.0)
    mod(p, 1, SRC_ENV_3, 'balance', 45)
    out.append(('WC BiasSweep', p))
    p = wc(fold=10, color=90, env_fold=90, fold_decay=15, sub=40, release=10)
    set_env(p, 1, attack=0, decay=env_value(0.25), sustain=0, release=10)
    set_env(p, 2, attack=0, decay=env_value(0.06), sustain=0, release=0)
    mod(p, 1, SRC_ENV_3, 'osc1', 10)        # pitch drop into the hit
    out.append(('WC Bongo', p))
    p = wc(fold=60, color=30)
    loop_env(p, 2, 0.5)
    mod(p, 1, SRC_ENV_3, 'mix_param', 45)
    out.append(('WC ColorWah', p))
    p = wc(fold=40, color=110, sync=50)
    voice_lfo(p, LFO_TRI, 1.5)
    mod(p, 1, 10, 'crush', 40)
    out.append(('WC SyncGrowl', p))
    p = wc(fold=45, color=110)
    voice_lfo(p, LFO_SH, 6.0)
    mod(p, 1, 10, 'param1', 40)
    out.append(('WC S&H Fold', p))
    p = wc(fold=10, color=80, env_fold=40)
    mod(p, 1, MOD_SRC_VELOCITY, 'param1', 20)
    mod(p, 4, SRC_NOTE, 'mix_param', 20)
    out.append(('WC VeloFolder', p))
    return out


def bank_i(voices):
    """(name, patch) for 24 understated, IDM-ish patches: small motion
    (drift, per-note variation, slow unsynced loops) rather than sweeps."""
    out = []

    def fm(slot, name):
        p = bytearray(voices[slot][1])
        out.append((name, p))
        return p

    # FM: TX81Z voices, moved by looping envelopes 1 and 3 and random
    # (slots 1 and 12 are free in converted voices; envelopes 4-7 belong to
    # the operators, and the voice LFO often carries the TX81Z's vibrato).
    p = fm('A11', 'FM GlassDrift')          # tape wow and a breathing tine
    loop_env(p, 0, 7.0, linear=False)
    mod(p, 1, SRC_ENV_1, 'fine', 2)
    loop_env(p, 2, 4.3)
    mod(p, 12, SRC_ENV_3, FM_OP[4], 5)
    p = fm('D26', 'FM RandBell')            # each strike a slightly other bell
    mod(p, 1, SRC_RANDOM, 'mix_param', 1)
    mod(p, 12, SRC_RANDOM, FM_OP[4], -4)
    p = fm('C15', 'FM SoftBass')            # a faint dotted-eighth pulse
    loop_env(p, 0, 0.39)
    mod(p, 1, SRC_ENV_1, FM_OP[2], -4)
    p = fm('D04', 'FM Kalimba')             # every note a little different
    mod(p, 1, SRC_RANDOM, FM_OP[2], -5)
    mod(p, 12, SRC_RANDOM, FM_OP[3], -5)
    p = fm('B26', 'FM HazePad')             # slow, unsynced drift
    loop_env(p, 0, 5.3)
    mod(p, 1, SRC_ENV_1, FM_OP[3], 6)
    loop_env(p, 2, 8.1, linear=False)
    mod(p, 12, SRC_ENV_3, 'fine', 2)
    p = fm('C27', 'FM Chime')               # brightness varies per note
    mod(p, 1, SRC_RANDOM, FM_OP[2], -6)
    mod(p, 12, SRC_RANDOM, FM_OP[4], -6)
    p = fm('C22', 'FM Blip')                # short, with a random edge
    set_env(p, 1, attack=0, decay=env_value(0.12), sustain=0, release=5)
    mod(p, 1, SRC_RANDOM, FM_OP[2], -8)
    p = fm('C30', 'FM WowPad')              # bell pad on a warped tape
    loop_env(p, 0, 3.1, linear=False)
    mod(p, 1, SRC_ENV_1, 'fine', 3)
    loop_env(p, 2, 6.0)
    mod(p, 12, SRC_ENV_3, FM_OP[4], 5)

    # Karplus-Strong: envelopes 1 and 3-7 and the voice LFO are free.
    p = ks(damping=12, color=110, exc=2, position=20)
    mod(p, 1, SRC_RANDOM, 'param2', 8)      # a glassy ping, varied
    out.append(('KS Pling', p))
    p = ks(damping=35, color=80, sustain=40)
    loop_env(p, 0, 3.7, linear=False)       # slow tape wow
    mod(p, 1, SRC_ENV_1, 'osc1', 1)
    out.append(('KS TapeNylon', p))
    p = ks(damping=30, color=90, body=20)
    mod(p, 1, SRC_RANDOM, 'param1', 6)      # dust: small per-note changes
    mod(p, 4, SRC_RANDOM, 'balance', 10)
    mod(p, 5, SRC_RANDOM, 'param2', 6)
    out.append(('KS Dust', p))
    p = ks(damping=18, color=118, position=50, ens=(12, 25, 50, 40))
    out.append(('KS GlassHarp', p))
    p = ks(damping=70, color=60, body=90, position=45)
    set_env(p, 2, attack=0, decay=env_value(0.05), sustain=0, release=0)
    mod(p, 1, SRC_ENV_3, 'osc1', 2)         # a thumb-piano thunk
    out.append(('KS Thumb', p))
    p = ks(damping=20, color=100, position=64, ens=(15, 40, 40, 50))
    voice_lfo(p, LFO_TRI, 0.23)             # color drifts at the edge
    mod(p, 1, 10, 'param2', 10)             # of metallic
    out.append(('KS Shimmer', p))
    p = ks(damping=55, color=85, sustain=30)
    mod(p, 1, MOD_SRC_VELOCITY, 'param1', -8)   # soft = muted
    out.append(('KS Velvet', p))
    p = ks(damping=40, color=100, decay=90, exc=2, position=15)
    mod(p, 1, SRC_RANDOM, 'osc1', 1)        # short clicks, pitch unsteady
    mod(p, 4, SRC_RANDOM, 'param1', 12)
    out.append(('KS Clicks', p))

    # West Coast: envelope 1 is the fold envelope; 3-7 and the LFO are free.
    p = wc(fold=18, color=85, attack=env_value(0.3), release=60)
    voice_lfo(p, LFO_TRI, 0.17)
    mod(p, 1, 10, 'param1', 10)
    out.append(('WC SoftFold', p))
    p = wc(fold=8, color=80, env_fold=60, fold_decay=12, sub=30, release=10)
    set_env(p, 1, attack=0, decay=env_value(0.2), sustain=0, release=10)
    set_env(p, 2, attack=0, decay=env_value(0.05), sustain=0, release=0)
    mod(p, 1, SRC_ENV_3, 'osc1', 4)
    out.append(('WC SoftBongo', p))
    p = wc(fold=30, color=95)
    voice_lfo(p, LFO_SH, 3.0)               # quiet random steps
    mod(p, 1, 10, 'param1', 12)
    out.append(('WC Steps', p))
    p = wc(fold=12, color=90, env_fold=45, fold_decay=20, release=15)
    set_env(p, 1, attack=0, decay=env_value(0.4), sustain=0, release=15)
    set_env(p, 2, attack=0, decay=env_value(0.1), sustain=0, release=0)
    mod(p, 1, SRC_ENV_3, 'osc1', 2)         # rubbery pitch dip
    out.append(('WC Rubber', p))
    p = wc(fold=40, color=100)
    voice_lfo(p, LFO_TRI, 0.31)
    mod(p, 1, 10, 'param2', 14)             # symmetry drifts
    out.append(('WC Drift', p))
    p = wc(fold=6, color=80, env_fold=30)
    mod(p, 1, MOD_SRC_VELOCITY, 'param1', 10)   # harder = more folds
    out.append(('WC VeloFold', p))
    p = wc(fold=15, color=70, env_fold=50, fold_decay=10, release=10)
    set_env(p, 1, attack=0, decay=env_value(0.3), sustain=0, release=10)
    mod(p, 1, SRC_RANDOM, 'mix_param', 12)  # woody, color varies
    out.append(('WC Wood', p))
    p = wc(fold=10, color=55, sub=70, attack=env_value(0.4), release=60)
    loop_env(p, 2, 4.7)
    mod(p, 1, SRC_ENV_3, 'mix_param', 8)
    out.append(('WC SubPulse', p))
    return out


def main():
    outdir = sys.argv[1] if len(sys.argv) > 1 else '.'
    voices = {slot: (name, patch) for slot, name, patch in tx81z_voices()}

    # FM patches - Bank C
    for i, slot in enumerate(BANK_C):
        name, patch = voices[slot]
        save_patch(outdir, 'C', i + 1, ('FM ' + name.strip()).ljust(14), patch)

    # Movement patches - Bank M
    for i, (name, patch) in enumerate(bank_m(voices)):
        save_patch(outdir, 'M', i, name.ljust(14), patch)

    # Understated, IDM-ish patches - Bank I
    for i, (name, patch) in enumerate(bank_i(voices)):
        save_patch(outdir, 'I', i, name.ljust(14), patch)

    # TX81Z factory voices A01-D32 - Bank T, slots 0-127
    for i, (slot, (name, patch)) in enumerate(voices.items()):
        save_patch(outdir, 'T', i, (slot + ' ' + name).ljust(14), patch)


if __name__ == '__main__':
    main()
