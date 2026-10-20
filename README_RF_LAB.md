# ESP-HACK Radio Lab — v1.3-rf-lab.3

An engineering fork of Teapot174/ESP-HACK, upstream commit
`8c57584c6a39fc3ac948ce2277337a5dd2915baf`, for the existing ESP32 DevKit V1,
SH1106, CC1101, four buttons and microSD wiring. License: AGPL-3.0, with the
original third-party license notices retained. Development branch:
`development/ultimate-radio`.

## Start here

The default **Pocket** main menu has category icons, three visible rows,
selection/scroll indicators and SD status. **Настройки → Интерфейс → Главное меню**
offers five styles: Страницы (Pages), Список (List), Wii, DSi and Pocket. Existing application
category IDs and wiring remain unchanged; see [Pocket interface](docs/POCKET_UI.md).
Open **Радио SubGHz** directly for the simple receive/files/chat/settings home;
technical tools and the previous radio menu are under **Дополнительно**.
File Send temporarily applies the file's exact supported frequency and restores
the physical radio afterwards. Simple recording prepares its RSSI threshold
from background noise; see the [Russian recording/file guide](docs/FILE_PLAYBACK_RU.md).
Its independent radio task keeps
display rendering and SD operations outside the GPIO edge ISR.

Open **Приложения SD** from the main menu to run Lua applications from `/apps`.
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
  Each page holds 39 real paths plus a separate **След. страница** row when
  more entries exist. BACK returns to the previous page before the parent
  folder. Hold DOWN/UP to change pages quickly; page and total counts appear
  in the header. Search and folder changes reset the page. Full paths up to
  1023 bytes are resolved from directory identities, with no full-path bank
  per row. Longer paths are visibly refused; UTF-8 names are never truncated
  for opening. Legacy Send uses the same browser.
- **File Send:** **Радио SubGHz → Файлы .sub → file → Отправить** supports
  one binary OOK RAW pass (starts HIGH, at most 2048 intervals / 5 seconds),
  exact `RcSwitch` protocol 1 or `Princeton` at 1..32 bits, or CAME 12/24 (six periods).
  Non-24 Princeton is an ESP-HACK legacy extension, not official Flipper support.
  Exact file frequency and compatible
  OOK configuration are required; unsupported or dynamic protocols are refused.
  CAME's initial LOW is rotated to the period end for finite HIGH-first RMT
  playback. See [supported static playback](docs/STATIC_SUB_PLAYBACK.md).
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

UP/DOWN navigate; OK opens; BACK returns. Text fields show an **8 × 4 keyboard**:
UP/DOWN select a key, hold UP/DOWN changes pages, OK types or activates
SP / <- / >> / OK, BACK deletes, hold OK accepts and hold BACK cancels while
preserving the original field. Numeric/HEX/filename fields retain their own
ASCII alphabets. Accepting nonempty normal-chat text queues a message; cancelling
does not. See the [keyboard guide](docs/KEYBOARD_RU.md).
Release the initiating button before the next screen accepts a new action.
Long informational screens scroll with UP/DOWN. RAW transmission requires a
fresh hold-OK action; BACK aborts RAW playback. Chat uses its manual send menu.

Radio Lab TX defaults off. In Radio Settings select **Shielded laboratory** for
the intended controlled setup, then choose a nominal -10/0/+10 dBm setting.
Opening the simple Chat screen temporarily applies its automatic 433.920 MHz /
Flipper plaintext / nominal 0 dBm TX-ready profile for this authorized setup;
messages still require a manual send action. The simple file Send screen also
temporarily selects ShieldedLab / nominal 0 dBm, for at most 5 seconds, without
requiring a separate settings change. It requires a fresh hold OK; opening a
file does not transmit. Both screens restore the previous settings on exit.
Other transmit tools retain explicit profile selection.
The new ESP32 TX policy allows only the explicit laboratory channels
433.920/868.350 MHz and at most 5 seconds per RAW operation. Chat adds finite
per-packet deadlines, a 100 ms gap and a local 1-second/60-second attempt-time
governor. The Flipper
also applies its installed firmware's TX permission settings. These are bounded
laboratory profiles, not a Ukrainian regulatory certification. PA values and
the antenna/matching network have not been measured. The profile resets to RX
only on each Radio Lab launch; it is not written to SD as a TX authorization.

The user confirmed failed reception in both ESP32/Flipper directions on the
Russian `c5779ee` build. The connected peer is Unleashed `unlshd-086e`
(`e7e4e179`); its backed-up Enhanced Sub-Ghz Chat 1.2 uses API 87.6 and delegates
the radio worker to that firmware. Source/profile checks have not established
the physical cause. The final `087f9b6` image passed controlled plaintext
exchange in both directions between its normal chat UI and the installed
Flipper's internal-radio CLI at nominal 433.920 MHz, with the ESP32's saved
+115 ppm correction. Enhanced Chat's GUI itself was not tested. The supplied ESP ACK companion
FAP built for official 1.5.1/API 88.2 is a separate application.

All eleven Russian SD apps passed 120 diagnostic input/tick/draw frames each on
the final `087f9b6` ESP32 build with radio starts disabled. The earlier `c5779ee` CI run also
passed. The `e23c31d` diagnostic candidate was verified flashed and yielded the
one-way RF result. All three CI jobs passed at `28e4d46`, including full-path
browsing and per-device calibration. All three jobs also passed at `d63f1ca`,
including its carrier-gate change. All four final flash segments verified and
the controlled normal-UI/Flipper-CLI two-way test passed. Earlier
release 1 evidence and exact source states are separated in the test report.
All three CI jobs also passed for the [final `087f9b6` keyboard build](https://github.com/Gegcbr2022/ESP-HACK/actions/runs/39486105314).
All four final flash segments verified. Its viewed OLED screenshot showed the
keyboard grid, highlight/actions, cursor and preview fitting 128 × 64. The USB
editor preview kept TX at zero and cancelled to the main menu without panic.
The repeated controlled two-way CLI and eleven-app tests passed on this image;
`d63f1ca` is earlier historical evidence.

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

Release 2 native checks pass on Windows Zig and Ubuntu GCC: 91,990 Lua checks
with 16,500 sample frames across eleven apps, 758,551 catalog checks and 21,892
menu/rendering/migration checks. These results cover software behavior and
bounds; they do not establish physical OLED/button usability or RF performance.
Additional suites passed 1,066,348 static encoder checks, 1,108,737 pagination
checks, 1,048,643 automatic chat profile/carrier-gate checks and 246 mock clock probe checks.
Actual-browser, editor/UART and calibration suites passed 5,026, 2,429 and
6,730,289 checks respectively; calibration mocks do not establish a measured
correction for every module.
The keyboard model passed 1,460,550 checks under Zig/GCC, with additional
GCC address/undefined-behavior sanitizer validation.
Basic settings and new screens use Russian; remaining advanced legacy screens
may retain English labels. Filenames and protocol identifiers are not translated.

Compiled/native-tested behavior is distinct from physical RF validation. A
successful flash, chip probe or OLED boot cannot establish receiver sensitivity,
accurate RF timing or ESP32 ↔ Flipper interoperability.
The controlled plaintext pair above is an observed exception with narrow
conditions: Enhanced GUI, ESP ACK/FAP, range, RF power and encryption were not tested.
