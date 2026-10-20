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

## Historical release 1 SDK build

The final Lua SDK image was written and all four segments passed explicit
esptool verification. The final image includes Flipper plaintext mode and
five-app diagnostics. Its application image is 1,658,160 bytes, SHA256
`286d1bd5f7f810df885393c1af66f38202a0f311cf8f051f3630394937e8d1f1`.
This last image defaults the chat UI to Flipper plaintext for the reported
Unleashed app. The original ESP ACK format remains selectable.
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
[Lua] SELFTEST PASS version=Lua 5.4.9 peak=14344/49152 instructionQuota=100000 wallUs=5000
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

## Release 2 connected-device checks

At source `c5779ee`, all eleven Russian Lua sources were installed with matching
closed-file SD readback SHA256 confirmations. The real ESP32 VM passed 120
input/tick/draw frames per application. Its largest app memory peak was
32,872 / 49,152 bytes; the longest observed callback was 6,085 us, within the
draw-only 10,000 us budget. Logic callbacks retain their 5,000 us limit.

Source `4d1fd3a` was written as four explicitly verified segments, preserving
the NVS gap. Its application image was 1,694,528 bytes, SHA256
`651fc599b70090dba9a004cdf9feb6f6b0fd0bec0bfe229f3d0879b09a648141`.
After reset the SD mounted and the Russian Pocket menu was captured directly
from the OLED framebuffer. The radio probe again reported PART00 / VERSION14
and zero SPI/ready timeouts.

A finite, IDLE-only XOSC/192 probe counted clock edges on GPIO4 and restored
IOCFG0. The median estimate was 25,995,040 Hz, identifying a nominal 26 MHz
module. This short gated count is not a ppm calibration or a measured carrier.

Controlled USB tests then used the Flipper's internal radio at 433.920 MHz:

- During a known synthetic Flipper OOK transmission, the ESP's receive-only
  monitor measured a peak of -33 dBm versus approximately -84 dBm background.
- The ESP transmitted six finite Princeton periods for the synthetic test
  value A53A53 at nominal 0 dBm. The Flipper decoded that exact value twice,
  with TE 389/390 us and guard factor30. This establishes that waveform's RF
  reception; the ESP's 782 us wall-completion delta is not edge jitter.
- Plain GFSK chat reception still failed: a controlled Flipper CLI send raised
  ESP RSSI to -48 dBm, but no sync/CRC-valid packet was accepted. The following
  USB-triggered ESP chat send hit a loopTask stack canary; that direction of
  that attempt is invalid. Deferred serial UI dispatch was subsequently added
  and requires the next device run.

The attached Flipper identifies as Unleashed086e, commit `e7e4e179`. Its
Enhanced Chat app uses the firmware's radio worker. This differs from the
separately supplied official1.5.1/API88.2 companion FAP build target.

## Physical checks still needed

Serial boot and register observations establish startup and bus access. They
do not establish OLED appearance or all button workflows, RF waveform timing,
receiver sensitivity, antenna matching, calibrated power, spectrum behavior,
or bidirectional chat with a physical Flipper beyond the finite OOK test above. Follow HARDWARE_VALIDATION.md
with a known transmitter/receiver and measurement equipment for those checks.
