# WAV audio import

Open a saved project and choose **Audio → Import WAV audio** from the top menu. A system file chooser selects the WAV; importing immediately creates a
VAG file in the adjacent `PROJECT_psx_data/` folder. No object selection is
required. The status line and log report the resulting path or an actionable
error. The source WAV is left untouched, and Save As copies imported audio along
with the rest of the data folder.

The editor encodes audio natively in a worker thread, without VAGEditor, Wine or
an external command. Inputs are RIFF/WAVE, uncompressed PCM, 8-bit unsigned or
16-bit signed little endian, one or two channels, 1000–44100 Hz, up to 64 MB.
Stereo is averaged to mono, and the original sample rate is retained. Export
compressed, floating-point or multichannel audio to a supported PCM WAV first.
The importer does not resample, normalize or preserve loop metadata.

Output is mono `VAGp` version `0x20` with a 48-byte big-endian header and PS1 SPU
ADPCM: 28 samples per 16-byte block. The encoder searches all five predictors
and shifts 0–12 for minimum reconstructed squared error, carrying decoded history
between blocks. It prepends a silent interpolation block, zero-pads the final
partial block and appends a silent one-shot end block (flag 1, no repeat).
Eight hexadecimal content-hash characters plus `.vag` provide a DOS 8.3 filename;
a preexisting different file is never overwritten. Identical repeated imports
reuse the existing file.

## Playing samples from C89 modules

A module declares each sound dependency with a comment containing a flat DOS 8.3
filename relative to the project data folder:

```c
/* @sound shot.vag */
#include "object.h"
/* Call after a shot/projectile was successfully created. */
/* psx_sound_play(scene, "shot.vag", 10000, 0); */
```

`psx_sound_play(scene, name, volume, pan)` starts a one-shot sample and returns 1,
or 0 if unavailable/muted. Volume is 0–16383; pan is Q12, -4096 left, 0 centre,
+4096 right. Out-of-range parameters are clamped. The editor preview does not run
module audio. There is no inspector sound assignment yet: use the imported VAG's
filename in the module declaration and playback call.

Export validates the referenced one-shot or full-sample looping VAGs and embeds their ADPCM data in the
Scene's generated C, aligned and padded to 64-byte DMA units. The sample therefore
travels inside the executable on the CD; no separate VAG CD read is needed. Only
referenced samples are included. Identical immutable ADPCM arrays can be shared
across Scenes; each Scene retains its own mutable bank metadata. Import alone does not add a
sample to a build. The build snapshots converted VAG assets before export;
missing dependencies fail the build instead of reusing stale copies.

At Scene load the console stops old voices and uploads the bank into SPU RAM.
Addresses 0x0000–0x100f are reserved; the sample arena ends at 0x80000, reverb is
off, and the padded bank must fit in the remaining 520176 bytes. Samples also
occupy main RAM as embedded executable data, so keep banks small. DMA upload and
completion waits occur only at loading boundaries, before resetting the fixed
simulation clock. Firing performs no allocation, CD reads, synthesis or DMA waits.
The voice envelope uses attack shift zero for an immediate shot transient and
full sustain until the one-shot end flag. Eight round-robin SPU voices allow overlapping effects; a ninth simultaneous
voice replaces the oldest. Scene changes and restarts stop existing effects.
`psx_music_play(scene, name, volume)` uses reserved hardware voice 23, independent
of the eight effect voices. Declare its dependency with `@sound` too. Music must
be encoded with loop-start flag 4 on the first music block after the silent lead-in
and repeat/end flag 3 on the final block; all other flags remain zero. The ordinary
WAV importer still produces one-shots. The Shadow Ops music generator adds these
markers explicitly; arbitrary or malformed loops are rejected during export.
A music request starts/replaces the dedicated voice; Scene changes stop it.
Call from the first update after the Scene audio bank is loaded, not module init
(which precedes the console bank upload). Playback requests belong in update logic,
not rendering or interrupts.

The importer is also available without opening the UI:

```sh
./build/psx-studio --import-wav effect.wav --audio-directory path/to/game_psx_data
```

## Verification and references

`make test` includes silence/format vectors, 8/16-bit mono/stereo handling,
partial blocks, padded RIFF chunks, unsupported/truncated input, repeated imports,
filename collisions and a decoded tone quality check. `tests/audio.c` optionally
writes its tone fixture when `PSX_AUDIO_FIXTURE` is set, allowing independent
decoders to check it. Encoder arithmetic runs only on the desktop. Strict C89 backend tests check
SPU upload alignment/budgets, voice overlap, pan, reset and failure paths. Gameplay
tests check successful player/guard shots, cooldown and no-ammo silence.

Format references:

- [SPU ADPCM blocks, predictors and flags](https://psx-spx.consoledev.net/soundprocessingunitspu/#spu-adpcm-samples)
- [VAG header format](https://psx-spx.consoledev.net/dev/ps1/cdr/cdromfileformats/audio/)

The SDK's `LIBREF46.PDF`, entries `SpuWrite`, `SpuSetTransferStartAddr`,
`SpuSetVoiceAttr` and `SpuSetCommonAttr`, defines transfer/voice contracts. In
particular, DMA transfers must be multiples of 64 bytes and use global/heap data.

## Optional example audio generators

With the separate Shadow Ops archive installed, these development scripts
regenerate its gunshot and looping background track through the native importer:

```sh
make
python3 tools/generate_shadow_audio.py
python3 tools/generate_shadow_music.py
```

They modify example assets and are not part of the editor or game build pipeline.
The music generator adds full-sample SPU loop markers explicitly; WAV imports
from the UI remain one-shots. Keep generated banks small because samples occupy
both executable memory and SPU RAM.
