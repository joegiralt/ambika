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
FM_WAVE_SINE = 0
FM_WAVE_HALF_SINE = 1
FM_WAVE_ABS_SINE = 2
FM_WAVE_QUARTER_SINE = 3
FM_WAVE_HALF_ABS = 4
FM_WAVE_TRIPLE_ABS = 5
FM_WAVE_PULSE_SINE = 6
FM_WAVE_SAW_SINE = 7

# TX81Z coarse frequency ratio indices (index into ratio table)
R_050 = 0   # 0.50x
R_071 = 1   # 0.71x (√2/2)
R_078 = 2   # 0.78x
R_087 = 3   # 0.87x
R_100 = 4   # 1.00x
R_141 = 5   # 1.41x (√2)
R_157 = 6   # 1.57x (π/2)
R_173 = 7   # 1.73x (√3)
R_200 = 8   # 2.00x
R_282 = 9   # 2.82x
R_300 = 10  # 3.00x
R_314 = 11  # 3.14x (π)
R_400 = 13  # 4.00x
R_500 = 16  # 5.00x
R_600 = 19  # 6.00x
R_700 = 22  # 7.00x
R_800 = 25  # 8.00x

# Mod sources (Carcosa v2.0)
MOD_SRC_ENV_1 = 0
MOD_SRC_ENV_2 = 1
MOD_SRC_ENV_3 = 2
MOD_SRC_VELOCITY = 16
MOD_SRC_PITCH_BEND = 18
MOD_SRC_WHEEL = 19

# Mod destinations
MOD_DST_PARAMETER_1 = 0
MOD_DST_OSC_1_2_COARSE = 4
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


def set_fm(patch, algo, feedback=0,
           wav1=0, wav2=0, wav3=0, wav4=0,
           rat1=R_100, rat2=R_100, rat3=R_100, rat4=R_100,
           fin1=0, fin2=0, fin3=0, fin4=0,
           lvl1=127, lvl2=127, lvl3=127, lvl4=127):
    """Set FM4OP parameters on a patch."""
    patch[0] = WAVEFORM_SAW  # Safe fallback waveform; engine byte controls rendering
    patch[1] = algo
    patch[2] = rat1
    patch[3] = fin1
    patch[4] = wav1 | (wav2 << 4)
    patch[5] = wav3 | (wav4 << 4)
    patch[6] = rat2
    patch[7] = fin2
    patch[8] = rat3   # mix_balance
    patch[9] = fin3   # mix_op
    patch[10] = rat4  # mix_parameter
    patch[11] = fin4  # mix_sub_osc_shape
    patch[12] = lvl1  # mix_sub_osc
    patch[13] = lvl2  # mix_noise
    patch[14] = lvl3  # mix_fuzz
    patch[15] = lvl4  # mix_crush
    patch[104] = feedback  # padding[0]


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


# --- FM Patches ---

def fm_lately_bass():
    # Engine type
    p = make_patch()
    set_engine(p, ENGINE_FM4OP)
    set_fm(p, algo=4, wav1=FM_WAVE_SINE, wav2=FM_WAVE_HALF_ABS,
           wav3=FM_WAVE_SINE, wav4=FM_WAVE_SINE,
           rat1=R_100, rat2=R_100, rat3=R_100, rat4=R_100,
           lvl1=127, lvl2=110, lvl3=127, lvl4=101)
    set_filter(p, cutoff=127)
    set_env(p, 0, attack=0, decay=50, sustain=100, release=30)  # env1 (VCA)
    set_env(p, 1, attack=0, decay=40, sustain=90, release=40)   # env2
    set_env(p, 3, attack=0, decay=30, sustain=90, release=30)   # env4 = op1 (carrier, sustained)
    set_env(p, 4, attack=0, decay=40, sustain=80, release=30)   # env5 = op2 (carrier, sustained)
    set_env(p, 5, attack=0, decay=10, sustain=0, release=20)    # env6 = op3 (modulator, FAST decay)
    set_env(p, 6, attack=0, decay=35, sustain=0, release=20)    # env7 = op4 (modulator, medium decay)
    set_mod(p, 0, MOD_SRC_ENV_2, MOD_DST_VCA, 63)
    set_mod(p, 1, MOD_SRC_VELOCITY, MOD_DST_VCA, 20)
    set_mod(p, 2, MOD_SRC_PITCH_BEND, MOD_DST_OSC_1_2_COARSE, 10)
    return p


def fm_epiano():
    # Engine type
    p = make_patch()
    set_engine(p, ENGINE_FM4OP)
    set_fm(p, algo=4, wav1=FM_WAVE_SINE, wav2=FM_WAVE_SINE,
           wav3=FM_WAVE_SINE, wav4=FM_WAVE_SINE,
           rat1=R_100, rat2=R_100, rat3=R_100, rat4=R_700,
           lvl1=127, lvl2=127, lvl3=90, lvl4=70)
    set_filter(p, cutoff=110)
    set_env(p, 0, attack=0, decay=60, sustain=60, release=40)
    set_env(p, 1, attack=0, decay=50, sustain=0, release=30)
    set_env(p, 3, attack=0, decay=70, sustain=50, release=40)   # op1 carrier
    set_env(p, 4, attack=0, decay=65, sustain=45, release=35)   # op2 carrier
    set_env(p, 5, attack=0, decay=15, sustain=0, release=10)    # op3 mod (bell attack)
    set_env(p, 6, attack=0, decay=25, sustain=0, release=15)    # op4 mod (bright shimmer)
    set_mod(p, 0, MOD_SRC_ENV_2, MOD_DST_VCA, 63)
    set_mod(p, 1, MOD_SRC_VELOCITY, MOD_DST_VCA, 40)
    return p


def fm_brass():
    # Engine type
    p = make_patch()
    set_engine(p, ENGINE_FM4OP)
    set_fm(p, algo=0, wav1=FM_WAVE_SINE, wav2=FM_WAVE_SINE,
           wav3=FM_WAVE_SINE, wav4=FM_WAVE_SINE,
           rat1=R_100, rat2=R_100, rat3=R_100, rat4=R_100,
           lvl1=127, lvl2=100, lvl3=90, lvl4=85)
    set_filter(p, cutoff=100, resonance=10)
    set_env(p, 0, attack=15, decay=30, sustain=100, release=30)
    set_env(p, 1, attack=10, decay=20, sustain=90, release=25)
    set_env(p, 3, attack=15, decay=30, sustain=100, release=30)  # op1 slow attack
    set_env(p, 4, attack=15, decay=30, sustain=90, release=30)   # op2
    set_env(p, 5, attack=20, decay=40, sustain=70, release=25)   # op3 mod (slow build)
    set_env(p, 6, attack=25, decay=50, sustain=60, release=20)   # op4 mod (slower build)
    set_mod(p, 0, MOD_SRC_ENV_2, MOD_DST_VCA, 63)
    set_mod(p, 1, MOD_SRC_VELOCITY, MOD_DST_VCA, 30)
    set_mod(p, 2, MOD_SRC_PITCH_BEND, MOD_DST_OSC_1_2_COARSE, 12)
    return p


def fm_marimba():
    # Engine type
    p = make_patch()
    set_engine(p, ENGINE_FM4OP)
    set_fm(p, algo=4, wav1=FM_WAVE_SINE, wav2=FM_WAVE_SINE,
           wav3=FM_WAVE_SINE, wav4=FM_WAVE_SINE,
           rat1=R_100, rat2=R_400, rat3=R_300, rat4=R_700,
           lvl1=127, lvl2=90, lvl3=100, lvl4=80)
    set_filter(p, cutoff=120)
    set_env(p, 0, attack=0, decay=40, sustain=0, release=20)
    set_env(p, 1, attack=0, decay=35, sustain=0, release=15)
    set_env(p, 3, attack=0, decay=40, sustain=0, release=20)    # op1 fast decay
    set_env(p, 4, attack=0, decay=30, sustain=0, release=15)    # op2 faster
    set_env(p, 5, attack=0, decay=5, sustain=0, release=5)      # op3 very fast
    set_env(p, 6, attack=0, decay=8, sustain=0, release=5)      # op4 very fast
    set_mod(p, 0, MOD_SRC_ENV_2, MOD_DST_VCA, 63)
    set_mod(p, 1, MOD_SRC_VELOCITY, MOD_DST_VCA, 50)
    return p


def fm_strings():
    # Engine type
    p = make_patch()
    set_engine(p, ENGINE_FM4OP)
    set_fm(p, algo=7, wav1=FM_WAVE_SINE, wav2=FM_WAVE_SINE,
           wav3=FM_WAVE_SINE, wav4=FM_WAVE_SINE,
           rat1=R_100, rat2=R_200, rat3=R_300, rat4=R_100,
           lvl1=127, lvl2=100, lvl3=80, lvl4=60)
    set_filter(p, cutoff=90, resonance=5)
    set_env(p, 0, attack=30, decay=20, sustain=110, release=40)
    set_env(p, 1, attack=25, decay=20, sustain=100, release=35)
    set_env(p, 3, attack=30, decay=20, sustain=110, release=40)
    set_env(p, 4, attack=30, decay=20, sustain=100, release=35)
    set_env(p, 5, attack=35, decay=30, sustain=80, release=30)
    set_env(p, 6, attack=20, decay=25, sustain=50, release=25)
    set_mod(p, 0, MOD_SRC_ENV_2, MOD_DST_VCA, 63)
    set_mod(p, 1, MOD_SRC_WHEEL, MOD_DST_PARAMETER_1, 20)
    return p


def fm_organ():
    # Engine type
    p = make_patch()
    set_engine(p, ENGINE_FM4OP)
    set_fm(p, algo=7, wav1=FM_WAVE_SINE, wav2=FM_WAVE_SINE,
           wav3=FM_WAVE_SINE, wav4=FM_WAVE_SINE,
           rat1=R_050, rat2=R_100, rat3=R_200, rat4=R_100,
           lvl1=127, lvl2=127, lvl3=100, lvl4=40,
           feedback=3)
    set_filter(p, cutoff=127)
    set_env(p, 0, attack=0, decay=0, sustain=127, release=10)
    set_env(p, 1, attack=0, decay=0, sustain=127, release=10)
    set_env(p, 3, attack=0, decay=0, sustain=127, release=10)
    set_env(p, 4, attack=0, decay=0, sustain=127, release=10)
    set_env(p, 5, attack=0, decay=0, sustain=127, release=10)
    set_env(p, 6, attack=0, decay=0, sustain=127, release=10)
    set_mod(p, 0, MOD_SRC_ENV_2, MOD_DST_VCA, 63)
    return p


def main():
    outdir = sys.argv[1] if len(sys.argv) > 1 else '.'

    # FM patches — Bank C
    save_patch(outdir, 'C', 1, 'FM LatelyBass ', fm_lately_bass())
    save_patch(outdir, 'C', 2, 'FM E.Piano    ', fm_epiano())
    save_patch(outdir, 'C', 3, 'FM Brass      ', fm_brass())
    save_patch(outdir, 'C', 4, 'FM Marimba    ', fm_marimba())
    save_patch(outdir, 'C', 5, 'FM Strings    ', fm_strings())
    save_patch(outdir, 'C', 6, 'FM Organ      ', fm_organ())


if __name__ == '__main__':
    main()
