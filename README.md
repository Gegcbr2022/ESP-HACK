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
- File Send supports one bounded binary OOK RAW pass or six periods of
  RcSwitch (protocol 1, 1..32 bits) / Princeton 1..32 / CAME 12/24. Dynamic protocols such as KeeLoq, Alutech and
  StarLine remain inspection-only; unsupported files are refused explicitly.
  Non-24 Princeton is an ESP-HACK legacy-input extension, while official
  Flipper Princeton remains 24-bit. Protocol names are matched exactly.
  Radio files use pages of 39 paths plus a separate Next Page row, preserving
  complete paths up to 1023 bytes. The header shows page/total counts;
  holding DOWN/UP changes pages quickly and BACK returns to the previous page.
- Radio Lab: receive, frequency/spectrum/RAW/protocol analysis, file and preset
  management, finite laboratory generator, Music Lab and detailed diagnostics.
- Russian Pocket menus, basic settings labels, button hints and all eleven SD
  apps; native Cyrillic font and bounded UTF-8 rendering. Advanced legacy
  technical screens retain some English labels. Pocket has icons,
  selection/scroll indicators and SD status;
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

The default Russian menu opens **Радио SubGHz** directly into receive/files/chat/settings,
with technical tools under **Дополнительно**. SD apps open through **Приложения SD**. See
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
**Настройки → Интерфейс → Главное меню**.

For chat, open **Радио SubGHz → Чат Flipper**. Set the Flipper peer to
433.920 MHz and **No encryption**. OK opens the send menu; choose TEST123,
another quick message or **Написать текст** and confirm with OK. UP/DOWN reads
history; BACK leaves chat. **Sent** confirms local transmission, without a peer
ACK in plaintext mode. Physical reception still failed in both directions on
the Russian `c5779ee` build. The connected Flipper was identified as Unleashed
`unlshd-086e` (`e7e4e179`), with Enhanced Sub-Ghz Chat 1.2 in its SD backup.
A later controlled one-way test received valid-CRC Flipper packets after a
device-specific frequency offset was applied. Calibrated bidirectional chat
still requires final validation; see the test report.

## Validation boundaries

The release includes actual ESP32/FAP binaries and portable/mock test results.
Compilation, a successful device flash and a CC1101 register probe do not prove
RF sensitivity, timing, spectral compliance or Flipper interoperability. The
test report separates observed results from pending physical tests.
The simple Chat and file Send screens temporarily apply the authorized Faraday
profile at nominal 0 dBm, then restore previous settings. Both require a manual
send action. File Send requires a fresh hold OK, preserves the exact file
frequency, and accepts only 433.920/868.350 MHz, at most 2048 intervals and
5 seconds per operation. Other Radio Lab transmit tools require selecting the
laboratory profile explicitly. Chat additionally enforces packet deadlines,
spacing and a 1-second/60-second attempt-time governor; this rolling budget is
not a RAW playback governor. Frequency choices and nominal PA values are not
a regional certification or measured RF power.

Release 2 native tests pass under Windows Zig and Ubuntu GCC: 91,990 Lua checks
with 16,500 app frames, 758,551 catalog checks and 21,892 menu/migration checks.
Static encoders passed 1,066,348 checks; pagination passed 1,108,737 checks; the
automatic chat profile/carrier gate passed 1,048,643 checks and the mock clock probe passed 246.
The actual browser passed 5,026 checks, UART dispatch 1,489 and calibration
math/persistence 6,730,289. These use host adapters rather than physical radio.
The `c5779ee` CI run passed all three jobs, and all eleven Russian SD apps
passed 120 diagnostic frames each on ESP32 with radio starts disabled.
The `e23c31d` diagnostic candidate was verified flashed and yielded the one-way
RF evidence. All three CI jobs also passed at `28e4d46`, including full-path
browsing and device calibration. The later `d63f1ca` carrier-gate correction
awaits final CI and bidirectional device checks.
Exact source states and limits are in the [test report](docs/TEST_REPORT.md).

Unrelated upstream applications remain available; their hardware behavior is
outside the new test coverage.

License: **AGPL-3.0**. Original Lua, Flipper preset and SmartRC license notices
are preserved in their respective source directories.
