# Game development playbook

Read this when continuing `examples/shadow-ops.psx` or creating another game.
This is an operational guide, not a second engine specification. Read
[spec.md](../spec.md) first; inspect the current implementation when a detail
changes. Keep this guide updated when changing the format, workflow or game contract.

Example games are distributed separately through GitHub Releases. Extract the
examples archive into the repository root before following example-specific
commands and links below. The editor and native build pipeline do not require
the `examples/` directory. Example integration tests skip when their projects
are absent; generic export and audio tests remain independent of example assets.

## Start a session

1. Read the requested game's `.psx`, adjacent `*_psx_data/` folder and game guide.
   For Shadow Ops, start with `examples/shadow-ops.md` and
   `examples/shadow-ops_psx_data/mission.c` from the example archive.
2. Check `git status --short` and preserve the user's changes. `build/` and
   `todo.md` are ignored: neither is a durable source of project knowledge.
3. Choose the smallest layer to change using the table below. Gameplay changes
   normally need no editor/runtime changes.
4. Validate the project through the headless exporter, test the behavior, then
   build with Psy-Q and inspect gameplay in an isolated emulator when applicable.
   Report what was actually verified and any remaining limitations.

| Change | Source of truth |
| --- | --- |
| Map layout, object placement, colliders, camera, simple actions | `.psx` |
| Shape, UVs, appearance | OBJ/PNG in the game's data folder; Shadow Ops model generator |
| Combat, AI, inventory, objectives, custom transitions | Referenced C89 modules |
| Project loading/validation, export | `editor/project.c`, `editor/scenes.c`, `editor/export.c` |
| Shared simulation and console APIs | `psx-lib/runtime/object.h` and framework runtime |
| Console drawing/timing | `psx-lib/`; generated application glue in `editor/game-main.h` |
| Build orchestration | `editor/build.c` |

Never implement a permanent feature by editing generated files under `build/`.
The next export/build replaces them. Editor preview does not execute C modules,
AI or HUD logic, and OpenGL does not reproduce PS1 ordering/GPU behavior exactly.

## Editor picking checks

`build/viewport-tests` checks surface picking, nearest-object ordering, rotations,
scales, billboard transparency and camera clipping in both projections. It skips
when GTK cannot open a display. On Linux run GUI checks in an isolated session:

```sh
env -u WAYLAND_DISPLAY -u WAYLAND_SOCKET GDK_BACKEND=x11 SDL_VIDEODRIVER=x11 \
  GTK_A11Y=none GIO_USE_VFS=local LIBGL_ALWAYS_SOFTWARE=1 \
  xvfb-run -a dbus-run-session -- make test
```

The **Background** inspector tab holds the active Scene's base PNG and depth
layers independently of object selection. Check depth editing, removal, undo and
Scene switching there when changing its UI. Picking deliberately remains usable
through painted background masks, just like the selection outline.

## Editing `.psx` directly

The same loader and validator serve the UI and headless exporter. Direct JSON
editing is supported; the file is data, not an opaque editor document.

- Current version is **3**. `scenes` entries have `{id, name, data}`; objects and
  scene camera/view settings live inside **`scenes[i].data`**.
- `active_scene` selects the level open in the editor. `startup_scene` selects
  the level booted by the game. These are independent stable Scene IDs.
- The root also contains a compatibility copy of the active Scene's objects and
  view settings. The loader loads the active entry's `data`; changing only root
  `objects` will not change a multi-Scene game. Keep the root copy synchronized
  for fields you edit in the active Scene. Preserve root project settings/name.
- Object IDs are unique within a Scene; reuse across different Scenes is valid.
  `active_camera` and `follow_target` reference local object IDs; `target_scene`
  references a Scene ID. Keep references valid when adding/removing objects.
- Object kinds are `mesh`, `sprite`, `billboard`, `camera`. Asset/texture/module
  paths are relative to the adjacent `PROJECT_STEM_psx_data/` folder. Copy both
  the `.psx` and that folder when relocating or cloning a project.
- JSON positions, rotations and scales are editor values: rotations are degrees,
  scale/zoom 1 means unit scale. The exporter converts them into console integer
  values (4096 per turn, Q12 scales/zoom). Do not put runtime Q12 values into JSON.
- `bounds` are collider **half extents**, not full size or a visual mesh scale.
  `collider_offset` shifts the collision centre in world axes. Keep mesh origins,
  visual dimensions and collider dimensions aligned.
- `input_mode`: 0 none, 1 X/Z, 2 X/Y. `collision_action`: 0 none, 1 kill self,
  2 kill other, 3 Change Scene (with `target_scene`). Non-solid volumes can trigger.
- Avoid saving an editor window holding an older in-memory copy over external
  edits. Close/reopen that project or reload it before further UI edits.

For example, changing a spawn in a script must update the nested Scene and, if
active, the root mirror. Run from the repository root; choose the ID deliberately:

```python
import copy
import json
from pathlib import Path

path = Path("examples/shadow-ops.psx")
project = json.loads(path.read_text())
scene = next(s for s in project["scenes"] if s["id"] == 2)
player = next(o for o in scene["data"]["objects"] if o["id"] == 2)
player["position"] = [-190, 20, 150]
if project["active_scene"] == scene["id"]:
    project["objects"] = copy.deepcopy(scene["data"]["objects"])
temporary = path.with_suffix(".psx.tmp")
temporary.write_text(json.dumps(project, indent=2) + "\n")
temporary.replace(path)
```

Export afterward: parsing JSON alone does not validate references or asset limits.

### Asset and project limits

Keep models low poly according to visual needs and measured performance. Current
technical ceilings are 16 Scenes, 512 objects per Scene, 4096 resident triangles
and 64 logic modules across the project. Each Scene also has a 4096-triangle
instance limit; shared immutable geometry does not remove that rendering cost.

OBJ files are limited to 4 MB, 1024 positions and 2048 triangles per model.
Faces must be planar and convex with 3–16 corners; indices refer to previously
declared data. Textured models require complete UVs in `[0,1]`. Materials and
normals are ignored; each object references one texture. See
[textures.md](textures.md) for image and VRAM limits. Exported positions and
colliders use integer world units; ensure rounding leaves positive bounds and
scales. These limits are validation ceilings, not recommended asset complexity.

## Working with examples

Examples are optional game projects, not engine fixtures or generic AI frameworks.
After installing the archive, read the game's adjacent guide and referenced C
modules before editing it. For Shadow Ops these are `examples/shadow-ops.md`,
`mission.c` and `menu.c` in its data folder.

The guide describes Scene/object IDs, controls, camera, save points and combat
assumptions. Changing IDs or adding guards, walls or another level may require
module arrays, mission conditions and tests to change too. Renaming labels alone
does not change those contracts. Keep built-in movement and collision actions
consistent with module ownership to avoid duplicate movement or unlocked portals.

Use [modules.md](modules.md) for console APIs, [audio.md](audio.md) for sound and
[textures.md](textures.md) for UV/conversion rules. Keep game-specific rules in
the release's game guide rather than duplicating them across engine documentation.

The optional `tools/generate_shadow_village.py` regenerates the integrated
mountain village in Scene 6 of the installed Shadow Ops example. It requires
Pillow only for desktop asset generation and replaces generated village scenery
and placements; preserve manual Scene 6 edits before running it. It also places
manual checkpoints beside every campaign exit. The campaign and village checks
share `/export/shadow-ops-example`; no separate prototype project is required.
Menu-style HUD messages have an opaque bordered panel sized to their visible
text, including checkpoint confirmations and errors.

The optional `examples/sanitarium.psx` is an original fixed-camera survival horror
demo with a pre-rendered plate and genuine 3D actors/collisions/projectiles.
This authoring example is separate from the standalone Last Ward C game.
Its pharmacy demonstrates transparent depth layers.
Generic `/export/scene-background`, `/export/depth-layers` and native pipeline fixtures remain independent
of example assets. Check both authoring preview and console framing after changing
a plate or its camera; host exports cannot prove that the painted perspective
matches the walkable geometry.

## Commands and verification

Build and test the desktop editor:

```sh
make
make test
```

Export a project to a fresh standalone C source folder:

```sh
./build/psx-studio --export path/to/game.psx --output path/to/export
```

The exported application uses the engine's C89 API. Neither SDK nor engine is
required for this export. Build through Studio using the included framework and an external SDK:

```sh
./build/psx-studio --build-ps1 --project path/to/game.psx \
  --psyq path/to/psyq
./build/psx-studio --build-ps1 --project path/to/game.psx \
  --psyq path/to/psyq --prepare-only
./build/psx-studio --build-ps1 --verify path/to/output
```

Studio locates the included PSX Lib automatically; there is no Settings field
for it. Old `engine_path` and DOSBox executable overrides are ignored. SDK CLI
overrides do not alter the saved project. DOSBox-X must be in PATH. The CLI
`--engine FILE` remains an explicit development/test override; it is not required
for normal use. Framework changes belong under `psx-lib/`, with its own C-only
build and tests kept independent of Studio.

Generated C is under `build/studio-projects/NAME-HASH/source/`; engine output is
under `build/engine/ps1/NAME-HASH/`. Names include project-path hashes. The source
workspace has its own lock and owner marker; the engine owns its output lock,
SDK inputs, incremental objects and final verification. CLI `--output-root`
continues to produce isolated hashed subfolders. Open `cdrom/game.cue` and keep
it together with `cdrom/GAME.ISO`.

`make test` runs PSX Lib's standalone tests and Studio's editor/export/integration
tests. `make test-lib` / `make test-studio` select either suite. Integration tests
use `psx-lib/` by default; developer overrides `PSX_LIB_ROOT` and `PSX_LIB` can
select a different framework source folder/executable for compatibility checks.
Framework tests never discover the parent editor or sibling games. SDK compilation,
CD verification and emulator performance need separate checks; preparation alone
is not a console build.

On Linux run GUI/DOSBox/emulator checks using the isolated Xvfb environment from
spec.md. Never use the user's emulator profile or primary DOSBox config.

### Known limitations

- Project JSON v1–v3 remains readable. Modules using the retired Studio HUD or
  inventory structs must migrate to engine overlays and application-owned state.
  The installed legacy Shadow Ops archive still needs that migration; its
  gameplay integration test reports a skip while authoring checks remain active.
  Sanitarium also retains legacy HUD/inventory modules that need migration before
  console builds with current PSX Lib; its editor preview/export remain available.
- Resource packaging uses the engine's C asset-list interface with one resource
  bank per Scene. Each bank has its own CD texture directory, preserving SDK
  lookup for projects whose combined texture directory would span sectors.
  Scene IDs are independent of resource-bank IDs. Standalone exports require
  `--resources EXPORT/reslist.h --asset-root EXPORT/resources` when built directly
  with the engine; Studio passes these automatically.
- The viewport does not execute gameplay/animation modules or emulate PS1 GPU
  ordering. Verify game behavior and performance on console/emulator.
- This integration has host C89 and engine preparation coverage. Real SDK and
  emulator verification require DOSBox-X; Windows packaging/execution requires
  a dedicated Windows session.

## Starting another game

Create a new `.psx` with its own adjacent data folder. Keep gameplay, menus,
inventories and animations in project C89 modules using engine public APIs.
Use stable Scene/object IDs, low-poly assets and fixed-step integer arithmetic.
Validate exports after authoring changes and keep a short game-specific guide.
