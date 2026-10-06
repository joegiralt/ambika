#!/usr/bin/python3
"""Count stale-buffer glitches in a capture of a held note.

usage: glitch_count.py capture.wav t0,t1,label [t0,t1,label ...]
Reads channel CH (default 4 = MOTU input 5) of a multichannel s32 WAV, such
as `pw-record --target <node> --channels 20 --rate 48000 --format s32 x.wav`.
A held sine has a tiny second difference; a replayed chunk of old buffer
does not, so this works at any pitch.
"""
import numpy as np, sys, wave, os
CH = int(os.environ.get('CH', 4))
def load(f, ch=CH):
    w = wave.open(f); n = w.getnframes(); c = w.getnchannels()
    return np.frombuffer(w.readframes(n), dtype='<i4').reshape(-1, c).astype(float)[:, ch] / 2**31
def stats(f, t0, t1, label, sr=48000):
    x=load(f); seg=x[int(t0*sr):int(t1*sr)]; seg=seg-seg.mean(); N=len(seg); amp=np.percentile(abs(seg),99)
    d2=np.abs(np.diff(seg,2))          # 2nd difference: ~0 for any slow sine, large at a discontinuity
    bad=d2>0.08*amp
    ev=[]; i=0
    while i<N-2:
        if bad[i]:
            j=i
            while j<N-2 and bad[j:j+48].any(): j+=1
            ev.append((i,j)); i=j
        else: i+=1
    du=np.array([e[1]-e[0] for e in ev])/sr*1000; st=np.array([e[0] for e in ev])/sr; per=np.diff(st)*1000
    print('%-16s rms %6.1f dBFS  events %5.1f/s  dur mean %.2f ms  period mean %.2f ms (min %.2f)  garbage %.1f%%' % (label, 20*np.log10(seg.std()), len(ev)/(N/sr), du.mean() if len(du) else 0, per.mean() if len(per) else 0, per.min() if len(per) else 0, 100*du.sum()/(N/sr*1000)))
for a in sys.argv[2:]:
    t0,t1,label=a.split(','); stats(sys.argv[1],float(t0),float(t1),label)
