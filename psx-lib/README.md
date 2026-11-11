# PSX Lib

A reusable PlayStation runtime/framework and native build tools. It provides
C89 rendering, audio, controller input, collision helpers, fixed updates,
resource loading and a Psy-Q/DOSBox build pipeline. Gameplay, menus, inventories,
icons and game-specific data models belong to consuming projects.

This folder builds independently with `make` and `make test`, without the
parent Makefile, GTK or Studio. From the repository root, `make lib` and
`make test-lib` delegate to those commands.

Applications are written in C. The engine does not load or export `.psx` documents.
No editor, GTK, OpenGL, Python, SDK or BIOS is embedded in the tool.

## Build the host tool

Linux dependencies: C11 compiler, Make, pkg-config, GLib/GIO, JSON-GLib and libpng.
On Debian/Ubuntu: `build-essential pkg-config libglib2.0-dev libjson-glib-dev libpng-dev`.

```sh
make
make test
./build/psx-lib --help
```

Windows: install MSYS2 UCRT64 with GCC, Make, pkgconf, glib2, json-glib and libpng,
then run `.\build.ps1` (or `.\build.ps1 -Test`). The executable is
`build/psx-lib.exe`. Windows execution is not verified by Linux tests.

## Build a C project

Start with [examples/c-starter](examples/c-starter), which owns its main.c:

```sh
./build/psx-lib --build-ps1 --source-root /path/to/game \
  --psyq /path/to/psyq --build-directory /path/to/game/build \
  --max-objects 64 --render-triangles 1024
```

Use flat DOS 8.3 C/header filenames. Additional C units are linked automatically.
Optional preconverted 16-bit TIMs are packaged under ASSETS. To convert original
OBJ/PNG/VAG assets, add `--resources /path/to/game/src/reslist.h` and
`--asset-root /path/to/game/game_data`. See the asset-list contract in ../spec/framework.md.
`--name my-game` selects a stable application/output name. --prepare-only
prepares the workspace without starting DOSBox. Compilation/packaging remain
native; Python is not a dependency.

The application can use update_game/prepare_overlay callbacks and generic
rectangle/image/text commands, or own its main loop with the lower-level APIs.
There are no built-in health/ammo fields, item types or prescribed menus.
The checkpoint service likewise uses an application-owned card filename/title.
Its PSXCP002 payload replaces the former example-specific format; legacy saves
require application-owned migration.

## Assets and build tools

Convert OBJ/PNG/VAG resources through the optional C asset-list interface:

```sh
./build/psx-lib --resources /path/to/game/reslist.h \
  --asset-root /path/to/game/assets --output /path/to/export
./build/psx-lib --import-wav effect.wav --audio-directory /path/to/game/assets
./build/psx-lib --build-ps1 --verify /path/to/game/build/ps1/my-game
```

For conversion during a C build, pass `--resources` and `--asset-root` together
with `--source-root`. The list defines resource banks, models, samples and plates;
it does not define gameplay objects, collisions, cameras or scene transitions.
The application owns those in C. See [the resource contract](../spec/framework.md#native-resource-conversion-for-c-applications).

Psy-Q, DOSBox-X, emulator and BIOS remain external. The SDK must contain
`psyq/CCPSX.EXE`, `psyq/PSYMAKE.EXE`, `include/` and `lib/`. The build creates
`BUILD.BAT`, `dos.conf`, prepared C89 sources/resources, `src/BUILD.LOG`, a
PS-X EXE and `cdrom/game.cue` + `cdrom/GAME.ISO`. Keep CUE and ISO together.
`--prepare-only` invalidates previous final output and prepares without DOSBox.
Use standalone resource conversion for asset checks without touching a playable CD.

The former `--project`, `--export`, `--validate` and `--exporter` options have
been removed. Neither legacy document versions nor `psx-game-1` are supported.
An editor such as PSX Studio must generate an ordinary C application and resource
list before invoking the engine; no document is passed to this framework.

## Incremental compilation

Native builds retain SDK object files in the managed build's `obj/` directory.
The first build fills the cache. Subsequent builds compile only units whose
content/dependencies, compile flags or SDK changed, then relink and regenerate
and verify the CD. `sources.rsp` lists C inputs; `objects.rsp` lists the exact
object files to link. The console prints the number compiled and reused.

Cache receipts use SHA-256 of the generated C/include closure and SDK inputs,
plus a digest of each object file. Missing, changed or uncommitted objects are
rebuilt; DOS timestamps are not used. Receipts are published only after a
successful SDK build and EXE/CD verification. Regenerating source snapshots still
removes obsolete application source files; removed object names are not linked.
`make clean` in the game removes the cache as well as the generated game.

The build launches `dosbox-x` from system PATH, in one session for compilation,
linking and CD packaging. No fallback to classic DOSBox/Staging or Flatpak is used;
missing dosbox-x produces an explicit error. Personal configuration is skipped
with -defaultconf and the generated configuration is passed with -conf.

The build profile uses 64 MB RAM, dynamic 386 emulation, fixed 200000 cycles,
and `private area size=131072` (128 KiB) for DOS kernel tables. EMS is disabled
to avoid its page frame overlapping the expanded private area in upper memory;
XMS remains available. This replaces the temporary compiler-batch restarts.
See the [DOSBox-X configuration reference](https://github.com/joncampbell123/dosbox-x/blob/master/dosbox-x.reference.full.conf).
The user confirmed the SDK build works with this profile. The agent did not
launch a compilation or emulator for verification.

## Layout

- `runtime/`: portable C89 simulation, camera/fixed math, frame and render helpers.
- `pipeline/template/src/`: Psy-Q GPU, SPU, pad and memory-card backends.
- `pipeline/cdrom/`: original CD-image packaging resources.
- `tools/`: C source build orchestration, native OBJ/PNG/TIM/VAG conversion,
  resource-bank validation and CD verification; no document loader, GTK or GL.
- `tests/`: portable runtime, resource conversion and C build/cache checks.
- `../spec/framework.md`: runtime, C application and resource contracts.
- `../spec.md`: shared development instructions for the repository.

The application owns its main loop and scene lifecycle. Resource conversion emits
immutable asset tables and writable bank metadata, without constructing objects
or discovering gameplay modules. Internal JSON manifests/cache receipts describe
build outputs only; they are not authoring formats.
