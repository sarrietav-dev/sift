#!/usr/bin/env -S uv run --script
# /// script
# requires-python = ">=3.11"
# dependencies = ["numpy>=2"]
# ///
"""Synthesize the music bed and the sound effects @remotion/sfx doesn't ship.

Writes public/music/bed.wav and public/sfx/{key,pop,riser,hit}.wav.

    uv run scripts/sounds.py [--seconds 56]
"""

import argparse
import pathlib
import wave

import numpy as np

SR = 44100
ROOT = pathlib.Path(__file__).resolve().parent.parent
rng = np.random.default_rng(7)


def write(path: pathlib.Path, stereo: np.ndarray) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    peak = np.max(np.abs(stereo)) or 1
    data = (stereo / peak * 0.89 * 32767).astype(np.int16)
    with wave.open(str(path), "wb") as w:
        w.setnchannels(2)
        w.setsampwidth(2)
        w.setframerate(SR)
        w.writeframes(data.reshape(-1).tobytes())


def lowpass(x: np.ndarray, cutoff: float) -> np.ndarray:
    a = np.exp(-2 * np.pi * cutoff / SR)
    y = np.empty_like(x)
    acc = 0.0
    for i, v in enumerate(x):
        acc = (1 - a) * v + a * acc
        y[i] = acc
    return y


def reverb(x: np.ndarray, mix: float = 0.3) -> np.ndarray:
    """A small Schroeder reverb: parallel combs into an allpass."""
    out = np.zeros(len(x) + SR * 2)
    for delay_ms, gain in [(29.7, 0.80), (37.1, 0.78), (41.1, 0.76), (43.7, 0.74)]:
        d = int(SR * delay_ms / 1000)
        buf = np.zeros(len(out))
        buf[: len(x)] = x
        for i in range(d, len(buf)):
            buf[i] += gain * buf[i - d]
        out += buf / 4
    wet = out[: len(x)]
    return (1 - mix) * x + mix * wet / (np.max(np.abs(wet)) or 1) * np.max(np.abs(x))


def stereo(x: np.ndarray, width: float = 0.0) -> np.ndarray:
    d = int(SR * 0.011)
    right = np.concatenate([np.zeros(d), x[:-d]]) if width else x
    return np.stack([x, (1 - width) * x + width * right], axis=1)


def note(freq: float, seconds: float, attack: float, release: float, detune=(0.0,)) -> np.ndarray:
    t = np.arange(int(SR * seconds)) / SR
    tone = sum(
        np.sin(2 * np.pi * freq * (1 + c / 1200) * t) + 0.25 * np.sin(4 * np.pi * freq * (1 + c / 1200) * t)
        for c in detune
    ) / len(detune)
    env = np.minimum(1, t / attack) * np.minimum(1, (seconds - t) / release).clip(0, 1)
    return tone * env


def midi(n: int) -> float:
    return 440 * 2 ** ((n - 69) / 12)


def bed(seconds: float) -> np.ndarray:
    """Warm four-chord pad with a quiet plucked arpeggio and a soft pulse, 80 bpm."""
    beat = 60 / 80
    bar = beat * 4
    chords = [  # Fmaj9, Am7, Dm9, Bbmaj7(#11) -- calm, a little bright
        [41, 53, 57, 60, 64, 67],
        [45, 52, 55, 60, 64, 67],
        [38, 53, 57, 60, 64, 69],
        [46, 53, 57, 62, 64, 69],
    ]
    n = int(SR * seconds)
    pad = np.zeros(n)
    arp = np.zeros(n)
    pulse = np.zeros(n)
    t0 = 0.0
    k = 0
    while t0 < seconds:
        chord = chords[k % 4]
        start = int(t0 * SR)
        length = min(bar * 2 + 1.5, seconds - t0)
        for m in chord:
            v = note(midi(m), length, attack=1.2, release=1.4, detune=(-7, 0, 6)) * (1.4 if m < 50 else 0.55)
            end = min(n, start + len(v))
            pad[start:end] += v[: end - start]
        # Arpeggio in eighths over the upper voices, pluck-shaped.
        for e in range(16):
            at = t0 + e * beat / 2
            if at >= seconds:
                break
            m = chord[2 + (e * 3) % 4] + 12
            dur = 0.9
            t = np.arange(int(SR * dur)) / SR
            pl = np.sin(2 * np.pi * midi(m) * t) * np.exp(-t * 6) * (0.9 if e % 4 == 0 else 0.6)
            s = int(at * SR)
            end = min(n, s + len(pl))
            arp[s:end] += pl[: end - s]
        # Soft sub pulse on 1 and 3.
        for b in (0, 2, 4, 6):
            at = t0 + b * beat
            if at >= seconds:
                break
            dur = 0.45
            t = np.arange(int(SR * dur)) / SR
            f = 55 * np.exp(-t * 9) + 42
            kick = np.sin(2 * np.pi * np.cumsum(f) / SR) * np.exp(-t * 8)
            s = int(at * SR)
            end = min(n, s + len(kick))
            pulse[s:end] += kick[: end - s]
        t0 += bar * 2
        k += 1

    pad = lowpass(pad, 1400)
    mix = 0.55 * pad / np.max(np.abs(pad)) + 0.22 * arp / np.max(np.abs(arp)) + 0.30 * pulse / np.max(np.abs(pulse))
    mix = reverb(mix, 0.35)
    t = np.arange(n) / SR
    mix *= np.minimum(1, t / 2.0) * np.minimum(1, (seconds - t) / 3.0).clip(0, 1)
    return stereo(mix, 0.6)


def key() -> np.ndarray:
    """One mechanical keystroke: a short filtered click plus a body thump."""
    t = np.arange(int(SR * 0.06)) / SR
    click = rng.standard_normal(len(t)) * np.exp(-t * 400)
    body = np.sin(2 * np.pi * 180 * t) * np.exp(-t * 90) * 0.5
    return stereo(lowpass(click, 5000) + body)


def pop() -> np.ndarray:
    t = np.arange(int(SR * 0.18)) / SR
    f = 520 + 380 * np.exp(-t * 40)
    return stereo(np.sin(2 * np.pi * np.cumsum(f) / SR) * np.exp(-t * 26))


def riser() -> np.ndarray:
    """Airy noise swell that peaks at its end, for transitions."""
    dur = 1.1
    t = np.arange(int(SR * dur)) / SR
    noise = rng.standard_normal(len(t))
    swell = (t / dur) ** 2.4
    shaped = lowpass(noise, 2600) * swell
    shimmer = np.sin(2 * np.pi * (600 + 900 * t / dur) * t) * swell * 0.15
    return stereo(reverb(shaped + shimmer, 0.25), 0.8)


def hit() -> np.ndarray:
    """Deep, soft impact with a little air on top."""
    dur = 2.2
    t = np.arange(int(SR * dur)) / SR
    f = 38 + 60 * np.exp(-t * 14)
    sub = np.sin(2 * np.pi * np.cumsum(f) / SR) * np.exp(-t * 2.2)
    air = lowpass(rng.standard_normal(len(t)), 3000) * np.exp(-t * 9) * 0.25
    return stereo(reverb(sub + air, 0.3), 0.5)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--seconds", type=float, default=56)
    args = parser.parse_args()
    write(ROOT / "public" / "sfx" / "key.wav", key())
    write(ROOT / "public" / "sfx" / "pop.wav", pop())
    write(ROOT / "public" / "sfx" / "riser.wav", riser())
    write(ROOT / "public" / "sfx" / "hit.wav", hit())
    write(ROOT / "public" / "music" / "bed.wav", bed(args.seconds))
    print("wrote sfx/key, sfx/pop, sfx/riser, sfx/hit, music/bed")


if __name__ == "__main__":
    main()
