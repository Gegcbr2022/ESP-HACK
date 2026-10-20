# Reproducible build, device backup and rollback

## ESP32 build

The unmodified upstream build was attempted first. Unpinned PlatformIO resolved
ESP32 platform 8.0.0/Arduino3.3.12, which fails in the bundled OneWireHub because
its `MEM_SIZE` identifier collides with a new SDK macro. Original sources built
successfully with **espressif32 6.12.0 / Arduino 2.0.17 / GCC8.4.0**. Dependencies
are now pinned in `platformio.ini`. The same SmartRC3.0.2 driver is vendored with
the documented bounded-wait/reset/error fixes; it has not been blindly replaced.

```powershell
python -m venv .tooling
.\.tooling\Scripts\python.exe -m pip install platformio==6.2.0
.\.tooling\Scripts\python.exe -m platformio run -e SH1106 -j 8
```

Use a short checkout path on Windows. Registry library examples can exceed
MAX_PATH in deeply nested directories, and the old Xtensa toolchain may fail
to spawn `cc1plus` when its own installation path contains spaces. In this
session temporary drive mappings solved both issues without changing sources:

```powershell
subst W: "<workspace>\work"
subst P: "%USERPROFILE%\.platformio"
$env:PLATFORMIO_PACKAGES_DIR='P:\packages'
Set-Location W:\ESP-HACK
W:\tooling\Scripts\python.exe -m platformio run -e SH1106 -j 8
```

The `%USERPROFILE%` notation above is illustrative; PowerShell users should
pass their actual path or `Join-Path $env:USERPROFILE '.platformio'`. Use unused
drive letters. Temporary mappings can be removed with `subst W: /d` and
`subst P: /d` after all processes using them exit.

The build generates `.pio/build/SH1106/firmware.bin`, bootloader and partition
images, plus `esp-hack_v1.3-rf-lab.1-sh1106-full.bin`. `build.py` uses PlatformIO's
installed Python esptool, not the original repository's bundled executable.
The full image includes the OTA initialization data. Its target is 4 MiB flash.

## Native tests

Use native GCC/Clang or a portable Zig0.13.0 executable. The Xtensa compiler
produces ESP32 code and cannot execute native Windows tests.

```powershell
python tests/portable/run_formats_tests.py --compiler <zig.exe>
python tests/portable/run_lab_tests.py --compiler <zig.exe>
python tests/run_chat_tests.py --compiler <zig.exe>
python tests/spi_driver_mock/run_spi_tests.py --compiler <zig.exe>
python tests/raw_capture_mock/run.py --cxx <zig.exe> --zig
python tests/lua_sandbox/run.py --compiler <zig.exe>
```

Compile `tests/portable/legacy_guard_tests.cpp` with `radio_config.cpp` and
`sub_file.cpp`, C++11 and `-Isrc`, then execute the native result. Additional
triggered-capture tests and a consolidated runner are included with the source.
See the test report for actual commands/results and limits of the mocks.

## Flipper build

See `flipper_apps/subghz_chat/README.md` and its `build.ps1`. The checked build
uses uFBT0.2.6, official firmware/SDK **1.5.1**, target7, API88.2 and a successful
strict import check. Copy `esphack_subghz_chat.fap` to `/ext/apps/Sub-GHz/`.
Other Flipper firmware versions may need a matching SDK rebuild.

## SD app installation after the runtime firmware

See [APPS_SDK.md](APPS_SDK.md) for the Lua API, `/apps` layout and examples.
Copying a new `.lua` file to SD, or installing it with `scripts/install_app.py`,
does not rebuild or flash firmware. A firmware update is required once to add
the runtime itself. Lua applications are separate from Flipper `.fap` files.

## Preserve the connected device first

The connected USB bridge was identified as CP2102, VID10C4/PIDEA60. Official
Microsoft Update Catalog driver11.5.0.417 matched those IDs. Its INF/SYS catalog
membership and kernel-policy signatures were verified before Windows UAC
installation. Device Manager then reported COM4 with no error.

The original firmware built from GitHub is a **source baseline**, not a backup
of the user's device. A real backup is a full chip read including bootloader,
partition table, NVS, both application slots and other storage. Treat it as
private: it may contain network settings or other device data.

`scripts/device_flash.py` reads the entire chip to a host file, computes its
SHA256, then runs an independent `esptool verify_flash 0 <image>` over that
complete image. The latter checks the device's full-flash MD5 against the saved
file; a successful receipt records `verification_method: esptool-full-flash-md5`
and one completed host read. MD5 here is esptool's transfer/integrity check,
not an authenticity signature. The SHA256 protects the saved file against later
changes. Example (replace paths/port):

```powershell
python scripts/device_flash.py --esptool <trusted-esptool.py> --port COM4 --baud 115200 backup --directory <private-backup-directory>
```

Optional `--double-read` instead reads the complete chip twice to the host and
requires identical SHA256 values before certifying a two-host-read receipt:

```powershell
python scripts/device_flash.py --esptool <trusted-esptool.py> --port COM4 --baud 115200 backup --directory <private-backup-directory> --double-read
```

An interrupted read is never a completed backup or an independent successful
comparison. For the connected device, one 4 MiB host dump completed and the
independent full-image device verification succeeded. A second host dump was
interrupted near completion; it is not counted as a second verified host read.
The private receipt/logs retain the exact method and results.

## Flash after a verified backup

```powershell
python scripts/device_flash.py --esptool <trusted-esptool.py> --port COM4 --baud 115200 flash --backup-receipt <backup.json> --release-manifest <release-manifest.json>
```

The helper verifies the backup image/receipt, matches the attached chip MAC,
checks every release segment's SHA256/size and requires ESP32/SH1106/4MiB.
Bootloader0x1000, partitions0x8000, OTA data0xE000 and application0x10000 are
written separately so the NVS region0x9000–0xDFFF is preserved. It then runs
explicit flash verification and resets. Existing SD files are not touched by
the host flashing script. The firmware creates `/subghz/presets` as needed.

A merged full image is also supplied for a fresh 4MiB device. Writing that image
at address0 can overwrite the NVS gap with padding; use the segment/receipt
workflow when preserving the connected device's NVS matters.

Capture serial output at115200 after reset. Look for the exact firmware version,
`[RadioLab] probe=PASS`, chip ID/registers, and `UI ready`. A missing SD card can
leave the existing error dialog open; select its **OK** button to continue.

## Restore the actual device backup

```powershell
python scripts/device_flash.py --esptool <trusted-esptool.py> --port COM4 --baud 115200 restore --backup-receipt <backup.json>
```

This checks the backup SHA256 and attached chip MAC, writes the entire saved
flash at0, verifies it, and resets. It restores the captured NVS and both OTA
slots. Keep the receipt, SHA256 and binary together. The separate source
baseline full image is only a secondary fallback and does not restore private
device state. Git source rollback: check out tag `baseline-upstream-8c57584`,
use the external/pinned compatible build configuration, and rebuild. The tag
itself intentionally preserves the unmodified upstream configuration.
