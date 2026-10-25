#!/usr/bin/env python3
"""Create Shadow Ops' original short cartoon gunshot, then use the native encoder.

Desktop asset generation only; no synthesis or floating-point math runs on PS1.
Run from the repository root after `make`. No external audio packages are needed.
"""
import os
import math
from pathlib import Path
import random
import shutil
import struct
import subprocess
import tempfile
import wave

ROOT = Path(__file__).resolve().parents[1]
RATE = 22050


def main():
    random_source = random.Random(7461)
    samples = []
    low = 0.0
    previous = 0.0
    # A sharp noisy crack, a descending low thump and a short airy tail.
    # Fade both endpoints to avoid clicks; the sound lasts 140 ms.
    for index in range(int(RATE * 0.140)):
        seconds = index / RATE
        noise = random_source.uniform(-1, 1)
        low += (noise - low) * 0.30
        transient = (noise - previous * 0.5) * math.exp(-seconds * 55)
        previous = noise
        body = math.sin(2 * math.pi * (190 * seconds - 350 * seconds**2)) * math.exp(-seconds * 18)
        tail = low * math.exp(-seconds * 24)
        attack = min(1.0, seconds / 0.0007)
        fade = min(1.0, (0.140 - seconds) / 0.015)
        value = (0.64 * transient + 0.32 * body + 0.22 * tail) * attack * fade
        # Offline soft limiting increases the audible body without hard clipping
        # the initial crack. The old peak-heavy sample was too quiet in-game.
        samples.append(round(math.tanh(value * 3.0) * 30000))
    with tempfile.TemporaryDirectory(prefix='shadow-shot-') as temporary:
        directory = Path(temporary)
        source = directory / 'shot.wav'
        with wave.open(str(source), 'wb') as output:
            output.setparams((1, 2, RATE, 0, 'NONE', 'not compressed'))
            output.writeframes(struct.pack('<' + 'h' * len(samples), *samples))
        subprocess.run([str(ROOT / 'build' / ('psx-studio.exe' if os.name == 'nt' else 'psx-studio')), '--import-wav', str(source),
                        '--audio-directory', str(directory)], check=True)
        encoded, = directory.glob('*.vag')
        destination = ROOT / 'examples/shadow-ops_psx_data/shot.vag'
        shutil.copyfile(encoded, destination)
        print(f'Created {destination.relative_to(ROOT)} ({destination.stat().st_size} bytes)')


if __name__ == '__main__':
    main()
