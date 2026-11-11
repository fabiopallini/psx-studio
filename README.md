# PSX Studio

Tools for creating original PlayStation games on Linux and Windows 11:

- **PSX Studio** is a visual editor with a Windows 95/98-style interface, a 3D
  preview, scenes, cameras, collisions and asset import. It exports `.psx`
  projects to ANSI C89 applications.
- **PSX Lib** is the included C89 framework and native build tool. It provides
  rendering, rigid-part animation transforms, input, audio, collision helpers,
  fixed-step updates, resource conversion and PS1 CD builds. It can also be used
  independently, with games written directly in C.

Studio uses PSX Lib for game builds. The framework accepts C sources and resource
lists; it does not read `.psx` files or generate gameplay.

![Shadow Ops in the 3D editor](screenshots/screenshot.png)

![Sanitarium with a pre-rendered background and 3D actors](screenshots/screenshot2.png)

## Requirements

To build PS1 games, supply your own **Psy-Q SDK** and put **dosbox-x** in `PATH`.
The SDK folder must contain `psyq/CCPSX.EXE`, `psyq/PSYMAKE.EXE`, `include/` and
`lib/`. An emulator and BIOS are external too. Keep PSX Lib's runtime and build
resources with its executable.

**Python is not required to run Studio, convert assets or build games.** It is
used only by developer tests and optional asset-generation tools.

## Build the tools

### Linux

Both components need a C11 compiler, Make, pkg-config, GLib/GIO, JSON-GLib and
libpng. The editor additionally needs GTK 4.12+, libepoxy and OpenGL 3.2 or
OpenGL ES 3.0.

Ubuntu 24.04+:

```sh
sudo apt install build-essential pkg-config libgtk-4-dev libjson-glib-dev libepoxy-dev libpng-dev
```

Fedora:

```sh
sudo dnf install gcc make pkgconf-pkg-config gtk4-devel json-glib-devel libepoxy-devel libpng-devel
```

Then build and launch:

```sh
make
./build/psx-studio
```

For PSX Lib alone, GTK and OpenGL are unnecessary. On Debian/Ubuntu install
`build-essential pkg-config libglib2.0-dev libjson-glib-dev libpng-dev`, then run
`make lib`. Its executable is `psx-lib/build/psx-lib`.

### Windows 11

Install [MSYS2](https://www.msys2.org/), update it with `pacman -Syu`, then install
these packages in its **UCRT64** terminal:

```sh
pacman -S --needed make mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-pkgconf \
  mingw-w64-ucrt-x86_64-gtk4 mingw-w64-ucrt-x86_64-json-glib \
  mingw-w64-ucrt-x86_64-libepoxy mingw-w64-ucrt-x86_64-libpng \
  mingw-w64-ucrt-x86_64-angleproject
```

From PowerShell in the repository folder, run `.\build.ps1`. Use
`-Msys2 'D:\msys64'` for a different MSYS2 location. If needed, launch it with
`powershell -ExecutionPolicy Bypass -File .\build.ps1`.

The distributable is `build/windows/psx-studio/`. Keep the whole folder together,
including `psx-lib.exe`, DLLs and `psx-lib/` resources. Recipients do not need
MSYS2 or Python. Keep personal projects outside this generated package folder.

For the framework alone, run `.\build.ps1 lib`. It needs only Make, GCC,
pkgconf, GLib, JSON-GLib and libpng from MSYS2 UCRT64; no GTK or ANGLE.
Its executable is `psx-lib/build/psx-lib.exe`.

### Build targets

| Command | Action |
| --- | --- |
| `make` | Build Studio and PSX Lib |
| `make studio` / `make lib` | Build either component |
| `make test` | Run both test suites; requires Python |
| `make test-studio` / `make test-lib` | Run either suite |
| `make clean` | Clean both generated build folders |
| `make -C psx-lib` | Build the framework independently |
| `make -C psx-lib test` | Test the framework independently |

On Windows, `.\build.ps1 test`, `test-studio` and `test-lib` select the same
suites. The `psx-lib/` source folder can also be copied and built on its own;
its build and tests do not require the editor or parent Makefile.

## Create a game with Studio

1. Create a project and save it as a `.psx` file.
2. Import low-poly OBJ models, PNG textures and WAV audio; arrange your objects.
3. Add collisions, cameras and any custom C89 gameplay modules.
4. In **File → Settings**, select the Psy-Q SDK and optionally an emulator command,
   such as `pcsxr -cdfile {cue}`. PSX Lib is found automatically.
5. Choose **Build → Build**, then **Run emulator**.

The build log opens when a build starts and reopens automatically on failure.
Use the bottom **Build log** button to show it at any time, including while
building, and **Copy log** to copy diagnostics. Compiler errors are included
even when DOSBox-X exits unsuccessfully.

Keep `MyGame.psx` and `MyGame_psx_data/` together. Use Scenes for multiple levels.
The **Background** tab supports a pre-rendered PNG and transparent foreground
layers with camera-space depth; author their physical collision volumes separately.

Right-drag to orbit, scroll to zoom, Shift + scroll to move sideways and
Ctrl + scroll to move along Y. The preview helps author scenes but does not
execute C modules or reproduce PS1 rendering exactly.

Generated C lives in `build/studio-projects/NAME-HASH/source/`; game output is
`build/engine/ps1/NAME-HASH/`. Open `cdrom/game.cue` in your emulator and keep it
with `cdrom/GAME.ISO`.

The same game build is available without opening the UI:

```sh
./build/psx-studio --build-ps1 --project path/to/game.psx --psyq path/to/psyq
```

To export only, use `--export path/to/game.psx --output path/to/export`. Choose
an empty destination initially; later exports prune obsolete Studio-owned files.
The project format remains version 3. Older gameplay modules using retired
HUD/inventory fields need migration to the framework's overlay APIs; see
[compatibility and validation](spec/game-development.md#known-limitations).

## Create a game directly in C

Start with [psx-lib/examples/c-starter](psx-lib/examples/c-starter). Your application
owns `main.c`, objects, cameras, gameplay, overlays and scene loading. Include
`object.h` for runtime APIs and `psx.h` for console services.

After building PSX Lib, compile your game:

```sh
./psx-lib/build/psx-lib --build-ps1 --source-root path/to/game \
  --psyq path/to/psyq --build-directory path/to/game/build --name my-game \
  --max-objects 64 --render-triangles 1024
```

Use unique, flat DOS 8.3 C/header filenames. Additional `.c` units are linked
automatically. Set object and triangle capacities for your game, then verify
memory usage and performance in an emulator. Runtime code is strict C89 and
uses integer/fixed-point arithmetic at 50 simulation steps per second.

For OBJ/PNG/VAG conversion during a build, add `--resources path/to/reslist.h`
and `--asset-root path/to/assets`. The C asset list describes resource banks,
not gameplay scenes. Preconverted 16-bit TIM files can also be supplied directly.
Studio exports include `reslist.h` and `resources/`; pass those with the same
options when building an exported project yourself.

Standalone asset conversion, WAV import and output verification are available too:

```sh
./psx-lib/build/psx-lib --resources path/to/reslist.h \
  --asset-root path/to/assets --output path/to/resource-export
./psx-lib/build/psx-lib --import-wav effect.wav --audio-directory path/to/assets
./psx-lib/build/psx-lib --build-ps1 --verify path/to/game/build/ps1/my-game
```

`--prepare-only` prepares a game build without starting DOSBox-X and invalidates
previous final artifacts. Standalone resource conversion checks assets without
touching a playable CD. Successful builds retain a content-hashed object cache;
later builds compile changed units, then relink, package and verify the CD.
See the [framework reference](spec/framework.md) for resource formats, APIs,
cache rules and the isolated DOSBox-X profile.

## Examples and reference

The optional **examples ZIP** on GitHub Releases contains **Shadow Ops**
(`shadow-ops.psx`), a tactical shooter, and **Sanitarium** (`sanitarium.psx`),
a fixed-camera horror demo with pre-rendered backgrounds and depth layers.
Extract it into the repository folder and open a project with **File → Open**.
Keep each `.psx` with its data folder. Shadow Ops asset generators, benchmark
preparation and art sources are included in `examples/shadow-ops_tools/`. These projects are distributed separately
and are not required to build the tools; the C starter is included in the source.

Detailed documentation:

- [Game development](spec/game-development.md): authoring, workflow and validation.
- [C89 modules](spec/modules.md): gameplay callbacks and runtime APIs.
- [PSX Lib](spec/framework.md): C applications, resource banks and build contracts.
- [Audio](spec/audio.md) and [textures](spec/textures.md): asset formats and conversion.
- [Architecture](spec/architecture.md): editor/framework responsibilities.

Shared development instructions are in the root [AGENTS.md](AGENTS.md) and
[spec.md](spec.md). Host tests do not establish console compatibility or FPS;
SDK builds and emulator checks remain separate validation steps.
