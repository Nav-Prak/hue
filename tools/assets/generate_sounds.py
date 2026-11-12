# tools/assets/generate_sounds.py
"""Synthesize Hue's sound effects. Deterministic, standard library only.

Every sound is project-authored procedural audio (filtered noise and simple
tones), so no third-party recording or license is involved. Output: 22.05 kHz
mono 16-bit PCM WAV files in assets/audio. Re-running reproduces the checked-in
files byte for byte.
"""
import math
from pathlib import Path
import random
import struct
import wave

RATE = 22050
OUT = Path(__file__).resolve().parents[2] / "assets" / "audio"


def envelope(t, length, attack=0.005, release=0.05):
    if t < attack:
        return t / attack
    if t > length - release:
        return max(0.0, (length - t) / release)
    return 1.0


def noise(rng, length, cutoff_start, cutoff_end):
    """One-pole low-passed white noise with a sweeping cutoff (Hz)."""
    out, state = [], 0.0
    count = int(length * RATE)
    for i in range(count):
        u = i / count
        cutoff = cutoff_start + (cutoff_end - cutoff_start) * u
        alpha = 1.0 - math.exp(-2.0 * math.pi * cutoff / RATE)
        state += alpha * (rng.uniform(-1.0, 1.0) - state)
        out.append(state)
    return out


def tone(length, start_hz, end_hz, harmonics=(1.0,)):
    out, phase = [], 0.0
    count = int(length * RATE)
    for i in range(count):
        u = i / count
        phase += 2.0 * math.pi * (start_hz + (end_hz - start_hz) * u) / RATE
        out.append(sum(a * math.sin(phase * (k + 1)) for k, a in enumerate(harmonics)))
    return out


def shaped(samples, gain, attack=0.005, release=0.05, decay=0.0):
    length = len(samples) / RATE
    out = []
    for i, s in enumerate(samples):
        t = i / RATE
        out.append(s * gain * envelope(t, length, attack, release) * math.exp(-decay * t))
    return out


def mix(*layers):
    out = [0.0] * max(len(layer) for layer in layers)
    for layer in layers:
        for i, s in enumerate(layer):
            out[i] += s
    return out


def after(delay, samples):
    return [0.0] * int(delay * RATE) + samples


def write(name, samples):
    peak = max(1e-9, max(abs(s) for s in samples))
    scale = min(1.0, 0.89 / peak)  # leave headroom; several sounds can overlap
    frames = b"".join(struct.pack("<h", int(max(-1.0, min(1.0, s * scale)) * 32767)) for s in samples)
    OUT.mkdir(parents=True, exist_ok=True)
    with wave.open(str(OUT / (name + ".wav")), "wb") as out:
        out.setnchannels(1)
        out.setsampwidth(2)
        out.setframerate(RATE)
        out.writeframes(frames)


def main():
    rng = random.Random(2026)
    write("swing_light", shaped(noise(rng, 0.18, 900, 3200), 0.8, 0.03, 0.09))
    write("swing_heavy", shaped(noise(rng, 0.32, 400, 2200), 1.0, 0.08, 0.14))
    write("hit_light", mix(shaped(tone(0.14, 190, 70), 0.9, 0.002, 0.05, 18),
                           shaped(noise(rng, 0.07, 4000, 900), 0.7, 0.001, 0.04, 30)))
    write("hit_heavy", mix(shaped(tone(0.26, 130, 42, (1.0, 0.4)), 1.0, 0.002, 0.08, 10),
                           shaped(noise(rng, 0.12, 5000, 500), 0.9, 0.001, 0.06, 20)))
    write("stagger", mix(shaped(tone(0.22, 320, 150, (1.0, 0.5, 0.25)), 0.6, 0.004, 0.1, 8),
                         shaped(noise(rng, 0.1, 2500, 700), 0.4, 0.002, 0.05, 20)))
    write("dodge", shaped(noise(rng, 0.2, 2600, 600), 0.6, 0.02, 0.12))
    write("death", mix(shaped(tone(0.7, 220, 55, (1.0, 0.5, 0.3)), 0.8, 0.01, 0.3, 3),
                       shaped(noise(rng, 0.3, 1200, 200), 0.4, 0.01, 0.2, 6)))
    write("lock_on", mix(shaped(tone(0.06, 880, 880), 0.5, 0.003, 0.03),
                         after(0.07, shaped(tone(0.08, 1320, 1320), 0.5, 0.003, 0.04))))
    write("lock_off", mix(shaped(tone(0.06, 990, 990), 0.4, 0.003, 0.03),
                          after(0.07, shaped(tone(0.08, 660, 660), 0.4, 0.003, 0.04))))
    write("fight_start", mix(shaped(tone(0.12, 392, 392, (1.0, 0.3)), 0.6, 0.004, 0.06),
                             after(0.12, shaped(tone(0.12, 523, 523, (1.0, 0.3)), 0.6, 0.004, 0.06)),
                             after(0.24, shaped(tone(0.3, 784, 784, (1.0, 0.3)), 0.7, 0.004, 0.2))))
    write("victory", mix(*[after(0.14 * i, shaped(tone(0.5, hz, hz, (1.0, 0.35, 0.15)), 0.5, 0.005, 0.3, 3))
                           for i, hz in enumerate((523, 659, 784, 1047))]))
    write("defeat", mix(*[after(0.2 * i, shaped(tone(0.6, hz, hz * 0.97, (1.0, 0.5, 0.2)), 0.5, 0.01, 0.35, 2))
                          for i, hz in enumerate((392, 311, 262, 196))]))
    total = sum(path.stat().st_size for path in OUT.glob("*.wav"))
    print(f"wrote {len(list(OUT.glob('*.wav')))} sounds, {total} bytes, to {OUT}")


if __name__ == "__main__":
    main()
