# Connected-device operations

This report concerns one attached ESP32 DevKit / SH1106 board, not all boards
or all functions. Private dumps, device identifiers and raw serial/flashing
logs are retained locally and are excluded from the public fork and release.

## Backup and first hardware check

- Windows initially exposed a CP2102 bridge without a driver. A matching,
  catalog-signed driver was installed, and the bridge became COM4.
- The chip identified as ESP32-D0WD-V3 revision 3.1 with 4 MiB flash.
- A complete 4,194,304-byte host dump was saved before any firmware write.
  Its saved-file SHA256 and independent full-image esptool device MD5
  verification succeeded. An interrupted second host read is not counted.
- The first RF laboratory build was written as four verified segments, leaving
  the NVS gap intact. The original full-flash backup remains the rollback image.
- Actual boot output reported CC1101 PART=00 / VERSION=14, no SPI errors or
  ready timeouts, and AM270 asynchronous RX with MARCSTATE=0D. The SD card
  mounted and SH1106 UI initialization reached the main menu.

## Final SDK build

The final Lua SDK image was written and all four segments passed explicit
esptool verification. The final image includes Flipper plaintext mode and
five-app diagnostics. Its application image is 1,658,160 bytes, SHA256
`ffc1cb663ffcad8c65bcbbaa6f1b5e578a6c780ce356ad846bc56b52e7106be1`.
After reset, the receive-only probe passed again with CC1101 VERSION=14,
MARCSTATE=0D and zero ready timeouts. SD mounted and UI initialization reported
121,544 free heap bytes. The user confirmed that the OLED menu and buttons work
during the preceding SDK build; final startup reached the same main menu.

The USB installer wrote `hello.lua` (895 bytes), `stopwatch.lua` (1,522 bytes)
and `rf_dashboard.lua` (1,996 bytes), followed by `snake.lua` (3,541 bytes) and
`game2048.lua` (4,230 bytes). Each received a matching `APP_INSTALLED`
size/SHA256 confirmation after the SD file was closed and read back.

Actual `appsdiag` output then reported:

```text
[Lua] SELFTEST PASS version=Lua 5.4.9 peak=14144/49152 instructionQuota=100000 wallUs=5000
[Lua] SAMPLE name=hello.lua result=PASS peak=16784/49152
[Lua] SAMPLE name=stopwatch.lua result=PASS peak=17936/49152
[Lua] SAMPLE name=rf_dashboard.lua result=PASS peak=20832/49152
[Lua] SAMPLE name=snake.lua result=PASS peak=23464/49152
[Lua] SAMPLE name=game2048.lua result=PASS peak=25024/49152
```

The sample check loads source from SD and calls one tick/draw cycle in each
fresh VM. Radio start is disabled in this diagnostic context. These results
establish actual installation/VM execution and quota rejection, not interactive
button coverage or physical RF measurements. UART communication stopped once
after the reset/port sequence and recovered after the user reconnected USB;
the successful sample checks were captured after that reconnection.

## Physical checks still needed

Serial boot and register observations establish startup and bus access. They
do not establish OLED appearance or all button workflows, RF waveform timing,
receiver sensitivity, antenna matching, calibrated power, spectrum behavior,
or bidirectional chat with a physical Flipper. Follow HARDWARE_VALIDATION.md
with a known transmitter/receiver and measurement equipment for those checks.
