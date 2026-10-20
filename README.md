# ESP-HACK Radio Lab

ESP32 DevKit V1 + CC1101 + SH1106 radio laboratory, with **Lua applications
installed on SD without rebuilding the firmware**, a four-button interface,
bounded RAW capture/playback and an official-SDK Flipper Zero chat companion.

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
- ESP32 ↔ Flipper chat: shared2FSK profile,160 ASCII bytes, fragmentation,
  discovery, CRCs, ACK/retry/backoff and duplicate suppression. Real FAP built
  against official1.5.1/API88.2; chat is unencrypted.
- SD Apps: bounded Lua scripts, drawing/input/time/RX APIs and example apps.
  Apps are Lua source, not Flipper `.fap` binaries or arbitrary ESP32 native
  code. Install by copying `/apps/*.lua` or the supplied USB UART uploader.
- Pinned builds, native tests, explicit hardware validation procedures and
  a device-identity/backup-gated flashing helper.

## Use

Main menu **SubGHz → Radio Lab** or **SD Apps**. See
[the user guide](README_RF_LAB.md), [build/flashing instructions](docs/BUILD_AND_FLASH.md),
[app SDK](docs/APPS_SDK.md), [chat protocol](docs/CHAT_PROTOCOL.md),
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

## Validation boundaries

The release includes actual ESP32/FAP binaries and portable/mock test results.
Compilation, a successful device flash and a CC1101 register probe do not prove
RF sensitivity, timing, spectral compliance or Flipper interoperability. The
test report separates observed results from pending physical tests. New TX
requires an explicit finite laboratory profile; frequency choices and PA table
values are not a regional certification.

Unrelated upstream applications remain available; their hardware behavior is
outside the new test coverage.

License: **AGPL-3.0**. Original Lua, Flipper preset and SmartRC license notices
are preserved in their respective source directories.
