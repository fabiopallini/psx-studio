#!/usr/bin/env python3
"""Generate an original looping tactical synth bed; desktop synthesis only."""
import os
import math
from pathlib import Path
import struct
import subprocess
import tempfile
import wave
ROOT = Path(__file__).resolve().parents[1]
def main():
    rate = 11025
    # Eight seconds / four bars at 120 BPM; notes and envelopes end at the seam.
    samples = []
    bass = [110, 110, 130.8128, 97.9989]
    melody = [440, 523.251, 659.255, 523.251, 440, 391.995, 329.628, 391.995,
              523.251, 659.255, 783.991, 659.255, 391.995, 493.883, 587.330, 493.883]
    for i in range(rate * 8):
        t = i / rate
        beat = t % .5
        note = bass[int(t // 2)]
        pulse = math.sin(2 * math.pi * note * beat) * math.exp(-beat * 8)
        high = math.sin(2 * math.pi * note * 4 * beat) * math.exp(-beat * 16)
        kick = math.sin(2 * math.pi * 55 * beat) * math.exp(-beat * 30)
        fade = min(1, beat / .006, (.5 - beat) / .015)
        lead = math.sin(2 * math.pi * melody[int(t * 2)] * beat)
        lead += .20 * math.sin(4 * math.pi * melody[int(t * 2)] * beat)
        lead *= math.exp(-beat * 2.2)
        # Midrange melody and a sustained envelope remain audible on small
        # speakers; the former bass-only bed was much quieter than gunshots.
        value = (pulse * .22 + high * .12 + kick * .15 + lead * .70) * fade
        samples.append(round(math.tanh(value * 1.8) * 30000))
    with tempfile.TemporaryDirectory(prefix='shadow-music-') as tmp:
        directory = Path(tmp)
        source = directory / 'patrol.wav'
        with wave.open(str(source), 'wb') as out:
            out.setparams((1, 2, rate, 0, 'NONE', 'not compressed'))
            out.writeframes(struct.pack('<' + 'h' * len(samples), *samples))
        subprocess.run([str(ROOT / 'build' / ('psx-studio.exe' if os.name == 'nt' else 'psx-studio')), '--import-wav', str(source), '--audio-directory', tmp], check=True)
        encoded, = directory.glob('*.vag')
        data = bytearray(encoded.read_bytes())
        # Silent lead-in once, loop start at first encoded music block; final
        # silent block repeats. SPU loop address then follows the start marker.
        data[65] = 4
        data[-15] = 3
        (ROOT / 'examples/shadow-ops_psx_data/patrol.vag').write_bytes(data)
if __name__ == '__main__':
    main()
