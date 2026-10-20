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

Final firmware installation and on-device Lua results are recorded in the
release test report after the final target build and device run.

## Physical checks still needed

Serial boot and register observations establish startup and bus access. They
do not establish OLED appearance or all button workflows, RF waveform timing,
receiver sensitivity, antenna matching, calibrated power, spectrum behavior,
or bidirectional chat with a physical Flipper. Follow HARDWARE_VALIDATION.md
with a known transmitter/receiver and measurement equipment for those checks.
