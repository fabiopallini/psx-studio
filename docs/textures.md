# Native PNG to TIM conversion

PSX Studio converts textures during scene export. `asset_load()` decodes PNG
with libpng; `asset_write_tim()` writes TIM from the same RGBA pixels used by
the preview, without an external converter.

PNG textures must be at most 256×256 pixels. A Scene supports six distinct
textures, of which at most four can be wider than 192 pixels. Reusing the same
texture reference shares a VRAM slot. These are capacity limits, not asset targets.

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
