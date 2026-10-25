# ANSI C89 logic modules

Each object can reference a `.c` file imported into the project data folder.
Include `object.h` and export exactly `const PsxLogic psx_logic`. The exporter
renames this symbol to link multiple modules; objects referencing the same file
share its implementation. Keep internal helpers and variables `static` to avoid
linker conflicts. Files must be ASCII and at most 256 KB. Additional headers
are not imported automatically.

```c
#include "object.h"

static void init(PsxObject *object, PsxScene *scene)
{
    (void)scene;
    object->rotation[1] = 0;
}

static void update(PsxObject *object, PsxScene *scene, unsigned long input)
{
    (void)scene;
    (void)input;
    object->rotation[1] = (object->rotation[1] + 16) & 4095;
}

static void collision(PsxObject *object, PsxObject *other, PsxScene *scene)
{
    (void)object;
    (void)scene;
    psx_object_kill(other);
}

const PsxLogic psx_logic = { init, update, collision };
```

Use `0` for an unused callback. Follow C89: declarations at the beginning of
blocks, `/* ... */` comments and no C99 constructs. This collision example kills
any object it meets; assign it only when that behavior is intended.

## Update cycle and input

The generated PAL game runs simulation at **50 fixed steps per second** using
an integer video clock. Each `update` is one 20 ms step. Keep movement increments
and timers per update: `position[0] += 2` means 100 world units per second, and
modules do not need a `deltaTime`.

`psUpdate()` owns timing and reads fresh controller input for each due step.
The main loop keeps calling it while GPU work/presentation is pending. The
renderer also yields between objects and small vertex/triangle batches, so a
longer render need not postpone all logic until the next image. Game callbacks
run in the main thread; the VBlank callback only displays a completed image.

Rendering uses an object/camera/HUD snapshot to avoid mixing positions from
successive simulation steps in one image. `psBeginFrame()` immediately returns
zero if no packet buffer is available. `psDisplay()` queues the completed list
without waiting. CPU preparation of one list can overlap GPU drawing of the
other. FPS counts images actually presented, independently of simulation calls.

Catch-up is bounded to five steps (100 ms) per service call after exceptional
stalls. The system is cooperative: one expensive callback/operation can still
exceed the CPU budget; this is not hard real-time preemption. Scene changes are
applied only after `psGpuIdle()`; queued/unfinished old frames are discarded,
blocking asset loading resets the clock, and held input must be released before
controlling the new Scene.

`psx_scene_init(scene)` calls `init` on living objects once at load time.
`psx_scene_update(scene, input)` applies movement configured in the editor,
calls `update` on surviving objects, then checks overlaps. `input` is the mask
of pressed buttons, also passed to `update`:

| Direction | Mask |
| --- | --- |
| Up | `4096UL` |
| Right | `8192UL` |
| Down | `16384UL` |
| Left | `32768UL` |
| Start | `2048UL` |
| Cross | `64UL` |
| Square | `128UL` |

`input_mode` is 0 for no movement, 1 for X/Z and 2 for X/Y. `speed` ranges
from 1 to 32 units per update. Diagonal movement affects both axes without
normalization. Avoid adding movement in a module if the editor already handles
it, unless this is intentional. A game's custom module may normalize its own
movement (Shadow Ops does); that does not change built-in movement behavior.

## Objects, scenes and collisions

See [runtime/object.h](../runtime/object.h) for the full interface. `PsxScene`
contains the object array and count, camera and `projection` mode (0 orthographic,
1 perspective). Object IDs retain their project values. `position` and
`half_size` use integer world units; `rotation` uses 4096 units per turn and
`scale` uses 4096 for 1.0. `user_data` holds custom state and is not serialized.
The module owns its memory and lifetime.

| Function | Effect |
| --- | --- |
| `psx_object_move(object, scene, dx, dy, dz)` | Moves in unit steps with collision checks. Returns 1 at the destination, 0 if blocked, dead or outside bounds. Partial movement may already have occurred. |
| `psx_objects_overlap(a, b)` | Tests AABB overlap, excluding dead objects, self-comparison and edge-only contact. |
| `psx_object_kill(object)` | Sets `alive = 0` without removing the array element. |

Use `psx_object_move` to respect solids, triggers and the ±32767 world bounds.
Two solids block movement; contact involving a non-solid volume acts as a
trigger. Blocked attempts also invoke callbacks. Callbacks can repeat during
movement steps and across updates; they do not represent only volume entry.
Configured actions run after callbacks: `collision_action` 0 = none, 1 = kill
self, 2 = kill other. Death in the first callback can prevent the second callback.

Do not change the scene array or count during callbacks. Dead objects remain
in the array but are neither updated nor drawn. Assigning `position` directly
bypasses movement checks and can pass through obstacles.

## Camera modules

A `PSX_OBJECT_CAMERA` supports the same init/update callbacks and D-pad movement
as other objects, but never collides or renders. Its position is the eye location;
rotation X is pitch and Y is yaw, in 4096 units per turn. Roll is unsupported.
`camera_zoom` uses 4096 for 1.0; `camera_projection` is 0 for orthographic and 1
for perspective. The editor zoom range 0.15–6 corresponds to integer values
614–24576 in modules; keep pitch within ±1024.

`scene->active_camera` selects a camera by object ID. After all update and
collision callbacks, a living active camera is copied into the renderer state.
The same synchronization occurs after init callbacks. With no active object on
load, the legacy scene camera is used. Losing the active camera at runtime holds
the last view. Use `follow_target` and `follow_offset` for ordinary following;
custom camera modules can instead compute their own positions.

## Scene transitions

Every Scene has a stable positive ID, shown in the editor's Scene selector.
`scene->scene_id` identifies the current Scene. To change it from init, update
or a collision callback:

```c
if (input & 2048UL) { /* Start, using Psy-Q button masks. */
    psx_scene_request_change(scene, 2);
}
```

The request returns 1 if queued, or 0 for an unknown/zero ID or an already queued
request. The first accepted request wins. Requesting the current ID restarts
that Scene. Use this API rather than calling generated loaders from callbacks.
The main loop applies the request after the update returns. Remaining old-Scene
updates/collision callbacks stop once a request is pending. Held controller input
must be released after a transition before gameplay input resumes.

Object arrays are reused. On re-entry, positions, alive flags, behavior settings,
camera and object user data reset to exported defaults; textures and projection
caches are reloaded for the destination. Do not retain object pointers across a
transition. IDs are local to a Scene; `psx_scene_find(scene, id)` returns a living
object in the current Scene, or 0.

To release resources owned by a module, assign `object->cleanup` during init:

```c
#include <stdlib.h>
static void cleanup(PsxObject *object, PsxScene *scene)
{
    (void)scene;
    free(object->user_data);
    object->user_data = 0;
}
```

Cleanup runs for each object with a registered callback, including dead objects,
before the Scene is replaced. Register it with `object->cleanup = cleanup` in
`init`; keep the existing three-field `PsxLogic` initializer. Do not change the
object array/count during cleanup. The engine clears `user_data` but cannot free
arbitrary pointers automatically. Module static/global variables remain resident;
reset them explicitly in init if they represent per-Scene state.

`collision_action = 3` requests `object->target_scene`. The editor exposes this
as **Collision: Change Scene** plus a destination selector. Existing actions
0, 1 and 2 retain their meanings. `collider_offset[3]` shifts the AABB centre in
world axes without moving the mesh; it is added to `position` during overlap and
movement checks. `show_colliders` controls debug drawing without changing physics.

A camera's `follow_target` is a local visual-object ID; `follow_offset[3]` is a
world-space offset. After all callbacks, the active camera follows the target's
updated position. Coordinates are clamped to the supported world range. A dead
or missing runtime target leaves the camera at its last position.

PS1 modules must use integer or fixed-point arithmetic. The console has no FPU;
do not use float/double or floating-point math calls in gameplay callbacks.
See `AGENTS.md` for the project arithmetic rules.

## Optional gameplay HUD

A module can enable `scene->hud.enabled` and set `health`, `max_health`, `ammo`
and `message` in `scene->hud`. The standard game loop then draws an HP bar across
the top overlay, ammunition, FPS and the message instead of the diagnostic scene
summary. The bar clamps health and supports maximum health up to 32767. Keep
message strings short (roughly 36 characters) and alive until replaced; static
string literals are suitable. Zero-initialized/disabled HUDs preserve the existing
overlay for older examples. Scene loads reset HUD state, so initialize it in your
module's init callback. The editor does not execute gameplay modules or draw this
runtime HUD in its preview. The optional Shadow Ops example demonstrates
shooting, enemies, supplies, health, death/restart and extraction.

### Flat ground ordering

`object->render_flags = PSX_RENDER_BACKGROUND` explicitly places a flat ground or backdrop mesh
behind normal depth-sorted geometry. The default is zero. Shadow Ops sets this
for object 1 in its mission initializer, on both Scene loads. Use it only when
all gameplay geometry belongs in front of that surface; elevated platforms,
bridges and walls must retain normal depth sorting. This avoids average-depth
ordering errors from large floor triangles without subdividing the entire map.

`PSX_RENDER_CULL_BACKFACES` omits zero/negative screen-space winding triangles.
Enable it only for meshes with consistent winding (as on Shadow Ops walls).
Sprites and other meshes stay two-sided by default. Near-plane crossings and
very wide projected edges conservatively bypass this rejection.

## HUD styles and sector checkpoints

`scene->hud.style` defaults to 0 (existing diagnostic/game HUD).
`PSX_HUD_HEALTH_ONLY` shows only the HP bar. `PSX_HUD_COMBAT` shows compact HP,
a pistol icon with a cyan frame and tiny ammunition digits.
`PSX_HUD_MENU` hides the bar and
prints `hud.message` as a multiline menu/prompt. Keep menu strings static so
render snapshots can safely reference them. Modules own menu input and pausing.
Health-only and combat styles suppress diagnostic text and the FPS overlay;
style 0 retains the standard text/status display.

The initial checkpoint backend is for Shadow Ops sector starts: set
`scene->checkpoint.operation` to `PSX_CHECKPOINT_SAVE` with `scene_id` 1 or 2,
or `PSX_CHECKPOINT_LOAD`. The console handles the request at GPU idle in the
main thread, clears operation and returns status 1/-1; a successful load supplies
the stored Scene ID. The module validates it and requests a normal Scene change.
BIOS card I/O is a deliberate pause; the engine resets timing and waits for input
release afterward. Do not issue requests while gameplay should continue.
This is not a generic object/inventory serializer. Host tests simulate completions.
The card backend uses one block in slot 1, a standard header/icon and a versioned,
CRC-checked payload; it never formats a card or deletes unrelated saves.

`hud.loading` optionally supplies a static string for a pending Scene transition.
The main loop calls `psShowLoading` before destination init/resource loading. This
function publishes a completed loading image and may wait for GPU/VBlank only at
that explicit I/O boundary. It is not for ordinary callbacks or frame rendering.
Card I/O similarly publishes a saving/reading screen first and is deferred until
no CPU frame is being built, avoiding reuse of packets/framebuffers under the GPU.

## Sample audio

Declare imported VAG dependencies with `/* @sound filename.vag */` (flat DOS 8.3
filename in the project data folder). In update logic, call
`psx_sound_play(scene, "filename.vag", 10000, 0)` after the gameplay event succeeds.
Volume ranges from 0 to 16383; pan is Q12, -4096 left to +4096 right. The call
returns 1 when started, 0 when unavailable/muted. It does no I/O or allocation.
Samples are embedded by export and uploaded at Scene load; eight hardware voices
allow overlap. `psx_music_play(scene, "patrol.vag", 4200)` starts a loop-marked
VAG on dedicated voice 23, which effects never replace. Call from update after
the bank has loaded. Scene changes stop music and effects. See [audio](audio.md) for accepted files, budgets and lifecycle.

## Analog direction input

The `input` callback argument retains the existing Psy-Q button masks. The engine
also updates `scene->input`: `analog` indicates a valid analog controller packet,
and `x`/`z` are raw left-stick axes (-128..127, positive X right, positive Z down).
A radial 24-unit dead zone produces zero axes. Digital controllers, disconnected
ports and unsupported devices produce zero analog axes. A physical/emulated
controller must be in analog mode. Game callbacks still run only in the main
thread, at fixed simulation steps; controller communication uses BIOS pad buffers.
Loading boundaries wait for both buttons and stick to return to neutral.

The portable C89 direction helpers use integers only: `psx_direction_angle(x,z)`
returns 4096 units per turn (north 0, west 1024); `psx_direction_vector(angle,v)`
returns Q12 X/Z components. `psx_direction_normalize(x,z,v)` normalizes bounded
world deltas to Q12, returning 0 for a zero vector. Sine uses a compact interpolated
quarter-wave table, and normalization uses an integer square root with rescaling
to keep products within PS1 32-bit long limits. Host reference tests compare these
helpers against floating-point trigonometry; the console uses no floating point.

## Ground overlays and attack feedback

`PSX_RENDER_DECAL` draws an untextured mesh as a translucent green ground overlay
above `PSX_RENDER_BACKGROUND` geometry and below normally sorted actors/walls.
`PSX_RENDER_ALERT` changes that decal to orange. Each translucent primitive sets
its own draw mode immediately before drawing. Ground decals use mode 3
(destination plus one quarter of the source), with low-intensity green/amber
colors so terrain remains visible. The draw-mode command shares the existing
polygon packet storage and does not require another polygon pool.
Shadow Ops uses four immutable triangle slices per guard and immutable character
poses for aiming, running and recoil. `PSX_RENDER_EMISSIVE` boosts
textured effect colours to emphasize white-hot tracer tips and muzzle flashes. Change object transforms or mesh pointers; never edit shared mesh
vertices while the renderer consumes a frozen snapshot. Pose meshes must fit the
original object's allocated vertex capacity.
