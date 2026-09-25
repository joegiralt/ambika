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

# Mod destinations
MOD_DST_PARAMETER_1 = 0
MOD_DST_OSC_1_2_COARSE = 4
MOD_DST_OSC_1_2_FINE = 5
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


def save_patch(outdir, bank, slot, name, patch_data):
    """Save a patch to the right directory."""
    bank_dir = os.path.join(outdir, 'PATCH', 'BANK', bank)
    os.makedirs(bank_dir, exist_ok=True)
    outpath = os.path.join(bank_dir, f'{slot:03d}.PAT')
    riff = make_riff_patch(patch_data, name)
    with open(outpath, 'wb') as f:
        f.write(riff)
    print(f'  {bank}{slot:02d} - {name.strip()}')


# --- FM Patches: TX81Z factory voices ---
#
# The TX81Z's 128 ROM voices (tx81z_factory.py) converted to the FM engine.
# Algorithm, feedback, waveforms, ratios and levels map 1:1 onto the engine,
# which matches the TX81Z's operator path. Envelopes, LFO and velocity are
# approximated with Carcosa's ADSRs, voice LFO and VCA. Not supported:
# fixed-frequency operators (they track pitch here), keyboard level/rate
# scaling, per-operator velocity and LFO amplitude modulation.

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


def eg_seconds(rate):
    """Yamaha EG: time to fall 96 dB at effective rate 0-63 (about 6.7 ms at
    the top, doubling every 4 steps)."""
    return 0.0067 * 2 ** ((63 - min(rate, 63)) / 4)


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

        attack = 0 if ar >= 31 else env_value(eg_seconds(2 * ar) / 12)
        drop_db = (15 - d1l) * 3
        if d1r and drop_db:
            decay = env_value(eg_seconds(2 * d1r) * drop_db / 96)
            sustain = round(127 * 10 ** (-drop_db / 20))
        elif d2r:
            decay, sustain = env_value(eg_seconds(2 * d2r) / 2), 0
        else:
            decay, sustain = 0, 127
        release = env_value(eg_seconds(4 * rr + 2) / 2)
        releases[op] = release
        set_env(p, 2 + op, attack=attack, decay=decay, sustain=sustain,
                release=release)

    # The carriers' envelopes shape the sound; the VCA just gates it.
    carriers = CARRIERS[alg]
    set_env(p, 1, attack=0, decay=0, sustain=127,
            release=max(releases[op] for op in carriers))
    set_filter(p, cutoff=127)
    set_mod(p, 0, MOD_SRC_ENV_2, MOD_DST_VCA, 63)
    kvs = max(v[offsets[op] + 6] & 7 for op in carriers)
    set_mod(p, 1, MOD_SRC_VELOCITY, MOD_DST_VCA, kvs * 9)
    set_mod(p, 2, MOD_SRC_PITCH_BEND, MOD_DST_OSC_1_2_COARSE, 32)

    # Vibrato: voice LFO -> pitch.
    cents = PMS_CENTS[(v[45] >> 4) & 7] * v[43] / 99
    if cents >= 1:
        hz = 0.06 * 2 ** (v[41] / 10)
        lfo_hz = [inc * CONTROL_RATE / 65536 for inc in LFO_INCREMENTS]
        p[48] = LFO_SHAPE[v[45] & 3]
        p[49] = nearest(lfo_hz, hz)
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


def main():
    outdir = sys.argv[1] if len(sys.argv) > 1 else '.'
    voices = {slot: (name, patch) for slot, name, patch in tx81z_voices()}

    # FM patches - Bank C
    for i, slot in enumerate(BANK_C):
        name, patch = voices[slot]
        save_patch(outdir, 'C', i + 1, ('FM ' + name.strip()).ljust(14), patch)

    # TX81Z factory voices A01-D32 - Bank T, slots 0-127
    for i, (slot, (name, patch)) in enumerate(voices.items()):
        save_patch(outdir, 'T', i, (slot + ' ' + name).ljust(14), patch)


if __name__ == '__main__':
    main()
