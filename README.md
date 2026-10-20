# ESP-HACK Radio Lab — v1.3-rf-lab.2

ESP32 DevKit V1 + CC1101 + SH1106 radio laboratory, with **Lua applications
installed on SD without rebuilding the firmware**, a four-button interface,
bounded RAW capture/playback and an official-SDK Flipper Zero chat companion.
Release 2 adds the Pocket main menu, a categorized app launcher, eleven SD
apps and a simpler Flipper plaintext chat screen.

This public fork builds on [Teapot174/ESP-HACK](https://github.com/Teapot174/ESP-HACK).
The original GPIO wiring and main applications are retained. The upstream
README is preserved in [README_UPSTREAM.md](README_UPSTREAM.md).

## What changed

- Audited CC1101 reset/SPI/RX transitions; fixed GPIO contention, out-of-bounds
  timing reads, stale decoded payload packing and polling-based RAW capture.
- Exact official Flipper1.5.1 AM270/AM650/FM238/FM476/FM12K presets, validated
  advanced settings and streaming `.sub` import/export with explicit errors.
- Radio Lab: receive, frequency/spectrum/RAW/protocol analysis, file and preset
  management, finite laboratory generator, Music Lab and detailed diagnostics.
- Pocket main menu with icons, selection/scroll indicators and SD status;
  Pages, List, Wii and DSi remain selectable alongside Pocket.
- Simple chat: automatic 433.920 MHz / Flipper plaintext / nominal 0 dBm setup
  for the authorized Faraday laboratory, with manual message sending and
  visible radio status. Enhanced Sub-GHz Chat must use **No encryption**.
  Advanced retains ESP ACK chat with fragmentation/discovery/retries and the
  companion FAP built against official1.5.1/API88.2. Both formats are plaintext.
- SD Apps: Tools/Games/Radio sections, friendly metadata, eight favorites and
  four recent apps persisted on SD. Eleven apps include Snake, 2048, Block Fall,
  Minesweeper, countdown, calculator, unit converter, resistor codes, hello,
  stopwatch and an RF dashboard. The Lua SDK exposes drawing/input/time/RX.
  Apps are Lua source, not Flipper `.fap` binaries or arbitrary ESP32 native
  code. Install by copying `/apps/*.lua` or the supplied USB UART uploader.
- Pinned builds, native tests, explicit hardware validation procedures and
  a device-identity/backup-gated flashing helper.

## Use

Main menu **SubGHz → Radio Lab** or **SD Apps**. See
[the user guide](README_RF_LAB.md), [build/flashing instructions](docs/BUILD_AND_FLASH.md),
[Russian quick start](docs/QUICK_START_RU.md), [app SDK](docs/APPS_SDK.md),
[games](docs/SD_GAMES.md), [utilities](docs/SD_UTILITIES.md),
[Pocket interface](docs/POCKET_UI.md), [chat compatibility](docs/FLIPPER_CHAT_COMPATIBILITY.md),
[hardware validation](docs/HARDWARE_VALIDATION.md) and
[feature status](docs/FEATURE_STATUS.md).

```powershell
python -m pip install platformio==6.2.0
python -m platformio run -e SH1106
python scripts/install_app.py --port COM4 examples/lua_apps/hello.lua
```

Use short paths on Windows; details are in the build guide. The target is the
original ESP32/4MiB/SH1106 board. ESP32 GPIOs and absent IR/nRF hardware are
documented, not guessed.

UP/DOWN selects, OK opens and BACK returns. In SD Apps, choose a group and an
app; hold OK on an app for its information, then OK to star/unstar it. Hold BACK
exits every running app. Change the main menu style under
**Settings → Interface → Menu**.

For chat, open **SubGHz → Radio Lab → Sub-GHz Chat**. Set the Flipper peer to
433.920 MHz and **No encryption**. OK opens the send menu; choose TEST123,
another quick message or Write message and confirm with OK. UP/DOWN reads
history; BACK leaves chat. **Sent** confirms local transmission, without a peer
ACK in plaintext mode. The user reported failed physical reception in both
directions on the preceding firmware; a successful exchange remains unresolved.

## Validation boundaries

The release includes actual ESP32/FAP binaries and portable/mock test results.
Compilation, a successful device flash and a CC1101 register probe do not prove
RF sensitivity, timing, spectral compliance or Flipper interoperability. The
test report separates observed results from pending physical tests. Other
Radio Lab transmit functions require selecting the finite laboratory profile.
Simple Chat temporarily applies its authorized Faraday profile automatically,
ready for manual sends, then restores previous settings on exit. It sends no
automatic beacon. Per-packet deadlines, packet spacing and a finite local
airtime governor remain active. Frequency choices and nominal PA values are
not a regional certification or measured RF power.

Release 2 native tests pass under Windows Zig and Ubuntu GCC: 91,617 Lua checks
with 16,500 app frames, 758,543 catalog checks and 22,235 menu/migration checks.
Final device, RF and release-artifact evidence belongs to the
[test report](docs/TEST_REPORT.md).

Unrelated upstream applications remain available; their hardware behavior is
outside the new test coverage.

License: **AGPL-3.0**. Original Lua, Flipper preset and SmartRC license notices
are preserved in their respective source directories.
