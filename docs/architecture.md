# Architecture

PSX Studio has three main parts: the desktop editor, a native export/build
pipeline, and the PlayStation runtime. Games supply their own assets and C89
modules; the engine does not depend on Shadow Ops or another example.

## Source layout

| Path | Responsibility |
| --- | --- |
| `editor/main.c` | GTK4 UI, project actions and asynchronous build launch |
| `editor/project.c`, `editor/scenes.c` | Project validation, persistence and Scene snapshots |
| `editor/assets.c`, `editor/audio.c` | Native OBJ/PNG import, TIM and WAV-to-VAG conversion |
| `editor/viewport.c` | OpenGL/GLES editor preview and object manipulation |
| `editor/export.c` | Project-to-C89 export, geometry, textures and module dependencies |
| `editor/build.c` | Native build preparation, locking, DOSBox launch and output verification |
| `runtime/` | Portable C89 objects, movement, collisions, camera and timing helpers |
| `pipeline/project-main.c` | Shared exported game loop |
| `pipeline/template/src/` | Psy-Q rendering, input, sound and memory-card backends |
| `pipeline/cdrom/` | CD image tools and boot resources |
| `tools/` | Windows packaging and optional development asset generators |
| `tests/` | Host checks for editor/export, runtime and build pipeline |
| `docs/` | Development workflow and technical reference guides |
| `examples/` | Optional projects installed from a separate release archive |
| `build/` | Generated binaries, logs and game images; ignored by Git |

The desktop application is C11. Console code and exported modules are strict
ANSI C89. Python is used only by optional asset generators and developer tests.
The Psy-Q SDK, DOSBox, emulator and BIOS are external dependencies.

## Projects and Scenes

JSON format version 3 stores Scenes with stable IDs. Each Scene contains its
objects, camera and view settings. The root retains a compatibility mirror of
the editor's active Scene; Scene entries are authoritative when loading.
Versions 1 and 2 load as single-Scene projects.

`editor/scenes.c` snapshots the active Scene before switching. Undo uses deep
JSON copies and records selection as well as project state. Saves are atomic;
Save As copies the accompanying asset folder. References are relative to
`PROJECT_psx_data/`, so the project and folder must travel together.

Export writes C89 Scene defaults, mesh tables, renamed modules and sound banks.
Identical immutable geometry and ADPCM bytes can be shared across Scenes;
live objects and audio-bank metadata remain separate. All geometry and module
code are resident in the executable. Scene textures are loaded from CD and reuse
VRAM slots. Runtime transitions clean up the previous Scene, reset live object
storage, initialize the destination and reload its resources.

## Native build pipeline

The UI starts the same executable with `--build-ps1` through GSubprocess,
keeping GTK responsive. `build.sh` is a convenience wrapper for this command.
The native pipeline snapshots the project/assets, exports sources and TIM files,
runs Psy-Q through DOSBox, and verifies the executable and CD contents.
Project source lists use a DOS 8.3 `sources.rsp` CCPSX control file so multiple
Scene modules do not exceed DOS's 126-byte command-tail limit.
It mounts the SDK read-only and invalidates previous final artifacts before export.

UI output is `build/ps1/PROJECT/`. CLI output-root overrides and legacy custom
settings use isolated `NAME-HASH/` subfolders. Native Windows locks or Linux
`flock` prevent simultaneous builds in the same folder. Ownership markers prevent
one project from overwriting another project's output.

Settings stores SDK, DOSBox and emulator configuration. Build metadata records
the effective SDK without persisting CLI overrides into the project. Output
commands require a successful build with valid markers and CD files.

Windows packaging includes GTK resources, DLL dependencies and explicit ANGLE
EGL/GLES libraries. It checks shader rendering and executable loading with MSYS2
removed from PATH. The Windows editor links with the GUI subsystem to avoid
an extra console window; captured CLI/build output is preserved. The GDBus
session helper ships beside GIO so session-bus autolaunch can find it.
Examples are packaged separately.

## Editor and game cameras

The editor uses a look-at orbit view with perspective/orthographic and axis
presets. Camera objects store eye position, pitch/yaw, zoom, projection and an
optional follow target. View through camera and Capture editor view explicitly
copy framing between them; ordinary editor navigation does not move a game camera.
Projects without an active camera use the legacy Scene view.

The preview and exporter share UV endpoint rounding and RGB555 texture conversion.
OpenGL uses affine texture interpolation, but desktop depth precision, clipping
and module-free preview behavior differ from the PS1. It is not a GPU emulator.

## Simulation and rendering

Gameplay runs at 50 fixed PAL steps per second. `psUpdate()` services the integer
clock and fresh pad input in the main thread, including between rendering batches.
Catch-up is bounded after exceptional stalls. Blocking Scene/card loads reset
timing and wait for neutral input. Callbacks never execute in interrupts.

Rendering uses a frozen object/camera/HUD snapshot. Two packet buffers allow CPU
preparation to overlap GPU work; `runtime/frame.h` tracks ownership. GPU polling
is nonblocking during gameplay. The VBlank callback presents completed images;
rendered FPS and simulation updates are independent. Long individual callbacks
can still delay cooperative scheduling.

Transforms, projection and clipping use integer/fixed-point math. Q12 scales and
zoom use 4096 for 1.0; rotations use 4096 units per turn. Console camera builds
exclude desktop floating-point helpers. Shared-position and local-geometry caches
reduce repeated work; GTE transforms have an integer fallback for wide coordinates.
Meshes must stay immutable while snapshots/caches reference them. On pose swaps,
the renderer relocates the shared-position map from another scene object using
that immutable mesh (including hidden animation references), falling back to
hashing for unknown module meshes. Camera trigonometry is reused until the
snapshot angles change; object trigonometry is calculated once per draw. Render
yields remain between objects, every 16 unique projected positions and every 16
triangles, preserving fixed-step input service during expensive frames.
For meshes with at least 32 triangles, a cached local L1 radius supplies a
conservative view-space bound before vertex transformation. Scale uses the
largest absolute Q12 component; a 16-unit plus 1/64-radius allowance covers
rotation/truncation error. Near-plane intersections and oversized coordinates or
zoom fall back to the full path. Rejected objects keep their projection cache
invalid so re-entry cannot reuse uncomputed geometry.

Ordering-table flags explicitly handle flat backdrop ground, translucent decals,
backface rejection and emissive effects. Ordinary geometry retains depth sorting;
there is no general depth buffer. See [module APIs](modules.md) for flag contracts,
[audio](audio.md) and [textures](textures.md) for formats, and
[game development](game-development.md#commands-and-verification) for validation
procedures and known limitations.

The application version is `STUDIO_APP_VERSION` in `editor/studio.h`; the title
shows it beside the application name. `STUDIO_VERSION` independently identifies
the JSON project format. Do not change the data format merely to release the editor.
