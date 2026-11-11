# Architecture

PSX Studio is a desktop authoring tool and a C89 application exporter. PSX Lib
is an included, independently buildable framework in `psx-lib/`. Studio never sends `.psx` documents to the engine and
does not ship a second console runtime, SDK backend or DOSBox launcher.

| Path | Responsibility |
| --- | --- |
| `editor/main.c` | GTK4 UI, project actions, settings and asynchronous build launch |
| `editor/project.c`, `editor/scenes.c` | Project persistence, validation and Scene snapshots |
| `editor/assets.c`, `editor/audio.c` | Native OBJ/PNG import, TIM and WAV-to-VAG conversion |
| `editor/viewport.c`, `editor/camera.c` | Desktop preview, picking and manipulation |
| `editor/export.c` | `.psx` to C89 object/scene/collision tables, modules and TIM assets |
| `editor/game-main.h` | Embedded application entry point using engine public APIs |
| `editor/build.c` | Generated-source ownership/locking and included framework invocation |
| `tests/` | Editor, export and engine-boundary checks |
| `tools/` | Windows packaging and optional developer asset generators |

The editor is C11 with GTK4, JSON-GLib, Epoxy and libpng. Generated game code and
modules are strict ANSI C89. Python is only an optional developer-test/asset-tool
dependency. `make studio` compiles the editor independently; game builds need the included
framework executable built by `make lib` or the default `make`.

## Authoring and application generation

JSON format version 3 stores Scenes with stable IDs. Each Scene contains objects,
cameras, collision settings and background/depth layers. The root mirrors the
active Scene for compatibility; versions 1/2 load as single-Scene projects.

Scene snapshots and undo use deep JSON copies. Saves are atomic; Save As copies
the accompanying `PROJECT_psx_data/` folder. Keep the project and data together.
Project settings include `psyq_path` and `emulator_command`. Studio always
locates its included PSX Lib without a framework setting. Old `engine_path`
and `dosbox` settings are ignored and omitted on save. DOSBox-X comes from PATH.

The exporter creates a complete application: `main.c`, `scene.c`, `scene.h`,
`sNNN.h` Scene tables, `lNNN.h` module bodies, `mNNN.c` independently compiled
logic wrappers, `reslist.h`, snapshotted `resources/` and `assets.json`. Lowercase DOS 8.3 C/include names work
with engine source normalization on case-sensitive hosts. Include-only Scene
and module bodies are headers so the engine does not compile them twice.

The exporter maps each Scene to an engine resource bank, independently of its
Scene ID. It emits C89 object defaults/collisions/cameras and binds meshes through
`psx_resource_mesh`, then applies resource metadata with `psx_resource_apply`.
The engine converts assets through its `--resources` C-list contract, shares
immutable geometry/sample arrays and packages textures in `ASSETS/Bnnn/`.

Immutable geometry and sample bytes can be shared across Scenes; writable
object arrays, bank metadata and application state remain separate. Scene changes
clear old objects, restore exported defaults, initialize modules and load resources
at GPU-idle boundaries. The largest Scene determines `--max-objects`. Generated
main uses `psUpdate`, `psBeginFrame`, `psDrawScene`, `psDrawOverlay` and `psDisplay`.
Gameplay modules use the included framework's `object.h` and other public APIs.

Exports record owned filenames in `.studio-export`. A later export prunes obsolete
owned files so removed modules cannot remain in direct-C builds. Export into an
empty folder initially; Studio does not overwrite an arbitrary source tree.

## Build boundary

The UI launches the same executable with `--build-ps1` using GSubprocess. `build.sh`
is a convenience dispatcher. Studio resolves the included executable under
`psx-lib/build/` in a checkout or `psx-lib.exe` beside the packaged Windows editor.
An explicit CLI `--engine` override is retained for development and tests only.
The UI never asks for a framework path or searches a sibling checkout/PATH.

The root Makefile delegates framework build/test/clean to `psx-lib/Makefile`.
The latter works independently without GTK or editor sources. The source tree
has one root `AGENTS.md` and `spec.md`; framework contracts are in
[framework reference](framework.md). No nested instruction file is needed.

Studio exports into `build/studio-projects/NAME-HASH/source/`. Names include a
hash of the canonical project path so equal basenames remain isolated. A native
Windows/Linux lock protects each source workspace, and an ownership marker
prevents overwriting another project. Failed export invalidates an existing
engine build's success receipt. Generated sources are regenerated completely;
the engine keeps its own incremental object cache outside them.

Studio invokes `psx-lib --build-ps1 --source-root ... --name ...
--build-directory ... --psyq ... --max-objects ... --render-triangles 8192
--resources .../reslist.h --asset-root .../resources`.
The engine owns runtime/backend injection, SDK validation, compilation, DOSBox-X,
cache receipts, output locks and EXE/CD verification. Its installed resources
must match its executable. `--prepare-only` is forwarded without starting DOSBox.
`--verify` delegates to the included framework, without a Studio ISO verifier.

Output defaults to `build/engine/ps1/NAME-HASH/`, isolated from legacy Studio
builds. Successful builds gain a `.studio-project` receipt for UI output discovery;
engine ownership markers continue to identify the generated C source directory.
CLI `--output-root` and legacy custom output settings retain engine hashed folders.
Legacy playable builds can still be discovered. The SDK can be recovered from
engine `build-info.json`; CLI overrides never rewrite saved project settings.

## Desktop preview and packaging

The viewport uses desktop-only floating-point camera helpers. It previews affine
texture interpolation, backgrounds and depth layers, but does not execute C
modules or emulate PS1 ordering, clipping, animation or gameplay.

Windows packaging includes GTK resources, DLL dependencies, ANGLE, GDBus and
`psx-lib.exe`, plus framework `runtime/` and `pipeline/` resources under `psx-lib/`.
DLL dependencies are collected for both executables. Settings contains SDK and
emulator configuration only; DOSBox-X must be in PATH. Windows execution
requires its own validation session.

The application version is `STUDIO_APP_VERSION`; JSON `STUDIO_VERSION` remains
independent. See [modules](modules.md) for application APIs and
[game development](game-development.md) for verification and limitations.
