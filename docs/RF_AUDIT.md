# CC1101 receive audit and validation

Physical validation is pending. Source inspection demonstrates several software
failure mechanisms; it cannot establish which wiring, antenna, crystal or supply
issue exists on the assembled device. Compilation and mock tests are separate
from reception measurements.

## Hardware preserved

| Component | Original ESP32 GPIO assignment |
| --- | --- |
| CC1101 SCK / MISO / MOSI / CS | 18 / 19 / 23 / 5 (VSPI) |
| CC1101 GDO0 | 4 |
| CC1101 GDO2 | Not connected/required by the existing board |
| MicroSD SCK / MISO / MOSI / CS | 14 / 17 / 13 / 15 (HSPI) |
| SH1106 SDA / SCL | 21 / 22 |
| Up / Down / OK / Back | 27 / 26 / 33 / 32 |

SD and CC1101 use different SPI controllers and pins. The original code does not
show an SD/CC1101 shared-bus collision. SD latency still blocked the old polling
recorder. This build fixes GDO0/CS at 4/5 even when an old NRF24 EEPROM setting
requests those pins. NRF24 CE/CS defaults remain 16/2; attempting to initialize
NRF24 on reserved CC1101 pins fails explicitly. Adding NRF24 still requires
verifying its wiring and chip-select isolation.

## Confirmed source defects and corrections

Line references below refer to the unmodified upstream `src/subghz.cpp` at the
baseline commit, not the changing release line numbers.

| Source evidence | Failure mechanism | Correction |
| --- | --- | --- |
| `MAX_DATA_LOG=512`, line 946 | rc-switch has a 67-entry nonterminated raw array; scanning for a zero can read outside it | Remove that scan. Standard Read snapshots decoded metadata; independent RAW capture handles unknown waveforms |
| Lines 911–924 | Key packing uses the previous bit length before assigning the new bit length | Snapshot all decoder fields together and pack using the received length |
| Lines 512–514 | Auto-save compares only the first key byte, which is zero for decoded 32-bit keys | Compare the complete packed key to suppress immediate duplicates |
| Lines 1417–1443 and 630–634 | GPIO polling misses edges during OLED transfer and synchronous SD writes | CHANGE interrupt pushes signed intervals into a bounded 2048-entry ring; UI/storage only consume it |
| Lines 1386–1389, 1438 | RSSI is read after a silence gap, so completed valid bursts can be rejected using the noise floor | Sample recent in-burst RSSI outside the ISR; treat missing samples as unmeasured rather than weak |
| Lines 1349 and 886 | Export says Ook650 while the receiver uses 270 kHz bandwidth | Export `FuriHalSubGhzPresetOok270Async` and restore the corresponding actual receive registers |
| Lines 326–329 | MCU drives GDO0 LOW after switching the CC1101 to receive | Stop in IDLE and release the MCU pin before any RX output |
| Lines 883–892 | RX configuration leaves packet format, sync, GDO routing and AGC dependent on prior applications | Apply/read back the complete AM270 receive configuration in IDLE |
| Line 320 | SCAL is issued after entering RX/TX, although it requires IDLE | Use the configured automatic IDLE-to-RX/TX calibration; confirm RX through MARCSTATE |
| Line 296 | A reset SPI strobe is issued before the library initializes SPI | Let library Init perform SPI setup and its reset sequence |
| `setDeviation(0)` with SmartRC 3.0.2 | The setter evaluates `log(0)` and converts the result to a byte | Write the ignored OOK deviation register directly |
| No explicit overflow/SD write outcome | Partial RAW buffers and incomplete saves could look successful | Reject damaged/overfull frames, count losses, check writes, discard sessions after a storage failure, and show an end-of-session warning |

The legacy decoder has a 32-bit payload limit. Its Princeton/CAME labels retain
upstream encoding heuristics; a matching pulse encoding is not proof of device
brand, access protocol or signal authenticity. No protocol is inferred from RSSI.
Standard Read cannot decode arbitrary signals; use RAW for timing capture.

## Driver and radio configuration findings

The retained SmartRC 3.0.2 driver uses SPI mode 0 at 4 MHz and assumes a 26 MHz
crystal. It supplies SPI transactions and a 200 ms MISO-ready timeout. Its public
API does not expose the timeout result: reads may return zero and writes may be
silently skipped. Register readback failures are therefore error evidence, not a
count of every electrical SPI fault. A logical state timeout can exceed its
nominal 20 ms because a driver SPI call can itself wait up to 200 ms.

Chip detection reads VERSION and rejects 00/FF. Diagnostics log PARTNUM,
VERSION, MARCSTATE, actual fixed pins, selected frequency, configuration readback
errors and FIFO recovery count. VERSION alone does not certify a genuine chip.

The AM270 receive profile explicitly sets:

- IOCFG0=0D, PKTCTRL0=32: asynchronous serial data at GDO0, continuous length,
  no whitening/packet CRC. Raw reception bypasses the FIFO packet engine.
- MDMCFG4=67, MDMCFG3=32: 270.833333 kHz RX bandwidth and approximately
  3.79372 kbaud filter configuration at a 26 MHz crystal.
- MDMCFG2=30: ASK/OOK, no sync/preamble qualifier.
- AGCCTRL0/1/2=40/00/03 and FREND1=B6: official Flipper AM270 AGC/front-end
  settings, rather than the unrelated driver defaults.
- MCSM0=18: synthesizer calibration on transition from IDLE. RX overflow is
  inspected and the FIFO is flushed only from IDLE. An async path should not
  accumulate packet-FIFO traffic; overflow suggests stale configuration.
- FSCTRL0=00: no assumed crystal correction. Frequency error must be measured;
  undocumented library default offsets are not a calibration of this device.

AM650 has different AGC/front-end settings as well as bandwidth. FM238 means
2-FSK with 2.380371 kHz deviation, while FM476 means 47.60742 kHz deviation;
the names are not data rates. The Radio Lab preset layer owns other modes and
packet settings; this audit patch only restores the legacy receive application.

The device still cannot measure crystal frequency from software alone. Confirm
the module marking/specification or compare a known transmitter against a
measured reference. A different crystal requires matching frequency/rate/filter
calculation, not an assumed correction.

## RAW engine API and operating limits

`research::raw_capture::start(pin)`, `stop()`, `pop(int32_t&)`, and `snapshot()`
are declared in `src/research/raw_capture.h`. The engine has one owner at a time.
Call `suspendLegacyReceive()` before a different application takes the radio,
then configure it. Restore the legacy application with `restoreReceiveMode()`.

Positive intervals represent HIGH, negative intervals LOW, in microseconds. The
ISR performs a timestamp, direct GPIO read, bounded ring operation and counters
under an ESP32 ISR critical section. It performs no SPI, SD, display, allocation
or decoding. Snapshot/pop use the matching task critical section. The queue is
8192 bytes plus counters; its initial and final partial idle levels are bounded
by capture start/stop, not a claim about an entire RF message.

Counters report edges, stored intervals, queue drops, rejected repeated/zero or
unrepresentable intervals, occupancy, high-water mark, last level and edge time.
Durations wrap correctly across the 32-bit microsecond clock when an individual
interval is at most INT32_MAX microseconds. Interrupt latency is included in
timing error. Flash operations that mask interrupts and electrical edges faster
than interrupt service can still lose information; software counts cannot count
every edge that never reached the ISR. This is not hardware-timer/RMT capture.

The legacy recorder bounds a single frame at 900 intervals, delimits frames by
an observed interval of at least 7000 us, and rejects frames beyond that bound.
A 7000 us separator is an application policy, not universal protocol detection.
The ring continues collecting while complete frames go to SD. Any queue loss or
rejected ISR interval invalidates the current frame and queued backlog; the
recorder resynchronizes on a future observed gap. Loss is logged and displayed
when capture stops. Each exported RAW_Data line has at most 512 values. A line
break adds no waveform delay. Failed SD writes invalidate and discard the
session rather than leaving an apparently complete partial file.
Empty sessions are discarded with a "No valid RAW frames" indication.

RSSI sampling is opportunistic at approximately 1 ms outside the ISR. It can
miss a short burst, and a busy backlog can associate a peak with adjacent
activity. It is only a receive filter/indicator. No sample preserves the frame;
a measured peak below the configured threshold rejects it. This is not a
calibrated RF power measurement or modulation identification.

The legacy app yields to FreeRTOS at least once per outer menu iteration. Capture
remains interrupt-driven during I2C display updates. Radio register access and
UI ownership remain single-application operations; this patch does not create a
second concurrent radio owner.

## Repeatable physical checks (pending)

1. Record module markings, antenna band, supply voltage and firmware commit.
   Use only a locally permitted test channel and user-created waveform. Select
   the same frequency on both devices. A matching number does not establish
   legal transmit permission or antenna suitability.
2. Open USB serial at 115200 baud. Enter legacy SubGHz Standard Read. Save the
   complete `[CC1101]` line and any readback/state-timeout errors. Expect GDO0=4,
   CS=5, AM270 and eventual MARCSTATE=0D. Repeat after entering/exiting SD file
   browsing, frequency analysis and another radio mode.
3. Create a laboratory RAW file on Flipper with
   `Preset: FuriHalSubGhzPresetOok270Async`, `Protocol: RAW`, the permitted
   frequency in hertz, and a distinct known pattern, e.g. alternating 500/-500
   us levels with a long idle between bursts. Ensure the complete file follows
   the official `.sub` format. Use finite sends and the normal Flipper region
   restriction. Keep the devices roughly 1–3 m apart to begin; too much received
   power can overload a receiver.
4. Start RAW recording before transmission, send the same finite test ten times,
   stop recording and retain the `[RAW]` summary plus SD file. Record transmitted
   pulse count, received count, median absolute timing error, worst timing error,
   edge/drop/rejection counters and queue high-water mark. Do not equate a saved
   file with a successful timing comparison.
5. Repeat while the normal OLED preview updates and with the same SD card. A
   nonzero drop/rejection counter invalidates affected frames. Repeat with a
   slower waveform to separate software-service limits from RF sensitivity.
6. Test end-to-end presets in Radio Lab only when both devices use the exact same
   corresponding registers. AM650/FM238/FM476 are separate tests; the legacy
   AM270 recorder is not evidence that FSK works. Record both preset names and
   actual register/configuration logs for each run.
7. If no edges arrive despite correct state/register readback, check GDO0 at the
   module and ESP32 and verify continuity. If SPI readback fails, check CS/SCK/
   MISO/MOSI and supply before tuning AGC. If edges arrive but timing/decoding
   fails, compare the two saved pulse sequences and preset configurations.

Please provide the complete boot/CC1101/RAW serial log, both `.sub` test files,
Flipper firmware version and the selected legal channel. Physical verification,
range, sensitivity, calibration, watchdog endurance and SD worst-case latency
remain pending until those measurements exist.

## Automated capture validation

`tests/raw_capture_mock/run.py` compiles the actual `raw_capture.cpp` against
mock Arduino/GPIO/IRQ surfaces. Its always-enabled checks cover signed timing,
microsecond-clock wrap, both GPIO input banks, queue bounds, overflow accounting,
head/tail wrap and ordering, repeated/zero/oversized interval rejection, invalid
pins and stop/reset. The staged implementation passed these checks with Zig
0.13.0 C++11, `-Wall -Wextra -Werror`, before integration. Example:

```
python tests/raw_capture_mock/run.py --cxx /path/to/zig --zig
```

The mocks execute the actual registered ISR callback but replace critical-section
primitives. They do not validate ESP32 interrupt latency, electrical GPIO edges,
cross-core contention, RF reception, SD stalls or power integrity. Target
compilation and physical timing verification must be reported separately.
The phase-2 changes also compiled for SH1106 / ESP32 DevKit with the pinned
espressif32 6.12.0 / Arduino 2.0.17 toolchain. RF/timing behavior remains physically
unverified.

## Primary references

- [TI CC1101 datasheet](https://www.ti.com/lit/gpn/cc1101): SPI, status/state
  machine, asynchronous mode, modulation and register definitions.
- [Official Flipper preset registers](https://github.com/flipperdevices/flipperzero-firmware/blob/1.5.1/lib/subghz/devices/cc1101_configs.c)
  and [preset naming](https://github.com/flipperdevices/flipperzero-firmware/blob/1.5.1/lib/subghz/subghz_setting.c).
- [Official Flipper SubGHz file format](https://github.com/flipperdevices/flipperzero-firmware/blob/1.5.1/documentation/file_formats/SubGhzFileFormats.md).
- [Retained SmartRC driver](https://github.com/LSatan/SmartRC-CC1101-Driver-Lib/blob/master/SmartRC_CC1101.cpp).
- [rc-switch receive implementation](https://github.com/sui77/rc-switch/blob/master/RCSwitch.cpp)
  and [its timing-array bound](https://github.com/sui77/rc-switch/blob/master/RCSwitch.h).
