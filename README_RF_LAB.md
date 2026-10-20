# ESP-HACK Radio Lab — v1.3-rf-lab.2

An engineering fork of Teapot174/ESP-HACK, upstream commit
`8c57584c6a39fc3ac948ce2277337a5dd2915baf`, for the existing ESP32 DevKit V1,
SH1106, CC1101, four buttons and microSD wiring. License: AGPL-3.0, with the
original third-party license notices retained. Development branch:
`development/ultimate-radio`.

## Start here

The default **Pocket** main menu has category icons, three visible rows,
selection/scroll indicators and SD status. **Settings → Interface → Menu**
offers five styles: Pages, List, Wii, DSi and Pocket. Existing application
category IDs and wiring remain unchanged; see [Pocket interface](docs/POCKET_UI.md).
Open **SubGHz → Radio Lab** for radio tools. Its independent radio task keeps
display rendering and SD operations outside the GPIO edge ISR.

Open **SD Apps** from the main menu to run Lua applications from `/apps`.
The launcher groups apps into All apps, Tools, Games, Radio, Favorites and
Recent. Comment metadata supplies friendly titles, categories and descriptions;
up to eight favorites and four recent apps persist on SD. Eleven source apps
include four games, a countdown, calculator, unit converter, resistor code
reader, hello, stopwatch and receive-only RF dashboard. See the
[SDK](docs/APPS_SDK.md), [utilities](docs/SD_UTILITIES.md) and
[games](docs/SD_GAMES.md). Install a new app over USB with
`python scripts/install_app.py --port COM4 my_app.lua` while
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
- **Chat:** a simple read/send screen opens with 433.920 MHz, Flipper plaintext
  and nominal 0 dBm selected automatically for the user's authorized Faraday
  setup. OK opens message entry or quick messages such as TEST123; UP/DOWN
  browse history. On Enhanced Sub-GHz Chat choose **No encryption** and the same
  frequency. Sent means local transmission, with no peer ACK. Advanced mode
  retains the ESP ACK protocol and companion official-1.5.1/API88.2 FAP.
  Both formats support up to 160 printable ASCII text bytes and remain
  unencrypted and unauthenticated. See [compatibility details](docs/FLIPPER_CHAT_COMPATIBILITY.md).
- **Diagnostics:** chip/register/state/RSSI/FIFO/timeout information, serial
  logs and reset/recovery. Boot performs a receive-only radio probe. Send
  `rfdiag` followed by a newline at 115200 baud while on the main menu to
  repeat that probe.

## Four-button controls

UP/DOWN navigate; OK opens; BACK returns. Text entry uses UP/DOWN to select a
character, OK to append, BACK to delete, hold OK to finish, hold BACK to cancel.
Release the initiating button before the next screen accepts a new action.
Long informational screens scroll with UP/DOWN. RAW transmission requires a
fresh hold-OK action; BACK aborts RAW playback. Chat uses its manual send menu.

Radio Lab TX defaults off. In Radio Settings select **Shielded laboratory** for
the intended controlled setup, then choose a nominal -10/0/+10 dBm setting.
Opening the simple Chat screen temporarily applies its automatic 433.920 MHz /
Flipper plaintext / nominal 0 dBm TX-ready profile for this authorized setup;
messages still require a manual send action. Leaving Chat restores the previous
settings. It does not send an automatic beacon.
The new ESP32 TX policy allows only the explicit laboratory channels
433.920/868.350 MHz and at most 5 seconds per RAW operation. Chat adds finite
per-packet deadlines, a 100 ms gap and a local 1-second/60-second attempt-time
governor. The Flipper
also enforces the official firmware's regional TX permission. These are bounded
laboratory profiles, not a Ukrainian regulatory certification. PA values and
the antenna/matching network have not been measured. The profile resets to RX
only on each Radio Lab launch; it is not written to SD as a TX authorization.

The user reported failed reception in both ESP32/Flipper directions on the
preceding firmware. Release 2 adds the simpler setup and packet/CRC/FIFO/state
diagnostics; a successful RF exchange has not yet been established. Final
release 2 image identification, flash verification and the eleven-app on-device
diagnostic remain pending the release owner's recorded results. Historical
release 1 flash/probe/app evidence is kept separately in the test report.

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

Release 2 native checks pass on Windows Zig and Ubuntu GCC: 91,617 Lua checks
with 16,500 sample frames across eleven apps, 758,543 catalog checks and 22,235
menu/rendering/migration checks. These results cover software behavior and
bounds; they do not establish physical OLED/button usability or RF performance.

Compiled/native-tested behavior is distinct from physical RF validation. A
successful flash, chip probe or OLED boot cannot establish receiver sensitivity,
accurate RF timing or ESP32 ↔ Flipper interoperability.
