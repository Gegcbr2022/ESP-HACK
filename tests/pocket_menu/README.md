# Pocket menu verification

The runner compiles all five production menu renderers and their actual
dispatcher against a host OLED framebuffer. The Pocket frames use the same
Adafruit GFX classic5x7 ASCII font bytes and original Russian glyph bitmaps as
firmware. Drawing and rounded-corner
rasterization are test doubles; these files are rendered UI previews, not photos
of a physical OLED. Legacy U8g2 fonts are approximated for an API/navigation
smoke test; no pixel-identity claim is made for those four styles.

```sh
python tests/pocket_menu/run.py --compiler g++ --snapshots test-results/pocket
```

Zig is also supported by passing its executable as `--compiler`. The ten
128x64 binary PGM images show each selected category, the SD-missing state and
the complete Russian alphabet.
No Python graphics dependency is required to run the tests or generate PGMs.

Checks include every category, every raw byte selection, both SD states, actual
text/pixel bounds, exactly one inverted selected row, visible window/scroll
bounds, navigation wrap, all five renderer dispatch paths, every valid legacy
EEPROM appearance combination, SD style aliases, invalid records, repeated
boots and explicit style choices after migration. GPIO/boot-logo bytes outside
the unchanged eight-byte EEPROM32..39 record are checked with canaries.

Russian checks inspect known Ж/Ё/ё pixels, black ink on an inverted row,
scale2, viewport clipping, preserved ASCII behavior and UTF-8 glyph boundaries.
Generate a PNG contact sheet without extra packages:

```sh
python tests/pocket_menu/preview.py test-results/pocket test-results/pocket.png
python tests/pocket_menu/preview.py test-results/pocket test-results/alphabet.png --alphabet
```

`font5x7.h` is the classic font from Adafruit GFX Library1.12.6, distributed
under its included BSD notice. All Pocket icon drawings are original code.
