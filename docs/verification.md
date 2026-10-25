# Verification

This guide separates repeatable checks from historical observations. A passing
host test does not prove a Psy-Q build, emulator behavior or physical hardware
compatibility. Record the commit, commands, environment and limitations for new
checks; do not treat ignored screenshots or old logs as permanent test fixtures.

## Automated host checks

On Linux:

```sh
make
make test
```

On Windows, after the MSYS2 setup in the [README](../README.md#windows-11):

```powershell
.\build.ps1 test
```

Python is required for the pipeline test harness, not production builds.
The suite checks project persistence and validation, Scene transitions, asset
import/export, native audio conversion, C89 runtime behavior, camera math,
input, fixed-step timing, frame ownership, sound and checkpoint encoding.
Pipeline tests exercise source preparation, ownership/locking, stale artifacts,
sound snapshots and CD validation with synthetic fixtures.

Example integration tests run only when the separate examples archive is
installed. Missing examples skip those tests; generic tests generate their own
fixtures. A skip is not evidence that the corresponding game was verified.

## Export and Psy-Q checks

Run from the repository root, using an actual project and your SDK path:

```sh
./build/psx-studio --export /path/to/Game.psx --output build/check-export
./build/psx-studio --build-ps1 --project /path/to/Game.psx --psyq /path/to/psyq
./build/psx-studio --build-ps1 --verify build/ps1/Game
```

On Windows use `psx-studio.exe`. The exporter validates project/asset contracts
without a display or SDK. Full builds require DOSBox and a display. Final native
verification checks the EXE header and CD contents; inspect `src/BUILD.LOG`,
`dosbox.log` and `texture.log` for failures. `--prepare-only` invalidates previous
final outputs and does not prove compilation.

Changes to console arithmetic, generated C or assets need a real Psy-Q build
and emulator inspection when tools are available. Console code must stay C89
and integer/fixed-point, including 32-bit-safe intermediate calculations.

## GUI and emulator checks

Follow the isolation rules in [AGENTS.md](../AGENTS.md). On Linux, use Xvfb,
unset Wayland variables and explicitly select X11 for GTK and SDL. For DOSBox
Staging, use a temporary software-texture configuration without loading the
user's configuration. On Windows use a dedicated test VM/session.

Check editor layout, selection, imports, camera controls, save/reopen and build
messages as relevant. A static `--smoke-test` screenshot does not exercise the
interactive UI or gameplay. Earlier Linux smoke runs saved a PNG and exited zero
while emitting GTK teardown assertions; check stderr as well as exit status.

For games, use a disposable emulator profile and memory cards. Exercise input,
movement/collisions, aiming, damage, audio overlap, transitions and loading/save
prompts. Stop owned processes afterward and preserve user settings. State
explicitly if debugger/RAM manipulation was used to set up a visual scenario.

Measure FPS with movement, camera changes and combat, not just a stationary
view. Gameplay uses 50 fixed PAL steps/s independently of displayed FPS; verify
equivalent behavior at different render cadences. Historical FPS samples varied
with the map and assets and do not establish a minimum performance guarantee.

## Latest recorded checks — October 25, 2026

- After fixing the gameplay fixture to mark its player as solid, `make test`
  passed on Linux: all 23 project tests, the C89/native backend checks and all
  10 pipeline tests. The fixture previously expected a non-solid player to stop
  against a wall; no runtime collision change was needed.
- The project suite also passed in an isolated directory without examples,
  skipping the three example integration tests. Sound export uses a generated
  silent VAG fixture rather than a Shadow Ops asset.
- Windows packaging now excludes examples. This packaging adjustment was
  reviewed on Linux, not executed on Windows in that check.
- Subsequent documentation-only edits do not imply new SDK, GUI or emulator runs.

## Earlier verification summary

Earlier development checks recorded successful Psy-Q builds and isolated PCSXr
runs for fixed-point rendering, input/render separation, Scene changes, sound,
manual checkpoints and Shadow Ops gameplay. These describe the builds tested
then, not every later revision. Some passage visual checks positioned the player
and cleared guards through owned-emulator RAM writes; they were controlled setup
checks rather than complete natural-combat playthroughs.

The October 25 Windows work recorded relocation and source preparation from the
portable package with MSYS2 removed from PATH and a stub SDK. An ANGLE EGL
pbuffer probe compiled the actual shaders and checked untextured pixels, affine
UV interpolation and RGB555 quantization using the Microsoft Basic Render Driver.
Packaging repeats the graphics/load checks. These checks do not by themselves
prove a full Psy-Q build or interactive GUI behavior on every target machine.

Physical PS1 hardware and Windows 10 have not been verified. Keep these limits
separate from Linux/Windows 11 host checks and emulator results.
