#!/usr/bin/env python3
"""Synthesises Cryo Loader's boot sound and builds it for the console.

An old-computer start-up chime: a soft, slightly "wrong" first chord (C major 7 sharp 11, lydian and
dreamy) blooms and slides into the home chord (E major 9), the way the big start-up chords of mid-90s
desktops resolve. Detuned saw voices through a filter that opens slowly, a gentle tape wobble, a
sparkle of bells on top and a long hall reverb. Everything is generated here: no samples.

Kept in stereo: one mono ADPCM file per side, played on two voices panned hard left and right. 48 kHz,
so nothing is resampled. A raised-cosine fade takes it to exact silence at FADE_END, followed by a short
run of zeros so the last ADPCM block (the one with the end flag) is silent.

usage: make_boot.py outdir [preview.wav]   (needs adpenc on PATH or ADPENC=..., numpy and scipy)
"""
import os, subprocess, sys, wave
import numpy as np
from scipy.signal import butter, sosfilt, fftconvolve

SR = 48000
LEN = 6.6          # s rendered
FADE_START = 4.8   # s: the boot screen is about 4 s; the tail fades out under the library
FADE_END = 6.4     # s: silence from here
PAD = 0.05         # s of zeros after the fade
rng = np.random.default_rng(1995)
t = np.arange(int(LEN * SR)) / SR


def hz(midi):
    return 440.0 * 2 ** ((midi - 69) / 12)


def smooth(x0, x1, a, b):
    """0 before a, 1 after b, an S curve in between"""
    u = np.clip((t - a) / (b - a), 0, 1)
    return x0 + (x1 - x0) * u * u * (3 - 2 * u)


# tape wobble: a slow wow plus a little drift, shared by every voice (in cents)
wow = 9 * np.sin(2 * np.pi * 0.43 * t) + 4 * np.sin(2 * np.pi * 0.17 * t + 1.3)
drift = np.cumsum(rng.normal(0, 1, len(t)))
drift = sosfilt(butter(1, 0.6, fs=SR, output='sos'), drift)
drift = 5 * drift / (np.abs(drift).max() + 1e-9)
cents = wow + drift


def saw(freq, detune_c, phase):
    """band-limited saw (harmonics up to ~9 kHz), pitch bent by the wobble"""
    f = freq * 2 ** ((detune_c + cents) / 1200)
    ph = 2 * np.pi * np.cumsum(f) / SR + phase
    out = np.zeros_like(t)
    n_max = int(9000 / freq)
    for n in range(1, max(2, n_max)):
        out += np.sin(n * ph) / n
    return out * 0.55


def chord(notes, a0, a1, r0, r1, spread):
    """a pad: three detuned saws per note per side, swelling in over a0..a1, out over r0..r1"""
    L = np.zeros_like(t)
    R = np.zeros_like(t)
    for i, m in enumerate(notes):
        for k, d in enumerate((-spread, 0.0, spread)):
            ph = rng.uniform(0, 2 * np.pi, 2)
            L += saw(hz(m), d + 1.5 * (k - 1), ph[0])
            R += saw(hz(m), -d - 1.5 * (k - 1), ph[1])
    env = smooth(0, 1, a0, a1) * smooth(1, 0, r0, r1)
    return L * env / len(notes), R * env / len(notes)


def lowpass_sweep(x, f0, f1, a, b, blocks=96):
    """a filter that opens from f0 to f1 over a..b (block-wise; the blocks are short enough not to hear)"""
    out = np.zeros_like(x)
    edges = np.linspace(0, len(x), blocks + 1).astype(int)
    zi = None
    for j in range(blocks):
        s, e = edges[j], edges[j + 1]
        tc = t[(s + e) // 2]
        u = np.clip((tc - a) / (b - a), 0, 1)
        fc = f0 * (f1 / f0) ** (u * u * (3 - 2 * u))
        sos = butter(2, fc, fs=SR, output='sos')
        if zi is None:
            zi = np.zeros((sos.shape[0], 2))
        out[s:e], zi = sosfilt(sos, x[s:e], zi=zi)
    return out


def bell(freq, at, dec, amp):
    """a soft glassy bell: a sine with an inharmonic partial that dies first"""
    tt = np.clip(t - at, 0, None)
    on = (t >= at).astype(float)
    att = np.clip(tt / 0.006, 0, 1)
    x = np.sin(2 * np.pi * freq * tt) + 0.35 * np.sin(2 * np.pi * freq * 2.76 * tt) * np.exp(-tt / (dec * 0.25))
    return amp * on * att * np.exp(-tt / dec) * x


# --- the two chords: C maj7#11 (C E G B F#) slides into E maj9 (E G# B D# F#)
c1 = [48, 55, 59, 64, 66]          # C3 G3 B3 E4 F#4
c2 = [40, 52, 56, 59, 63, 66, 71]  # E2 E3 G#3 B3 D#4 F#4 B4
L1, R1 = chord(c1, 0.02, 0.55, 1.05, 1.75, 7)
L2, R2 = chord(c2, 1.05, 1.9, 4.2, 6.2, 9)
padL = L1 * 0.9 + L2
padR = R1 * 0.9 + R2
padL = lowpass_sweep(padL, 380, 3200, 0.0, 2.6)
padR = lowpass_sweep(padR, 380, 3200, 0.0, 2.6)

# a low sine under the home chord, and a breath of filtered noise as it lands
sub = 0.35 * np.sin(2 * np.pi * hz(28) * t) * smooth(0, 1, 1.0, 1.8) * smooth(1, 0, 3.8, 6.0)
air = rng.normal(0, 1, len(t))
air = sosfilt(butter(2, [2500, 7000], btype='band', fs=SR, output='sos'), air) * 0.05
air *= smooth(0, 1, 0.9, 1.25) * smooth(1, 0, 1.3, 2.6)

# sparkle: a rising arpeggio of the home chord as it arrives, alternating sides
sparkL = np.zeros_like(t)
sparkR = np.zeros_like(t)
for i, (m, at) in enumerate(zip([76, 80, 83, 87, 90, 95], [1.18, 1.32, 1.46, 1.60, 1.74, 1.95])):
    b = bell(hz(m), at, 1.1 + 0.15 * i, 0.10)
    if i % 2:
        sparkL += 0.45 * b
        sparkR += b
    else:
        sparkL += b
        sparkR += 0.45 * b

dryL = padL + sub + air + sparkL
dryR = padR + sub + air + sparkR

# --- hall reverb: decaying noise, a little darker as it fades, different on each side
ir_len = int(3.2 * SR)
ti = np.arange(ir_len) / SR
irs = []
for side in range(2):
    n = rng.normal(0, 1, ir_len) * np.exp(-ti / 0.85)
    n = sosfilt(butter(1, 5200, fs=SR, output='sos'), n)
    n[: int(0.012 * SR)] = 0  # pre-delay
    irs.append(n / np.sqrt(np.sum(n * n)))
wetL = fftconvolve(dryL, irs[0])[: len(t)]
wetR = fftconvolve(dryR, irs[1])[: len(t)]
L = 0.62 * dryL + 0.75 * wetL
R = 0.62 * dryR + 0.75 * wetR

# --- fades, a gentle saturation, level
env = smooth(1, 0, FADE_START, FADE_END)
env[t < 0.004] *= 0.5 - 0.5 * np.cos(np.pi * t[t < 0.004] / 0.004)
st = np.stack([L, R], 1) * env[:, None]
st = st[: int(FADE_END * SR)]
st = np.vstack([st, np.zeros((int(PAD * SR), 2))])
st /= np.abs(st).max()
st = np.tanh(1.3 * st) / np.tanh(1.3)
# as loud as the menu sounds expect (about -16 dB RMS over the first 4 s), never above -1 dB peak
rms = np.sqrt(np.mean(st[: 4 * SR] ** 2))
gain = min(10 ** (-16 / 20) / rms, 10 ** (-1 / 20) / np.abs(st).max())
pcm = np.clip(np.round(st * gain * 32767), -32768, 32767).astype('<i2')


def write_wav(path, data, ch):
    with wave.open(path, 'wb') as w:
        w.setnchannels(ch)
        w.setsampwidth(2)
        w.setframerate(SR)
        w.writeframes(data.tobytes())


def main():
    out = sys.argv[1]
    adpenc = os.environ.get('ADPENC', 'adpenc')
    os.makedirs(out, exist_ok=True)
    if len(sys.argv) > 2:
        write_wav(sys.argv[2], pcm, 2)
    for side, name in ((0, 'l'), (1, 'r')):
        wav = os.path.join(out, 'cryo_boot_%s.wav' % name)
        write_wav(wav, np.ascontiguousarray(pcm[:, side]), 1)
        subprocess.run([adpenc, wav, os.path.join(out, 'cryo_boot_%s.adp' % name)], check=True, capture_output=True)
        os.remove(wav)
    print('boot: %.2f s, fade %.1f-%.1f s' % (len(pcm) / SR, FADE_START, FADE_END))


if __name__ == '__main__':
    main()
