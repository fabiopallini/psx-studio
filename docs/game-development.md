# Game development playbook

Read this when continuing `examples/shadow-ops.psx` or creating another game.
This is an operational guide, not a second engine specification. Read
[AGENTS.md](../AGENTS.md) first; inspect the current implementation when a detail
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
| Shared simulation and console APIs | `runtime/object.h`, `runtime/object.c` |
| Console drawing/timing | `pipeline/template/src/psx.c`, `pipeline/project-main.c`, runtime headers |
| Build orchestration | `editor/build.c` |

Never implement a permanent feature by editing generated files under `build/`.
The next export/build replaces them. Editor preview does not execute C modules,
AI or HUD logic, and OpenGL does not reproduce PS1 ordering/GPU behavior exactly.

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

## Commands and verification

Run from `psx-studio/`. No installation is needed if dependencies are present.
On Windows, follow the [Windows setup](../README.md#windows-11), then use
`.\build.ps1` for the editor. To build a game:

```powershell
.\build.ps1 -Project examples/shadow-ops.psx -Psyq 'C:\SDK\psyq'
```

Windows editor objects use `build/windows/`
to avoid reusing Linux objects. Windows builds also prepare the portable
`build/windows/psx-studio/` folder. Editor packaging and PS1 builds do not
require Python; Psy-Q and DOSBox remain external tools.

```sh
make
make test
# Focused existing test: exports the current Shadow Ops and runs its C89 harness.
./build/tests -p /export/shadow-ops-example
# Validate/export without GUI, SDK, emulator or a CD build.
./build/psx-studio --export examples/shadow-ops.psx --output build/shadow-export
```

`tests/shadow_game.c` covers movement/aiming, cover, bullets, damage, pickups,
guard kills, gates, extraction/restart and equivalent gameplay at different render
cadences. Adapt scenario assertions when intentionally changing layouts/rules;
do not delete coverage merely to make a redesigned map pass.

Build with the existing SDK; provide its actual path rather than copying a
machine-specific path into source. CLI overrides do not rewrite project settings:

```sh
./build/psx-studio --build-ps1 --project examples/shadow-ops.psx --psyq "$PSX_TASK_SDK"
```

The command above starts DOSBox and needs a display. For automated execution,
run it with the isolated-display rules in AGENTS.md. Output defaults to
`build/ps1/PROJECT/`, using the sanitized `.psx` stem. Editor builds always use
this location and expose no output setting. CLI `--output-root` and legacy custom
`output_path` retain NAME-HASH subfolders; an existing fixed folder owned by a
different project is rejected before its artifacts are invalidated. Find the actual path in the build output
or `build/last-ps1-build.txt`, and check its project marker before using it.
Console source/header filenames must remain DOS 8.3-compatible (for example,
`card.c` and `save.h`); a host build does not catch long-name failures in Psy-Q.
The CD is `cdrom/game.cue` plus `cdrom/GAME.ISO`; keep them together.
`src/BUILD.LOG`, `dosbox.log` and `texture.log` hold build evidence.
`build-info.json` records the SDK used; these ignored files may be absent in a
fresh checkout. An empty `.psx` SDK field does not prevent a CLI override build.

`--prepare-only` still requires an SDK, prepares/snapshots/exports sources and
textures, and invalidates prior final outputs. It is not a read-only check.
Use plain `--export` for validation that leaves the last playable CD alone.

For an automated editor snapshot (Xvfb/xvfb-run must already be available):

```sh
xvfb-run -a -s "-screen 0 1280x1024x24" env -u WAYLAND_DISPLAY -u WAYLAND_SOCKET \
  GDK_BACKEND=x11 SDL_VIDEODRIVER=x11 GSK_RENDERER=gl \
  LIBGL_ALWAYS_SOFTWARE=1 GTK_A11Y=none GSETTINGS_BACKEND=memory \
  DBUS_SESSION_BUS_ADDRESS=unix:path=/tmp/psx-studio-no-session-bus \
  ./build/psx-studio --project examples/shadow-ops.psx \
  --smoke-test build/shadow-preview.png
```

The smoke command saves a static preview and exits; it does not play the game.
In the current GTK build it can emit widget assertions during window teardown
although the PNG is saved and the exit code is zero. Record stderr as well as
exit status; this remains an editor teardown issue to investigate, not evidence
of a clean GUI regression check.

For gameplay checks use `pcsxr -cdfile /actual/output/cdrom/game.cue` on an
isolated display, with the same Wayland/X11 isolation and a disposable emulator
profile (keep the user's BIOS/configuration intact). Exercise movement, aiming,
firing, damage, pickups, camera follow, both directions of Scene travel and
restart. Stop owned GUI/Xvfb processes afterward. Do not open the user's desktop
or file manager for automated checks. For DOSBox Staging also use the temporary
software-texture configuration described in AGENTS.md and disable primary/local
configuration loading; Xvfb alone is insufficient.

FPS measures displayed images, not simulation updates. The shared engine services
50 PAL simulation steps/s cooperatively while rendering from frozen snapshots;
GPU/video polling stays nonblocking. Do not reintroduce a blocking VSync main loop
or solve a graphical slowdown by slowing input/gameplay. Measure FPS during
movement and combat after geometry or logic changes. It counts presented images,
not input updates; historical emulator samples are not a guaranteed minimum.
Keep reproducible evidence and limitations in [verification.md](verification.md).

## Starting another game

Copy an appropriate example `.psx` **and** its data folder under a new matching
stem, change the project name, then update all intended Scene/object/module
contracts. `first-scene` is a smaller base for simple movement and map changes;
Shadow Ops is a combat prototype with game-specific module assumptions, not a
generic AI framework. Remove unused objects/assets only after checking references
in every Scene and C module. Add focused behavior tests for new rules, validate
all Scenes through export, and keep a short game-specific guide like
`examples/shadow-ops.md`. Make reusable engine changes only when the game needs
an actual capability missing from the current runtime/editor.

Shadow Ops movement uses normalized digital/analog directions and Q8 residuals, so
diagonals retain the same speed per simulation step as axial
movement (subject to integer position rounding). Digital diagonal components
use Q12 2896, avoiding a square root in its update loop. Built-in editor movement
currently applies speed independently to each axis; it does not normalize diagonals.
