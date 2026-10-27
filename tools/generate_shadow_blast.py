#!/usr/bin/env python3
"""Optional offline original grenade effect; native PSX Studio WAV/VAG encoder."""
import math
import os
from pathlib import Path
import random
import shutil
import struct
import subprocess
import tempfile
import wave
ROOT=Path(__file__).resolve().parents[1]
def main():
    rate=22050
    rng=random.Random(1907)
    samples=[];low=0
    for i in range(rate//2):
        t=i/rate;noise=rng.uniform(-1,1);low+=(noise-low)*0.18
        value=(low*1.8+math.sin(2*math.pi*(85*t-40*t*t))*.7)*math.exp(-t*10)
        value*=min(1,t/.001)*min(1,(.5-t)/.02)
        samples.append(round(math.tanh(value*2)*29000))
    with tempfile.TemporaryDirectory(prefix='shadow-blast-') as temporary:
        folder=Path(temporary);wav=folder/'boom.wav'
        with wave.open(str(wav),'wb') as out:
            out.setparams((1,2,rate,0,'NONE','not compressed'))
            out.writeframes(struct.pack('<'+'h'*len(samples),*samples))
        subprocess.run([str(ROOT/'build'/('psx-studio.exe' if os.name=='nt' else 'psx-studio')),'--import-wav',str(wav),'--audio-directory',str(folder)],check=True)
        shutil.copyfile(next(folder.glob('*.vag')),ROOT/'examples/shadow-ops_psx_data/boom.vag')
if __name__=='__main__':main()
