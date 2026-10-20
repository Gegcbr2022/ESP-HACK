# Pocket interface

Pocket is the default main menu in v1.3-rf-lab.2. The128x64 screen shows three
icon rows, an inverted selection, a position counter, a scrollbar, SD status
and button hints. `SD` means the card is mounted; `--` means unavailable. It does
not imply battery monitoring or radio transmission. Icons are original line
drawings; no Flipper mascot or artwork is included.

UP/DOWN move through the same eight categories; OK opens the selected one.
Settings → Interface → Menu offers Pages, List, Wii, DSi and Pocket. Existing
category IDs and functions remain unchanged.

Chat is directly available at **SubGHz → Chat**. From the first SubGHz item,
Read, press UP once to reach it. Radio Lab remains available at its existing
position, with its original Chat entry as well.

The first boot after upgrading changes only the saved main menu style to
Pocket. The existing eight-byte appearance record stays at EEPROM32..39 with
a new signature. Valid submenu, color and standby selections are retained;
GPIO, WiFi/BLE names and the saved boot-logo path are not reset. Legacy SD-only
appearance settings retain those fields while adopting Pocket. Subsequent
style choices, including any older renderer, persist across restarts.

If EEPROM saving fails, the serial log reports the failure; a later boot may
retry migration. When no valid record and no SD card are available, Pocket
works immediately and migration is persisted when configuration can be loaded.

Host previews and migration tests are generated with
`python tests/pocket_menu/run.py --compiler g++ --snapshots test-results/pocket`.
They exercise the production renderers with a framebuffer double. OLED viewing
angle, contrast and physical button behavior still need an on-device check.
