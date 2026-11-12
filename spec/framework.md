# PSX Lib framework reference

Read the repository-wide [specification](../spec.md) first. Paths below are
relative to `psx-lib/` unless stated otherwise. The framework also builds and
tests when this folder is copied outside the repository.

## Architecture

`tools/main.c` exposes C builds, resource-bank conversion, WAV import and build
verification. `tools/build.c` prepares application sources and injects `runtime/`
and the Psy-Q backends from `pipeline/template/src/`. Applications own main.c;
there is no default game entry point or generated scene loader.

`tools/core.h` contains resource-only host types. `tools/resources.c` validates
asset roots, model IDs/kinds, relative paths and depth layers. `tools/assets.c`
imports OBJ/PNG and emits TIM pixels. `tools/export.c` directly emits resource
meshes, texture/audio tables and PsxResourceBank values. It never generates
PsxObject defaults, cameras, collisions, game callbacks or scene transitions.
`tools/audio.c` performs native PCM WAV to SPU ADPCM conversion.

Resource arrays deduplicate by exact initializer contents across banks. Geometry
uses bounded C units (96 KiB source target; an individual mesh remains intact),
and each sample uses its own compilation unit to fit Psy-Q assembler limits.
Writable sample-bank metadata and texture indices stay local to each bank.

Runtime C89 objects, cameras, input, collision, frame snapshots and fixed-step
helpers are independent of host asset types. A PsxScene is an application-owned
runtime context; it does not imply an authoring document. Rendered object,
camera, active overlay and rotation-bank state are frozen by value; immutable mesh
pointers are shared. The backend uses two packet buffers to overlap CPU/GPU work.

## Direct C source projects

`--build-ps1 --source-root DIR --psyq SDK` requires a directory containing main.c.
The application owns the entry point, object/camera setup, gameplay, overlays,
loading and scene lifecycle. See `examples/c-starter` for a minimal program.
An empty PsxScene is a valid input/rendering context.

Source files use unique, flat DOS 8.3 .c/.h names. All additional .c units are
compiled automatically; headers are copied and fingerprinted transitively.
Reject case-fold collisions and attempts to replace engine runtime files.
Non-source files/directories are ignored except optional preconverted 16-bit
TIMs. These use DOS-safe uppercase filenames and are packaged under ASSETS;
validate dimensions, page bounds and framebuffer/font reservations. The
application supplies its texture bank to psSetScene at an explicit load boundary.

`--max-objects` (1..512, default 64) and `--render-triangles` (256..8192, default
1024) specialize runtime work/packet buffers. The polygon allocation and emission
guard both use STUDIO_POLYGON_LIMIT; change them together. These capacities
are technical ceilings; verify actual RAM/FPS on console/emulator.

`--name NAME` selects a stable basename (1..32 ASCII letters/digits/_/-).
`--build-directory` selects a generated workspace independently of installed
engine resources; default output is BUILD_DIRECTORY/ps1/NAME. `--output-root`
retains source-path-hashed subfolders. The source root is the ownership identity;
source paths cannot be inside their managed output. The historical filename
`.psx-studio-build` remains only an ownership receipt for existing C build caches.
It does not enable any document parsing.

`--prepare-only` snapshots/prepares without DOSBox, invalidating final artifacts.
`--verify PATH` verifies an existing build without source/SDK arguments.
Builds without --source-root are rejected; there is no implicit demo/probe game.
Removed document options --project/--export/--validate/--exporter are errors.

`--define NAME=unsigned_decimal_integer` writes guarded psxdefs.h before
normalization and hashing. Identifiers use at most 48 ASCII characters; values
have at most nine decimal digits. Values render canonically. Omitting the option
resets the generated header. Only including units are invalidated.

### SDK object cache

`tools/build.c` regenerates source snapshots but retains `obj/` outside `src/`.
A host-generated make recipe runs `ccpsx -c` only for dirty units, followed by an
object-only link using `objects.rsp`. Every invocation still relinks, packages and
validates the CD. File names obey DOS 8.3 and command tails remain bounded.

Fingerprints include compile flags, canonical SDK path, contents under SDK
`psyq/`, `include/` and `lib/`, and transitive generated C/header includes. Managed
source names are resolved case-insensitively like DOS. Macro/external includes,
time macros and SDK symlinks conservatively prevent reuse when their inputs
cannot be tracked. Object digests prevent reuse of missing or damaged outputs.

`obj/pending.json` describes this invocation; `obj/cache.json` contains receipts
from a verified build. Preparation does not publish receipts. Dirty object files
are removed before compilation, and receipts are committed only after SDK and
CD validation, with inputs checked again. Existing output ownership, symlink
checks, build locks and invalidation of old success artifacts remain mandatory.
Do not replace the cache with timestamp-only dependency rules: native export
rewrites files, and DOS timestamps have insufficient resolution.


### Native resource conversion for C applications

C applications may optionally add --resources FILE --asset-root DIR. These two
options require --source-root when building. The standalone frontend also
accepts --resources FILE --asset-root DIR --output DIR without any SDK.
--name NAME selects a stable C application's output basename (1..32 ASCII
letters/digits/underscore/hyphen). Output ownership is still tied to source root;
different source roots require separate owned outputs.

The conversion input is a restricted, ASCII X-macro list, one invocation per
line; no preprocessor, expressions or arbitrary code are evaluated:

```c
/* A resource bank chosen by the application, not a generated game scene. */
PSX_BANK(7, "bedroom.png")
PSX_MODEL(2, mesh, "actor.obj", "skin.png")
PSX_MODEL(31, sprite, "", "key.png")
PSX_SOUND("step.vag")
PSX_LAYER("foreground.png", 550)
PSX_END()
```

Banks have unique IDs 1..65535, at most 24. Each bank has up to 512 model IDs
(positive and unique in that bank), up to four layers, one optional 320x256
background (empty path means none), and named VAG samples. Model kinds are mesh,
sprite or billboard; OBJ requires mesh. Quoted relative asset paths allow ASCII
letters, digits, underscore, period, slash and hyphen, with no parent traversal.
Numeric arguments are decimal literals. Local quoted .h includes may share
resource groups across banks; includes use the bounded source bundler (ASCII,
16 levels and 1 MiB aggregate, no parent traversal/symbolic links). Shared X-macro
headers intentionally have no include guards and are expanded at every use.
Empty lines and whole-line // or /* */ comments are accepted. Bank/end nesting and all arguments are validated.

Generated assets.h exposes psx_resources(id), returning a PsxResourceBank.
psx_resource_mesh(bank,model_id) looks up immutable geometry.
psx_resource_apply(scene,bank) binds audio, background and depth-layer metadata;
the application owns objects, transforms, cameras, logic and transitions, and
passes bank->textures/texture_count to psSetScene at its load boundary.
There is no generated main, scene construction, module discovery or gameplay.
C/header and TIM files emitted into the build workspace use DOS-safe names.
They must not collide with application source names. Standalone conversion
requires a fresh destination (the application can recreate its export folder).

Conversion uses native OBJ/UV import, PNG/TIM packing and VAG validation. It checks 4096 model triangles per bank, 4096 unique
resident triangles across banks, object-texture/page and SPU sample budgets.
Geometry and samples deduplicate by exact initializer contents across banks;
texture indices/VRAM placement and writable sample-bank addresses remain local.
Shared arrays use separate compilation units for legacy assembler limits.
CD textures live under ASSETS/B<bank-id> and are loaded only at explicit entry.
assets.json is generated internal packaging metadata, not an authoring document.
The application validates its own C transforms/colliders and scaled vertex range.


## C runtime APIs

Include `object.h` for PsxScene, PsxObject, PsxMesh, PsxTexture and resource APIs;
include `psx.h` for native platform services. Names and storage are owned by the
application. There is no required psx_logic symbol, module renaming or automatic
scene construction. Optional PsxLogic callbacks use { init, update, collision };
initialize unused callbacks to 0 and attach the table to objects in application C.

### Simulation and presentation

Call psInit, initialize application state and psx_scene_init, then psSetScene
at the initial load boundary. The main loop repeatedly services psUpdate,
checks psBeginFrame and draws with psDrawScene/psDrawOverlay before psDisplay.
The example demonstrates this loop. psUpdate services integer clock/input and
the renderer yields cooperatively during large batches. One callback can still
stall scheduling; no interrupt executes game code.

Each update is a 20 ms simulation step. psBeginFrame returns zero when no packet
buffer is available; psDisplay queues without blocking. VBlank presents complete
frames. FPS counts actual presentations rather than simulation calls. Catch-up
is bounded to five steps per service call after exceptional stalls.

scene->update_game is an optional fixed-step callback, including empty scenes.
scene->prepare_overlay builds presentation before a frame snapshot and must not
advance simulation, perform I/O or call psUpdate. Gameplay/menu/inventory/health
state belongs to the consuming application.

### Objects, cameras and collisions

Position and half_size use integer world units; rotation uses 4096/turn and scale
uses Q12. IDs and user_data are application-owned. The application owns memory
and must not mutate the object array/count during callbacks or in-flight frames.

psx_object_move performs bounded unit-step collision-aware movement and returns
1 at the destination, 0 if blocked/dead/outside bounds; partial motion can occur.
psx_objects_overlap uses AABBs, excludes dead/self/edge-only contact. Two solids
block movement; non-solid contacts act as triggers. Callbacks can repeat.
collision_action values are 0 none, 1 kill self, 2 kill other, 3 request target_scene.
Assigning position directly bypasses movement checks. collider_offset shifts the
AABB centre independently of the mesh. Dead objects remain allocated.

input_mode is 0 none, 1 X/Z or 2 X/Y; speed is units per step. Built-in digital
movement applies speed on each axis. Applications can normalize via the integer
psx_direction_* helpers. Button masks retain Psy-Q layout: Up 4096, Right 8192,
Down 16384, Left 32768, Start 2048, Cross 64, Square 128.
scene->input holds analog x/z (-128..127) after radial dead zone 24; unsupported
or digital/disconnected controllers produce zero axes. Callbacks run at fixed steps.

PSX_OBJECT_CAMERA never renders/collides. Position is eye location, rotation X/Y
is pitch/yaw, roll unsupported. camera_zoom is Q12 and camera_projection is
0 orthographic/1 perspective. active_camera selects by object ID; follow_target
and follow_offset are world-space following controls. Sync occurs after callbacks.
Applications may also supply Scene camera values directly.

### Scene lifecycle

The application supplies scene_exists and handles pending_scene requests from
psx_scene_request_change. First valid request wins; further old-scene updates
stop. At GPU idle, the application clears old objects with psx_scene_clear,
restores/constructs destination C state, calls psx_scene_init, binds resource banks
and calls psSetScene. No engine-generated loader or scene-ID registry exists.
psx_scene_find returns a living local object by ID; do not retain object pointers
across application-owned scene replacement. psx_scene_clear calls registered
cleanup even on dead objects; it cannot free arbitrary user_data automatically.
Application static/global state remains resident and must be reset intentionally.

### Overlays, checkpoints and audio

psx_overlay_clear clears commands/text; psx_overlay_rect and psx_overlay_sprite
emit bounded 2D painter-order primitives. Sprite texture indices are 0-based.
psx_overlay_text copies up to 511 bytes. Capacity is 384 commands/16 images;
texture UV rectangles must fit one 256x256 page and the actual loaded image.
Overlay data freezes inline with each frame snapshot.

PsxCheckpoint uses application-owned file/title and checkpoint IDs. Request
PSX_CHECKPOINT_SAVE/LOAD through operation and check status 1/-1. The application
maps loaded IDs to its own state. Card I/O occurs at GPU idle and resets timing;
it is not a general object serializer. scene->loading supplies a static caption
for application-controlled loading boundaries. Do not perform loading in draws.

psx_sound_play(scene,name,volume,pan) starts an already uploaded sample, volume
0..16383 and Q12 pan -4096..4096. Eight round-robin effect voices permit overlap;
psx_music_play uses reserved voice 23 for validated full-sample looping VAG data.
Start audio after psSetScene uploads its bank, not from object init beforehand.
Per-frame playback performs no allocation, CD read or DMA wait. Stop/replace
voices and upload new banks only at explicit load boundaries.

### Rendering flags and animation

PSX_RENDER_HIDDEN suppresses drawing while retaining simulation/collision.
PSX_RENDER_NO_FALLBACK suppresses the diagnostic/default box for a meshless
simulation root. PSX_RENDER_BACKGROUND explicitly orders flat backdrop geometry
behind actors; ordinary geometry uses depth sorting. CULL_BACKFACES requires
consistent winding. DECAL/ALERT support translucent green/orange ground feedback;
EMISSIVE keeps textured effects bright. These do not provide a general Z buffer.

Use immutable mesh swaps and object transforms for animation. Every replacement
pose must fit the reserved vertex capacity established at loading. Changes to
mesh vertices in flight invalidate snapshot/cache assumptions and are forbidden.

### Visible meshes, billboards and GTE projection

The native projection cache records whether all shared positions lie on screen
and in front of the near plane. Such meshes skip repeated per-triangle clipping
classification and GPU span tests; ordinary backface/depth/UV handling remains.
Any object/camera/matrix change recomputes this classification. Crossing the near
plane or a screen edge retains the existing bounded clipping path.

Camera-facing billboards also use a direct GTE matrix: X/Y scale is applied
in view space, with constant centre depth. This avoids rotating each corner out
of and back into camera space. Oversized coefficients keep the legacy path.
The hardware projector batches three shared positions when possible; overflow
or near-plane cases retain the individual vertex fallback. Render polling
uses a shared sixteen-unit vertex/triangle work budget across rigid parts,
plus periodic object-list servicing. Three-vertex batches can exceed that budget
by at most two units. This avoids redundant polls at every tiny part boundary
without coupling fixed simulation steps to drawn frames.

Registered matrix meshes use GTE perspective projection when Q12 zoom is in
(0,8192]. H is rounded to an integer focal length (`zoom * 600 / 4096`), so native
pixel rounding can differ slightly from the CPU reference. Raw SZ3 is retained
for depth ordering; overflow, near-plane, wide-coordinate and orthographic cases
retain the CPU path. Simulation yields restore GTE transform state.

Native direct meshes cache signed-short local GTE vertices and packed UV/page
words in the existing twelve-byte-per-corner workspace, without enlarging the
vertex pool. Mesh or projection-path changes invalidate local packing; texture
page/atlas changes invalidate UV packing. Fully visible textured triangles emit
the standard eight-word FT3 packet directly; clipped triangles retain the
general path. The SDK backend emits explicit RTPS/RTPT command words because
older INLINE_C.H variants contain incompatible pseudo-opcodes. Empty font
queues skip FntFlush; queued text and loading boundaries retain flushing.

Frame snapshots copy the scene prefix and active overlay commands/text,
backdrops and rotation slots, plus the complete allocated object count. Unused
capacity is not presentation state and is not copied. Shrinking/growing counts
and source mutations are covered by the standalone snapshot tests.

### Small mesh transformation precision

Mesh offsets retain eight fractional bits through scaling and XYZ rotations,
then round symmetrically once to world integer coordinates. This prevents small
scaled, articulated parts from losing a unit at each intermediate operation.
Billboard transforms retain their existing path. Inputs with any local coordinate
outside +/-32767 or scale outside +/-65536 use the legacy path. With bounded
inputs the initial Q8 magnitude is at most 134213632; even three conservative
rotation doublings stay within signed 32-bit long. Q12 products split quotient
and remainder to avoid overflowing the intermediate multiplication.


### Direct object rotation matrices and GTE path

psx_object_rotation(scene,object) allocates/reuses a scene-local Q12 rotation
slot and returns a PsxObjectRotation pointer; m[9] holds its signed-short
coefficients in row-major order. Up to 64 slots
live inline in PsxScene, initialized to identity and cleared at scene teardown.
render_flags bits 8 and above carry the one-based slot; low eight bits remain
ordinary flags. Preserve upper bits when updating low flags on registered parts.
No PsxObject field/layout is changed. Optional matrix rotations override Euler
for mesh objects. Objects without a registered slot retain the old Euler path.

The frame snapshot copies the active inline rotation slots by value; render lookups use
the frozen scene, not live simulation pointers. Projection caches store/compare
matrix values as well as object/camera state, so changing a rotation invalidates
geometry while a translation-only update does not. Matrix binding/removal also
invalidates the cached shape. psx_object_rotation_find is an O(1) slot lookup.

The native backend folds camera rotation, object rotation and scale into one
GTE matrix per object, with full-width camera-space translation. Raw local mesh
vertices go straight through RotTrans, skipping CPU per-vertex object rotation.
Coefficients outside signed-short range or wide/overflowing input take a bounded
integer fallback. Projection/clipping/UV logic is unchanged; simulation yields
restore the composed GTE matrix. This path uses no floating-point arithmetic.


## Native asset formats

OBJ inputs are at most 4 MB, 1024 positions and 2048 triangles per model. Faces
are convex/planar with 3..16 previously declared corners. Textured models require
complete UVs in [0,1]. Materials/normals are ignored. Asset arithmetic on the host
may use floating point; generated vertex coordinates are integer C89 values.
UV endpoints quantize to round(coordinate * (dimension - 1)), preserving OBJ V flip.

PNG object textures are at most 256x256. A bank supports six distinct model
textures; plates/layers share remaining VRAM through page-safe packing. Without
plates the six slots start at X 320/576/832, Y 0/256; only four slots accept
widths above 192. All layouts protect framebuffers, font memory and UV wrapping.

16-bit TIM is little-endian identifier 0x10, mode 2, no CLUT, with pixel-block
size 12 + width*height*2. Channels truncate to RGB555. Alpha below 128 becomes
0x0000; opaque quantized black becomes 0x8000. Dithering/palettes/intermediate
alpha are not generated. Prepared headers, VRAM rectangles, lengths and packaged
bytes are validated natively.

PSX_BANK may reference a PNG background sampled to 320x256 PAL over black and
split into 256x256 and 64x256 TIMs at (320,0)/(576,0). PSX_LAYER declares up to
four ordered transparent full-canvas layers with camera depth 16..32767. Layers
crop/split to visible bounds and share ordinary depth sorting; equal-depth order
follows declaration order. Fully transparent layers are rejected. Source plates
are at most 4 MB/2048x2048. Preserve canvas alignment when authoring.
Bank metadata stores base/background indices and cropped backdrop rectangles;
psx_resource_apply binds these, sounds and counts without constructing objects.

Native WAV import accepts little-endian RIFF/WAVE PCM, mono/stereo, 8/16-bit,
1000..44100 Hz up to 64 MB. Stereo averages to mono; rate is retained. Output is
VAGp 0x20 with a 48-byte big-endian header and SPU ADPCM (28 samples/16-byte block).
The encoder searches five predictors/shifts 0..12, prepends a silent interpolation
block and appends a one-shot terminator. It does not resample or normalize.
Content-hashed DOS 8.3 VAG names avoid overwriting different content.

PSX_SOUND names a flat DOS 8.3 VAG in the asset root. Conversion validates
one-shots or full-sample loops, embeds aligned/padded data in its own C unit,
and deduplicates identical arrays across banks. Each bank has separate mutable
SPU addresses. SPU RAM 0..0x100f is reserved; padded sample data must fit the
remaining 520176 bytes with reverb off. Samples also consume main RAM. Loop music
requires first-block flag 4 and final flag 3; arbitrary loops are rejected.

## DOSBox-X build profile

Resolve only dosbox-x from PATH. Use one isolated session with -defaultconf and
generated -conf; no version probe, classic/Staging/Flatpak fallback or personal
configuration. Set memsize=64, core=dynamic, cputype=386, cycles=fixed 200000,
private area size=131072 in [dosbox] and ems=false in [dos]; keep XMS enabled.
The 128 KiB DOS table arena and disabled EMS avoid compiler child-process/table
exhaustion and upper-memory overlap. Preserve incremental hashing and final
artifact verification when changing this profile.

### Failure diagnostics

SDK compiler output is stored separately in src/BUILD.LOG from the DOSBox host
console in dosbox.log. If DOSBox exits unsuccessfully or times out, print both
available logs before the final error so callers can expose compiler failures.
Each failure excerpt is bounded to its last 128 KiB. Decode compiler output as
CP437, sanitize host output to valid UTF-8 and normalize line endings. Discard
previous dosbox.log during preparation so launch failures cannot show stale
messages. Diagnostics must not replace the original error/exit status.

## Validation

`make test` covers C89 runtime, overlays, object/rotation caches, snapshots,
fixed math, collision/input, audio, resource-bank conversion and native build/CD
verification. Resource tests compile generated C89 and execute bank lookup/binding
with synthetic OBJ/PNG/VAG fixtures. Pipeline tests reject document options and
exercise C unit/header/SDK/flag cache invalidation, ownership, locking, stale
artifact removal, resource conversion, native TIM packaging and ISO verification.
They do not compile SDK code or establish emulator/hardware performance.

External editor integration tests belong to the editor. No game/example archive
is required by engine tests. Windows execution and real SDK/emulator checks must
be reported separately from Linux host tests.
