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

## Windows console and GDBus packaging check

The Windows package was rebuilt with the editor PE subsystem set to Windows GUI.
Packaging verified captured editor and native-build help output, and loaded the
packaged GDBus helper with MSYS2 removed from PATH. The EGL/ANGLE pbuffer shader
and rendering checks passed. The published package includes gdbus.exe beside
libgio. No interactive GUI, Psy-Q build or emulator check was performed for this
linker and packaging change.

## Shadow Ops grenadiers and OBJ viewer — October 27, 2026

The optional examples installed in this workspace now contain four gameplay
Scenes plus the title menu. New Scenes 4/5 are Grenadier Foundry and Night Relay
Complex; warehouse extraction leads into them. Guards use original 176-triangle
immutable poses (agent poses remain 84). The native export reports 2353 resident
triangles for the complete project. The optional examples and viewer remain
ignored by Git and must accompany the separately distributed examples archive.

Checks actually performed on Linux:

- `make` and `make test` passed, including all 23 project/export checks and 11
  native pipeline tests. The additional pipeline fixture uses 20 logic modules
  to check the CCPSX control file and short DOS command tail.
- The C89 Shadow Ops harness passed after the final test additions: campaign
  transitions, direct grenade impact, evasion, blast shielding, horizontal wall
  interception, warning/effect cleanup, surviving airborne grenades after the
  thrower dies, and equivalent combat under uneven render batches.
- A real Psy-Q build of the final five-Scene project passed with
  `-pedantic-errors`. Native validation accepted its PS-X EXE, CD sectors, ISO
  contents, textures and CUE. Output is `build/ps1/shadow-ops/`.
  DOSBox Staging ran under isolated Xvfb with primary/local configurations
  disabled and explicit software texture output. An initial headless GLX failure
  and the DOS command-tail failure were resolved before the successful build.
- A separate ignored diagnostic project copied the final new sectors, booted
  Scene 5, moved the player/one guard into a combat test arrangement and enabled
  the standard FPS HUD. It also built with Psy-Q. PCSX-R with a disposable
  configuration/card directory ran it under isolated Xvfb. Captured frames show
  a grenade landing ring, damage, live movement, aim/fire and camera follow.
  The repeated 11-second check after compilation finished showed approximately
  11–15 displayed FPS in inspected movement/combat samples. An earlier concurrent
  compilation sample dipped to 5 FPS. These are emulator observations, not a
  hardware performance guarantee; graphical fluidity still needs optimization.
  SDL audio used the dummy backend, so boom audibility was not assessed.
- The local Three.js viewer passed syntax checks and headless Chrome interaction
  checks: automatic preview, stepping, side-by-side view, wireframe, throw poses,
  multiple local OBJ files with PNG, numeric sorting/reordering, an empty invalid
  OBJ and clearing. Browser JavaScript emitted no page errors. Screenshots were
  visually inspected. Its test server and browser were stopped afterward.
- The editor saved a Scene 5 preview and exited successfully under Xvfb, but
  emitted the previously documented GTK widget assertions during teardown.
  This is not a clean editor GUI regression result.

Generated logs and screenshots live under ignored `build/shadow-*` paths.
The full campaign was covered by the host harness, not manually completed in
PCSX-R. No Windows build, physical console test or memory-card save/load on the
new sectors was performed; new sectors deliberately have no save checkpoint.

### Standalone viewer follow-up — October 27, 2026

`examples/3d-viewer/index.html` now embeds the bundled JavaScript, styles,
Three.js MIT license and character preview OBJ/PNG data. npm is only an optional
maintainer tool for regenerating that HTML, not a user runtime requirement.
Headless Chrome passed the same viewer interactions with networking disabled
and `file://` URLs, first in the examples folder and then with only index.html
copied into `/tmp`. Local OBJ/PNG selection, pose order/playback and bundled
previews worked without a server or sibling files; no JavaScript page errors
were observed. JavaScript syntax checks passed. Browser test processes exited.

### Viewer cleanup and restored shot colours — October 27, 2026

The viewer folder now contains only `index.html` and `README.md`. Removed npm
manifests, node_modules, the optional server, bundler and separate development
sources. The self-contained HTML was unchanged. The offline `file://` browser
interaction check passed again after removal, including local OBJ/PNG selection,
pose playback/reordering and built-in previews, with no JavaScript page errors.

The sector generator had replaced `atlas.png` with `industry.png` on the twelve
tracers and three muzzle flashes in each new Scene. Their original UV sample
points changed from warm RGB (244,180,108) and white (255,255,255) to industrial
grey (76,86,90) and (56,67,71). Restored `atlas.png` on those 30 objects in Scenes
4/5 without regenerating the Scenes or changing user transforms. The generator
now keeps the original shot-effect atlas when replacing scenery materials.
A regression assertion checks the tracer/muzzle texture in every Scene.
The focused example host/export harness and native five-Scene export passed.
The colour correction also passed a real Psy-Q build with PS-X EXE/CD validation.
A rebuilt diagnostic copy booting the relay Scene was exercised in isolated
PCSX-R; `build/shadow-shot-emulator-24.png` visibly shows the restored warm tracer
with its white-hot tip. Dummy audio was used, and test processes were stopped.

### Main Menu as Scene 1 — October 27, 2026

The example now orders and numbers Main Menu as Scene 1, Outer Yard as Scene 2,
Supply Warehouse as Scene 3, followed by the unchanged foundry/relay IDs 4/5.
Startup and the editor's active Scene are both 1. All passage references and the
model/sector/passage generators were updated. Existing card payload sector
codes remain 1 for yard and 2 for warehouse; mission/menu modules translate these
to/from current Scene IDs, preserving existing checkpoint meaning without
changing the console card format. The host harness covers both legacy load
codes, saving from both original sectors, startup/New Game and campaign travel.
`make test` passed after the renumbering, including the complete example harness
and all 11 pipeline tests. The editor screenshot confirms `1: Main Menu [start]`
selected on open; it still emitted the known GTK widget assertions at teardown.
The final Psy-Q rebuild passed PS-X EXE/CD validation (an earlier attempt was
terminated with exit 143 and was rerun successfully). Isolated PCSX-R then booted
the title menu and entered the yard through New Game; screenshots
`build/shadow-menu-emulator-16.png` and `-40.png` were visually inspected.
Emulator/Xvfb processes were stopped. Card-code mapping was tested on the host;
no physical memory-card I/O or Windows verification was performed in this follow-up.

### Character silhouettes and render work — October 27, 2026

Shadow Ops agent poses now contain 174 triangles and guard poses 200, with
bevelled torsos, connected necks/shoulders, tapered sleeves and six-sided legs
with distinct boot collars/toes. Collider dimensions, scene numbering, gameplay
logic and coloured shot atlases are unchanged. The offline viewer's embedded
OBJ previews were refreshed. Its `file://` Chrome check passed with networking
disabled, covering pose stepping, side-by-side/wireframe, grenade throw, local
multi-OBJ/texture import, reordering, invalid input and clearing.

The renderer reuses immutable pose position maps, computes object trigonometry
once per draw, caches camera trigonometry and polls logic every 16 unique
positions instead of every 16 flattened corners. Swept movement skips pairs
that cannot block or notify game code. New host coverage checks relocation of
shared indices in both directions and collision callbacks that make a later
object solid during the same movement substep. Conservative sphere rejection
also has host coverage across orthographic/perspective viewport edges, near-plane
intersections and fallback ranges. The first host compile caught an uncalled new
test helper; it was connected and the complete suite rerun. `make test` passed, including
the example harness and all 11 pipeline tests. A real Psy-Q build of the
instrumented five-scene project passed EXE/CD validation; its linker map ended
BSS at `0x801f2708`, below the `0x801ffff0` initial stack (55,528 bytes of address
space between them, not a measurement of stack high-water usage).

The final instrumented build (including conservative bounds) ended BSS at
`0x801f32e8`, leaving 52,488 bytes before the initial stack address. The actual
five-scene game was rebuilt with Psy-Q and passed PS-X EXE/CD contents validation.

The diagnostic copy uses 400 fixed simulation steps per sector, identical
scripted input in both versions, normal geometry/colliders/camera/AI, replenished
health/ammo and a disabled checkpoint prompt. `tools/prepare_shadow_benchmark.py`
creates this disposable copy without editing the actual game. Start through New
Game: initial direct-to-gameplay diagnostic launches stalled at loading in this
PCSX-R setup. The emulator did not forward game `printf` messages; measurements
instead read the existing `PsxFrameRate` counter from the owned emulator's RAM,
without writing RAM. Launches used isolated Xvfb, llvmpipe, dummy audio and no
concurrent compilation. The first carried-over menu/previous-scene FPS value
was discarded, then seven completed approximately one-second FPS windows were
averaged per sector, excluding the final stationary diagnostic period.

| Scene | Original mean FPS | Updated mean FPS | Updated sampled range |
| --- | ---: | ---: | ---: |
| Outer Yard | 21.83 | 22.21 | 16.6–31.0 |
| Supply Warehouse | 20.27 | 20.89 | 16.0–31.0 |
| Grenadier Foundry | 15.23 | 15.00 | 12.0–25.4 |
| Night Relay Complex | 14.93 | 15.31 | 12.0–22.5 |

These are single deterministic emulator runs, not physical-console performance
or proof of a statistically significant speedup. They show broadly preserved
frame rates with the higher-detail characters; the heavier sectors still fall
well below a stable 30 FPS. An intermediate version without conservative bounds
was slower with the new assets and was superseded. The fixed-step host harness
continues to check gameplay equivalence at different rendering cadences.
Raw samples, linker maps and captured frames are in ignored `build/perf/`.

The final uninstrumented game was then exercised separately in isolated PCSX-R:
New Game, directional movement/camera follow and three aim/fire cycles worked;
HUD ammo fell from 36 to 33 and enemy fire reduced health. Captures
`build/shadow-final-emulator-48.png` and `-80.png` were visually inspected, as
was the updated guard throw preview. The normal game has no benchmark overrides.
Owned emulator/Xvfb processes were stopped. This was not a full manual campaign,
physical-console, Windows, audible mix or memory-card I/O verification.
