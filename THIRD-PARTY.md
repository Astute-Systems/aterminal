# Reused Code And Font Data

## Space Paranoids

Source checkout: `../space-paranoids`, configurable via `SPACE_PARANOIDS_DIR`.

- `engine/src/text.cpp`: vector-stroke font and anti-aliased rendering.
- `engine/src/post.cpp`: software bloom.
- `engine/src/parallel.cpp`: parallel image processing.
- `engine/src/png.cpp`: dependency-free PNG output for verification captures.
- `engine/include/softrender/types.h`: software image storage and color helpers.
- `src/terminal.cpp`: phosphor styling and radial vignette formula, adapted here.
- `adlib/src/adlib_synth.cpp` and `adlib_design.cpp`: AdLib-style FM synthesis;
  the game's `kUiMove` effect supplies the short, attenuated key click.

The renderer and AdLib library are MIT-licensed by Ross Newman. Their original
license files remain in the source checkout and are included by the install rules.
The original synthesizer identifies parts derived from Atlas, copyright 2026
Astute Systems PTY LTD; its existing attribution is preserved in the reused source.

## GNU Unifont

`assets/unifont.hex` is the unmodified `unifont_all-16.0.04.hex` font data from:

https://unifoundry.com/pub/unifont/unifont-16.0.04/font-builds/unifont_all-16.0.04.hex.gz

The exact upstream license is in [assets/UNIFONT-LICENSE.txt](assets/UNIFONT-LICENSE.txt).
The font is distributed separately from application code. Font licensing includes
a font embedding exception; see the upstream notice for full terms and credits.
No copyrighted standards documents or proprietary font data are included.