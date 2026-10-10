#!/usr/bin/env python3
"""Generates PBL-7's explosion sample (resources/sfx/boom.raw).

The speaker's built-in waveforms are all pitched; an explosion is noise. This
synthesises one burst of it -- low-passed, with the brightness and level dying
away -- which the game plays through speaker_play_tracks() at different pitches:
high and short for a fighter, low and long for a boss. Computed, not recorded,
so there is nothing to license; rerun after changing it.

Output: mono, signed 8-bit PCM at 16 kHz (SpeakerPcmFormat_16kHz_8bit).

    python3 tools/gen_sfx.py            # writes resources/sfx/boom.raw
    python3 tools/gen_sfx.py --wav DIR  # also writes a .wav preview to DIR
"""
import argparse
import math
import os
import random
import struct
import wave

RATE = 16000
HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, '..', 'resources', 'sfx')


def boom():
    """Noise through a one-pole low-pass whose cutoff falls as it decays: a sharp
    crack that rumbles out, plus a little low thump for body."""
    rnd = random.Random(11)
    n = int(RATE * 0.45)
    out, lp, phase = [], 0.0, 0.0
    for i in range(n):
        t = i / RATE
        cutoff = 3500 * math.exp(-t / 0.09) + 250
        a = 1 - math.exp(-2 * math.pi * cutoff / RATE)
        lp += a * (rnd.uniform(-1, 1) - lp)
        phase += 2 * math.pi * (90 * math.exp(-t / 0.2) + 50) / RATE
        thump = math.sin(phase) * math.exp(-t / 0.08) * 0.5
        env = min(1.0, t / 0.002) * math.exp(-t / 0.12)
        out.append(math.tanh(2.5 * (lp * 1.6 + thump)) * env)
    return out


def to_pcm8(samples):
    peak = max(abs(s) for s in samples) or 1.0
    return b''.join(struct.pack('b', max(-127, min(127, int(round(s / peak * 120)))))
                    for s in samples)


def write_wav(path, samples):
    peak = max(abs(s) for s in samples) or 1.0
    with wave.open(path, 'wb') as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(RATE)
        w.writeframes(b''.join(struct.pack('<h', int(s / peak * 30000)) for s in samples))


SOUNDS = {'boom': boom}

if __name__ == '__main__':
    ap = argparse.ArgumentParser()
    ap.add_argument('--wav', help='also write .wav previews to this directory')
    args = ap.parse_args()
    os.makedirs(OUT, exist_ok=True)
    for name, fn in SOUNDS.items():
        s = fn()
        with open(os.path.join(OUT, name + '.raw'), 'wb') as f:
            f.write(to_pcm8(s))
        if args.wav:
            write_wav(os.path.join(args.wav, name + '.wav'), s)
        print('%-5s %5d bytes' % (name, len(s)))
