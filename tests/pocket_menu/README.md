# Pocket menu verification

The runner compiles all five production menu renderers and their actual
dispatcher against a host OLED framebuffer. The Pocket frames use the same
Adafruit GFX classic5x7 font bytes as firmware. Drawing and rounded-corner
rasterization are test doubles; these files are rendered UI previews, not photos
of a physical OLED. Legacy U8g2 fonts are approximated for an API/navigation
smoke test; no pixel-identity claim is made for those four styles.

```sh
python tests/pocket_menu/run.py --compiler g++ --snapshots test-results/pocket
```

Zig is also supported by passing its executable as `--compiler`. The nine
128x64 binary PGM images show each selected category and the SD-missing state.
No Python graphics dependency is required to run the tests or generate PGMs.

Checks include every category, every raw byte selection, both SD states, actual
text/pixel bounds, exactly one inverted selected row, visible window/scroll
bounds, navigation wrap, all five renderer dispatch paths, every valid legacy
EEPROM appearance combination, SD style aliases, invalid records, repeated
boots and explicit style choices after migration. GPIO/boot-logo bytes outside
the unchanged eight-byte EEPROM32..39 record are checked with canaries.

`font5x7.h` is the classic font from Adafruit GFX Library1.12.6, distributed
under its included BSD notice. All Pocket icon drawings are original code.
