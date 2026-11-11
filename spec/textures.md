# Native PNG to TIM conversion

PSX Studio converts textures during scene export. `asset_load()` decodes PNG
with libpng; `asset_write_tim()` writes TIM from the same RGBA pixels used by
the preview, without an external converter.

PNG textures must be at most 256×256 pixels. A Scene supports six distinct object
textures. Reusing the same reference shares a VRAM allocation. Background and
depth-layer TIM tiles are additional, subject to available VRAM. Scenes without
plates retain their legacy six-page layout. These are capacity limits, not asset
targets.

Each distinct texture produces `Tnnn.TIM` in the export directory. The pipeline
validates its header, length and VRAM rectangle, copies it into `data/`, then
includes it in the ISO9660 `ASSETS` directory. `texture.log` records names,
dimensions and positions. `--prepare-only` also prepares and verifies textures.

## Format and transparency

Files use little-endian integers, identifier `0x10`, mode 2 (16-bit), no CLUT and
a pixel block of `12 + width*height*2` bytes. The block contains VRAM X/Y,
width in halfwords and height, followed by pixels in top-to-bottom row order.
At 16-bit depth, halfword width equals pixel width.

Each 8-bit RGB channel is reduced to 5 bits by discarding its lowest three bits:
red occupies bits 0–4, green 5–9 and blue 10–14. Alpha below 128 becomes transparent
`0x0000`; alpha 128–255 becomes opaque, matching the viewport threshold.
Quantized opaque black is written as `0x8000` to distinguish it from transparent
texels. This matches the opaque textured commands used by the renderer.

Dithering, 4/8-bit palettes and intermediate alpha levels are not generated.
The PS1 STP bit is not a conventional alpha channel; future semitransparency
support also requires renderer options. Reference:
[PSX-SPX texture and opaque black documentation](https://psx-spx.consoledev.net/dev/ps1/gpu/video-memory-vram/).

## Project compatibility

Source PNG files, VRAM limits and CD texture paths are unchanged. Obsolete
configuration fields are ignored on load and omitted when saving. Previous
external conversion tools have been removed.

## Preview and console UVs

Both exporter and editor quantize each UV endpoint to
`round(coordinate * (dimension - 1))`. The desktop sampler uses that texel's
centre, preserving the shared OBJ V flip and nearest affine interpolation.
Preview colours are reduced to RGB555, matching native TIM conversion. Selection
uses the existing outline without tinting the texture. The OpenGL preview still
uses desktop geometry/depth precision: it is not a pixel-exact PS1 GPU emulator,
and does not execute game modules, animation or runtime render flags.

## Pre-rendered Scene backgrounds

Use **Background...** in the **Background** tab on the right to import a PNG
without selecting an object. The project must be saved first. **Remove** clears only that Scene's
reference; the data file remains available to other Scenes and undo history.
Backgrounds participate in undo/redo, Scene switching, Save As and build snapshots.

The optional Scene JSON field `background` is a path relative to the project data
folder; empty or absent means no background. The editor accepts PNGs up to 4 MB
and 2048×2048. Native export samples them to the **320×256 PAL framebuffer** using
nearest-neighbour sampling, compositing alpha over black. Author the composition
for that frame; imported images fill it rather than adding letterbox bars.
Ordinary object textures retain their 256×256 limit.

Export splits the base plate into opaque RGB555 tiles: 256×256 at VRAM
`(320,0)` and 64×256 at `(576,0)`. Object textures and depth-layer tiles share
remaining page rectangles through native packing, including unused space beside
the 64-pixel tile. The packer prevents page/UV wrapping, framebuffer overlap and
font overlap at the bottom-right of VRAM. Capacity depends on actual dimensions;
there is no longer a four-object-texture limit for background Scenes.
Textures are uploaded once at Scene load, without per-frame image transfers.

## Transparent depth layers

Choose **Add layer...** in the **Background** tab on the right to import a
full-frame transparent PNG, then select it in the layer list and set **Layer depth**. A Scene supports
up to four ordered layers in addition to its base background. **Remove** detaches
the selected layer; **Up/Down** changes compositing order for equal depths. Later
list entries draw on top at equal depth. Layers participate in undo/redo, Scene
switching, Save As and native build snapshots.

```json
"background_layers": [
  { "path": "counter.png", "depth": 550 },
  { "path": "near-pillar.png", "depth": 420 }
]
```

This optional array defaults to empty in older projects. Paths are relative to
the data folder. Layer PNGs have the same 4 MB / 2048×2048 source limits as the
base, and are sampled to 320×256 with nearest-neighbour sampling. Preserve a
full-frame transparent canvas so the artwork stays aligned with the base plate.
Alpha below 128 is transparent; higher alpha is opaque. Intermediate alpha does
not create glass blending. Fully transparent layers are rejected.

Depth is **positive camera-space distance in world units**, from 16 to 32767;
it is not world Z and is not multiplied by camera zoom. Smaller values draw
closer to the camera. At depth 550, geometry with larger camera distance renders
behind opaque pixels of that layer, while closer geometry renders in front.
The desktop preview uses its depth buffer; the PS1 inserts sprites into the same
ordering table as triangles. Like other PS1 geometry this is approximate,
average-triangle-depth ordering, with finite near/far buckets, not a per-pixel
Z-buffer. Split complex surfaces into separate layers when one depth cannot
represent their silhouette well.

Export crops each layer to its opaque bounding rectangle and splits widths above
256 into two TIMs. Transparent holes are retained. Tight silhouettes therefore
save VRAM without changing screen position. Insufficient VRAM produces an export
error; reduce dimensions or combine ordinary object atlases if necessary.

The editor fits the native game frame into the viewport and scales the 3D preview
with it. **View through camera** aligns authoring to the chosen camera. Keep a
fixed camera so painted perspective and layer depth remain consistent. Selection
outlines stay visible for editing even behind layers. Depth layers do not infer
collisions: author matching solid volumes separately. The Sanitarium pharmacy
uses `counter.png` at depth 550 and a separate counter collision volume.
