# ESP-HACK Radio Lab — v1.3-rf-lab.1

An engineering fork of Teapot174/ESP-HACK, upstream commit
`8c57584c6a39fc3ac948ce2277337a5dd2915baf`, for the existing ESP32 DevKit V1,
SH1106, CC1101, four buttons and microSD wiring. License: AGPL-3.0, with the
original third-party license notices retained. Development branch:
`development/ultimate-radio`.

## Start here

Open **SubGHz → Radio Lab**. The original main applications remain available.
The lab uses an independent radio task and a small monochrome list interface;
display rendering and SD operations do not run in the GPIO edge ISR.

Open **SD Apps** from the main menu to run Lua applications from `/apps`.
Five examples, including Snake and 2048, and the [app SDK](docs/APPS_SDK.md) are included. Install a new
app over USB with `python scripts/install_app.py --port COM4 my_app.lua` while
the main menu is open. This transfers the source to SD and checks its SHA256;
it does not flash the ESP32. Apps can draw, handle buttons, keep time and use
receive-only radio measurements within bounded memory and callback budgets.

- **Radio Settings:** select AM270/AM650/FM238/FM476/FM12K or validated advanced
  modem settings. Save/load/edit/duplicate/delete presets using the preset file
  manager. Presets use portable `.sub` custom-register metadata and retain host
  RSSI/RX/TX deadlines.
- **Read laboratory / Read RAW:** receive a bounded waveform. The laboratory
  decoder recognizes the documented LAB-OOK24 test protocol; the legacy Read
  screen retains its rc-switch decoders. Stop and save to SD with OK.
- **Frequency / Spectrum:** RSSI, carrier indication, peak hold, 64 sweep bins,
  25/50/100 kHz steps and CSV export. CC1101 is a sequential narrowband receiver,
  not a wideband SDR; it cannot observe all bins simultaneously.
- **RAW / Protocol Analyzer:** inspect pulse count, duration, min/max/gaps,
  pulse sequence and LAB-OOK24 payload/CRC. The graph uses a pulse-index axis,
  not a calibrated analog oscilloscope axis.
- **Signal Manager:** bounded folder browsing, current-folder text search,
  validation/metadata, rename, move, duplicate/export and explicit deletion.
  A page holds 40 matching entries. Search narrows matches before that limit.
- **Signal Generator:** one user-defined LAB-OOK24 payload with CRC-8. This is
  a finite generated test frame, not continuous RF output.
- **Modulation Explorer:** compare four official receive presets with RSSI and
  edge activity. Activity is a heuristic, not modulation identification.
- **Music Lab:** stream-inspect RAW file metadata/statistics; stage and play
  binary OOK waveforms up to 2048 pulses and the selected duration limit.
  The RMT peripheral produces pulse durations at a nominal 1 us tick. Wall-time
  completion diagnostics do not measure RF edge jitter. Async FSK playback,
  conventional FM audio and note-to-speaker decoding are not implemented.
- **Chat:** 160 printable ASCII bytes, discovery, peers, 40-byte fragments,
  CRCs, ACKs, retry/backoff, duplicate suppression and RAM message history.
  Install the included FAP on official Flipper firmware 1.5.1/API88.2.
  Chat is unencrypted and unauthenticated.
  Select **Mode: Flipper plain** for the standard CLI / Enhanced Sub-GHz Chat
  plaintext protocol instead. In Enhanced Chat choose **No encryption** and
  the same frequency. This mode broadcasts text without delivery ACKs; see
  [compatibility details](docs/FLIPPER_CHAT_COMPATIBILITY.md).
- **Diagnostics:** chip/register/state/RSSI/FIFO/timeout information, serial
  logs and reset/recovery. Boot performs a receive-only radio probe. Send
  `rfdiag` followed by a newline at 115200 baud while on the main menu to
  repeat that probe.

## Four-button controls

UP/DOWN navigate; OK opens; BACK returns. Text entry uses UP/DOWN to select a
character, OK to append, BACK to delete, hold OK to finish, hold BACK to cancel.
Release the initiating button before the next screen accepts a new action.
Long informational screens scroll with UP/DOWN. Transmission requires a fresh
hold-OK action; BACK aborts RAW playback.

New TX defaults off. In Radio Settings select **Shielded laboratory** only for
the intended controlled setup, then choose a nominal -10/0/+10 dBm setting.
The new ESP32 TX policy allows only the explicit laboratory channels
433.920/868.350 MHz and at most 5 seconds per RAW operation. Chat adds finite
per-packet deadlines, a 100 ms gap and a fixed-window airtime budget. The Flipper
also enforces the official firmware's regional TX permission. These are bounded
laboratory profiles, not a Ukrainian regulatory certification. PA values and
the antenna/matching network have not been measured. The profile resets to RX
only on each Radio Lab launch; it is not written to SD as a TX authorization.

## Build, test, flash and rollback

See [BUILD_AND_FLASH.md](docs/BUILD_AND_FLASH.md), the private device backup
receipt, and the release manifest. The source archive includes all new source,
tests, the FAP project and exact presets. The separate Git bundle preserves the
upstream history and incremental development commits. Public fork:
[Gegcbr2022/ESP-HACK](https://github.com/Gegcbr2022/ESP-HACK), branch
`development/ultimate-radio`.

- [Hardware and wiring](docs/HARDWARE.md)
- [CC1101 source audit](docs/RF_AUDIT.md)
- [Preset provenance and supported settings](docs/RADIO_PRESETS.md)
- [.sub compatibility](docs/SUB_COMPATIBILITY.md)
- [Chat wire protocol](docs/CHAT_PROTOCOL.md)
- [Physical validation procedure](docs/HARDWARE_VALIDATION.md)
- [Feature status and limitations](docs/FEATURE_STATUS.md)
- [Test report](docs/TEST_REPORT.md)

Compiled/native-tested behavior is distinct from physical RF validation. A
successful flash, chip probe or OLED boot cannot establish receiver sensitivity,
accurate RF timing or ESP32 ↔ Flipper interoperability.
