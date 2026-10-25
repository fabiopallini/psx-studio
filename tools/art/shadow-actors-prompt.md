# Shadow Ops actor texture

Source atlas generated with imagegen. This document preserves the original
prompt, not a guarantee that regenerating it reproduces the released texture.
The original is `shadow-actors-source.png`; Shadow Ops uses
`examples/shadow-ops_psx_data/actors.png` from the separate examples archive.
The 4-by-4 atlas can be reduced to a 128×128 starting texture with a box filter.
Run from the repository root, after installing the archive, with ImageMagick:

```sh
magick tools/art/shadow-actors-source.png -filter Box -resize 128x128! -strip examples/shadow-ops_psx_data/actors.png
```

This overwrites the game's texture. Preserve any later UV-padding/material edits
or work on a copy before regenerating. The historical prompt mentions a knife
material; current Shadow Ops gameplay is pistol-only.

## Generation prompt

Use case: stylized-concept. Asset type: production low-resolution UV texture atlas for original 1998 PlayStation stealth-action soldier models. Square flat orthographic sheet with an EXACT 4 columns by 4 rows grid of equal square cells touching edge to edge, no gutters, labels, lettering, logos or preview models. Minimal hand-painted late-1990s console pixel-art textures, like the restrained material treatment of Metal Gear Solid PS1, but original character designs. Critically SIMPLE faces: large flat skin color areas, a dark eyebrow/eye band, small angular nose shadow, one tiny mouth shadow; NO detailed eyes, eyelashes, iris, lips, beard, pores, photo skin or individual hair strands. Muted slate blue, charcoal, olive and desaturated tan, 4 to 6 broad flat tones per material. Broad angular painted folds, large simple straps, no fine grain, wear noise, tiny stitching or photorealism. Intended to downsample cleanly to 128x128 total, 32x32 per tile.
Row1 from left: agent torso FRONT, slate-blue fitted tactical suit with dark graphite harness and two simple olive pouches, belt bottom; agent torso BACK matching suit and plain dark vest back; guard torso FRONT desaturated olive/tan suit with dark charcoal vest and simple pouches; guard torso BACK matching.
Row2: agent head FRONT only, dark short hair across top quarter and simple flat warm skin face below, understated unreadable eyes; agent head SIDE dark hair upper half and plain skin lower half with one ear shadow; guard head FRONT with olive cap upper quarter and simple warm skin face below; guard head SIDE olive cap upper half and plain skin lower half.
Row3: agent blue-grey SLEEVE from shoulder top to dark GLOVE bottom, two broad elbow fold shadows; agent charcoal blue-grey TROUSER LEG with one simple olive knee pad center and dark COMBAT BOOT bottom third; guard desaturated olive SLEEVE and dark GLOVE bottom; guard olive/tan TROUSER LEG with charcoal knee pad and dark BOOT bottom third.
Row4: blue-grey jacket side fabric with two broad fold shadows; plain charcoal/olive shoulder armor panel; dark blue-grey gunmetal surface with one simple hard specular stripe; plain silver knife steel with one bright edge stripe.
Each material tile fills its entire square, full opaque coverage. No models, no scenes, no presentation background, no rendered clothes in perspective. No realism beyond simple convincing material shapes. Especially do not turn faces into portrait illustrations. Crisp restrained pixel-art color blocks; character appearance should come from a handful of readable planes.
