# Test report — v1.3-rf-lab.2

Recorded for the development branch on **2026-10-20**. These results distinguish
host tests, actual target compilation, and connected-device evidence. A passing
mock does not establish RF sensitivity, pulse accuracy or physical interoperability.
Release 1 artifact/device evidence is explicitly historical. The Russian
`c5779ee` source state passed CI and eleven-app ESP32 diagnostics, while chat
still failed both physical directions. The later `4d1fd3a` candidate adds file
pagination and finite USB RF/file diagnostics. Its final image identification,
flash/probe and physical results await the release owner and are not inferred
from the earlier build.

## Build results

| Target / source state | Result | Evidence and limits |
|---|---|---|
| Upstream `8c57584c6a39fc3ac948ce2277337a5dd2915baf`, original unpinned environment | Failed | PlatformIO selected espressif32 8.0.0 / Arduino 3.3.12; the bundled OneWireHub `MEM_SIZE` identifier conflicts with an SDK macro |
| Same original firmware sources, compatible external build configuration | PASS | SH1106 linked using espressif32 6.12.0, Arduino 2.0.17, Xtensa GCC 8.4.0; 88,072 static RAM bytes and 1,475,809 flash bytes reported; source baseline only |
| Integrated RF lab build before the Lua SDK | PASS | SH1106 target linked and full image merged; one checked intermediate reported 115,872 static RAM bytes and 1,531,693 flash bytes; these are not the final Lua firmware's sizes |
| Historical release 1 ESP32 image, plaintext chat and five-app diagnostics | PASS | SH1106 linked and full image merged: 117,768 static RAM bytes, 1,651,585 flash bytes (84.0%); application binary 1,658,160 bytes, SHA256 `286d1bd5f7f810df885393c1af66f38202a0f311cf8f051f3630394937e8d1f1` |
| Russian release 2 source `c5779ee`: Pocket/settings, static Send, simple chat and eleven apps | PASS build/CI; device app checks recorded below | Linux SH1106 CI linked and uploaded its firmware; all eleven Russian SD apps passed 120 diagnostic frames each on the ESP32 build. Physical chat still failed both directions. The release owner's image manifest identifies the separately built device binary |
| Later candidate `4d1fd3a`: file pagination, reference-clock probe and finite USB diagnostics | Final device/artifact sign-off pending | The release owner will record matching build result, application size/SHA256, flash verification and actual probe results. Earlier CI/device evidence does not identify or validate this candidate |
| Companion Flipper chat FAP (previous checked source) | PASS | uFBT 0.2.6, official firmware/SDK 1.5.1, target 7 / API 88.2; actual `.fap` produced and strict import check passed |

The official Flipper source reference is
`9e810dc399e027e48ce07751df72168abbb19065`. FAP compilation/import validation is
not a physical Flipper launch or a radio interoperability test. The exact
hardware, pins and rollback procedure are documented separately.

## Native results

The recorded runs used native Windows x64 executables built with official
Zig 0.13.0 (`zig cc` for C99, `zig c++` for C++11), plus Ubuntu GCC/G++ for
the release 2 Lua, catalog and Pocket suites. The C++ project harnesses
compile with warnings treated as errors. Lua's vendored C VM is compiled as C,
then linked with the C++ test harness; it is not replaced by a Python emulator.

| Suite | Result | What was exercised |
|---|---|---|
| Portable radio settings and `.sub` support | **21,000 checks PASS** | Exact official presets, bandwidth/deviation/frequency boundaries, invalid combinations, parser bounds/signs/corruption, UTF-8 BOM/encoding checks and five-modulation preset persistence; includes 20,000 deterministic malformed-input mutations |
| Lab limits, FIFO policy, packet metadata and LAB-OOK24 decoder | **10,593 checks PASS** | TX opt-in/channel/duration bounds, synthesized band limits, RMT splitting, IDLE empty/partial recovery, complete FIFO counts 3–63, RX/non-RX deadlines, overflow, CRC bit flips, signed RSSI/frequency-error decoding, timing tolerance and 52-interval generator termination |
| Automatic Flipper plaintext profile | **61 checks PASS** | Complete register/PA presence, valid GFSK settings, exact shared profile, frequency and host deadline setup; source correctness does not establish RF reception |
| Static `.sub` encoder | **800,843 checks PASS** | Production parser/config/Princeton24/CAME12/24 encoder, six periods, bit order, period decoding, TE/guard handling, CAME LOW-header rotation, duration/capacity/canaries, rejection of dynamic or unsupported metadata and modem shaping |
| Radio file pagination / complete paths | **1,069,109 checks PASS** | 39 actual entries plus a separate NextPage slot; directory sizes 0–1000 across every page, order/coverage, deletion clamp, complete112-byte buffer boundaries and Russian/spaced names; no filesystem hardware in this harness |
| CC1101 reference-clock probe, hardware mocks | **246 checks PASS** | Actual probe source with PCNT/SPI/clock adapters; finite measurements, busy/SPI/counter/timing/overflow faults, pin/register restoration and cleanup; no actual crystal measurement in this harness |
| Legacy payload / replay guards | PASS | First/changed key length packing, exact OOK preset application, unsupported formats/protocols, payload and timing bounds; this harness does not export a numeric check count |
| Shared chat, C99 | **394 checks PASS** plus **100,000 malformed-frame trials** | Serialization/CRC, bounds, delivery/ACK/retry/duplicates, fragmentation, delayed/lost traffic, boot/session changes and time wrap |
| Shared chat, C++11 | **394 checks PASS** plus **100,000 malformed-frame trials** | Same shared production protocol/session code compiled and executed in C++ |
| Flipper plaintext compatibility, C99 and C++11 | **205,361 checks PASS per language**, including **100,000 malformed-stream events per language** | Exact profile arithmetic, 60-byte stream chunks, CRLF/quiet assembly, split ANSI escapes, truncation/rejection, continuous-stream and TX deadlines, wrap, no ACK/retry, late-result rejection; Windows Zig and Ubuntu GCC |
| Actual vendored CC1101 driver with SPI mocks | **43 checks PASS** | 5 ms ready deadlines, microsecond wrap, task yielding, CS release, sticky error accounting, zeroed failed reads and initialization abort |
| Actual RAW capture engine with GPIO/IRQ/clock mocks | PASS | Signed durations, timer wrap, queue bounds/overflow, reset, GPIO banks and rejected timing; the harness prints a coverage summary rather than a numeric check count |
| Triggered capture policy | **96 checks PASS** | Noise gating, ordered/wrapped prehistory, burst quiet windows, limits and corruption |
| Actual Lua VM + production sandbox | **91,990 checks PASS** | Bounded allocator/coalescing/canaries, 25 VM reopen/close cycles, instruction/wall quotas, separate 10ms draw / 5ms logic deadlines across microsecond wrap and both hook/return checks, source/bytecode/API restrictions, OOM, callback errors and safe numeric/table errors including retained allocations over 75% of the heap; table-operation guards reject giant move ranges and sparse lengths above 32 bits, while ordinary/overlapping moves retain correct results |
| Eleven Lua source apps | **16,500 host frames PASS**, included in Lua suite | 1,500 frames per app; drawing/input/clock/heap/quota bounds; Snake and 2048 behavior, Block Fall bag/collision/line scoring/top-out, Minesweeper placement/flood/flags/win/loss, utility arithmetic/conversion/resistor/timer edges |
| Bounded app catalog | **758,551 checks PASS** | Metadata/defaults, safe filenames, category filters, eight favorites/four recent apps, scroll bounds including zero/one-row views; includes 10,000 metadata mutations and 10,000 preference operations |
| Pocket / five main menu styles | **21,892 checks PASS** | Production renderers under framebuffer adapters, known Russian glyph pixels/UTF-8 bounds, basic Settings and both submenu renderers, all eight categories, legacy SubGHz Chat submenu and EEPROM/SD appearance migration; 14 native PGM snapshots |
| Actual app installer with mocked SD/serial/digest adapters | **22 scenarios PASS** | Transfer framing, bounded lengths, integrity failures, cancellation, temporary-file promotion and recovery; digest backend is a control-flow double, while target firmware uses mbedTLS |
| Offline flash artifact validation | **20 unittest cases PASS** | Valid OTA0 layout, required/unique offsets, protected NVS write scope, size caps, SHA256/declared-size mismatch, path escape rejection and actual app0 capacity |

One checked **64-bit GCC host** sample run reported these peaks, all within
the 49,152-byte VM bank:

| App | Peak bytes | App | Peak bytes |
|---|---:|---|---:|
| hello | 26,016 | stopwatch | 26,328 |
| RF dashboard | 48,656 | Snake | 33,792 |
| 2048 | 40,840 | countdown | 28,856 |
| calculator | 30,728 | unit converter | 31,584 |
| resistor codes | 31,160 | Block Fall | 40,936 |
| Minesweeper | 40,720 | | |

These are host allocations, not ESP32 heap measurements. Native adapters do
not perform GPIO, serial uploads or transmissions. Additional game fixtures
compile assertion chunks outside the app source. CI exposed accumulation of
those temporary chunks; a test-only cleanup now collects previous fixture
chunks before compiling another. Production VM limits and the 16,500 normal-GC
app frames are unchanged. After the fix, twenty independent GCC processes all
passed (330,000 sample frames total), preserving the 48 KiB bank.

Run the checked suites from the repository root:

```text
python tests/portable/run_formats_tests.py --compiler <zig.exe>
python tests/portable/run_lab_tests.py --compiler <zig.exe>
python tests/portable/run_chat_profile_tests.py --compiler <zig.exe>
python tests/portable/run_static_sub_tests.py --compiler <zig.exe>
python tests/portable/run_file_page_tests.py --compiler <zig.exe>
python tests/portable/run_legacy_tests.py --compiler <zig.exe>
python tests/portable/run_app_catalog_tests.py --compiler <zig.exe>
python tests/pocket_menu/run.py --compiler <zig.exe>
python tests/run_chat_tests.py --compiler <zig.exe>
python tests/flipper_chat_compat/run.py --compiler <zig.exe>
python tests/spi_driver_mock/run_spi_tests.py --compiler <zig.exe>
python tests/rf_clock_mock/run.py --compiler <zig.exe>
python tests/raw_capture_mock/run.py --cxx <zig.exe> --zig
python tests/triggered_capture/run_triggered_tests.py --compiler <zig.exe>
python tests/lua_sandbox/run.py --compiler <zig.exe>
python tests/app_install_mock/run.py --cxx <zig.exe> --zig
python -m unittest discover -s tests/host -p test_device_flash.py -v
```

The release 2 Lua, catalog, Pocket, static encoder, pagination and clock-probe
suites passed on Windows Zig and Ubuntu GCC. Latest recorded parser/profile
counts also passed in the `c5779ee` Ubuntu CI run. Full local stdout logs are retained outside the repository; raw device
logs and backup data are not public source artifacts.

## CI configuration

`.github/workflows/ci.yml` configures Ubuntu 24.04 / Python 3.12 jobs for native
tests, the SH1106 firmware and the official 1.5.1 FAP. Native C++ suites use
`g++`; chat explicitly selects C99/C++11 with `-x`; Lua uses `gcc` for the VM's
25 C translation units and `g++` for its harness. The RAW runner accepts `--cxx`
and does not require Zig on Linux. The native job includes all eleven Lua apps,
app catalog, Pocket appearance/migration, parser/static encoders, chat profile,
file pagination, clock-probe mocks, legacy guards and installer checks.

The configured action release tags were checked against the official
[checkout release](https://github.com/actions/checkout/releases/tag/v7.0.1),
[setup-python release](https://github.com/actions/setup-python/releases/tag/v7.0.0)
and [upload-artifact release](https://github.com/actions/upload-artifact/releases/tag/v7.0.2).
The [historical release 1 GitHub Actions run](https://github.com/Gegcbr2022/ESP-HACK/actions/runs/39468658074)
passed all three jobs at runtime-source commit
`8d47538fce27a59d8b395c3f2618e354f5c05817`, including its five-app Lua suite,
plaintext compatibility tests and uploaded ESP32/FAP artifacts. It is not the
release 2 CI result.

The [Russian release 2 CI run](https://github.com/Gegcbr2022/ESP-HACK/actions/runs/39477650272)
passed **native, ESP32 SH1106 and Flipper** at exact source
`c5779ee2f6c50ebeb8cbe183697a06b03a4824c2`. Both ESP32 and Flipper artifacts
uploaded successfully. It includes the Russian UI/apps, static encoder and
automatic profile suites, after the host-only game-fixture cleanup. This run
predates pagination and the clock-probe diagnostics in `4d1fd3a`; a new run is
needed to cover those changes. Their local native tests are recorded above.
The supplied firmware's manifest/SHA256 identifies its own Windows build;
GitHub Actions builds independently on Linux.

## Connected-device and RF status

The connected device's original **4 MiB full host dump** completed and an
independent full-image `esptool verify_flash` MD5 comparison succeeded; the
saved host file also has a SHA256 receipt. An interrupted second host transfer
is not counted as a second successful independent dump. Private receipt/logs
are retained separately from source/release code.

### Historical release 1 hardware evidence

The release 1 SDK image was written to the attached ESP32 and all four segments
passed explicit esptool verification. The boot probe reported PART=00 /
VERSION=14, MARCSTATE=0D, zero SPI errors/ready timeouts, SD mounted and SH1106 UI
ready with 121,544 free heap bytes. The user confirmed that the OLED menu and
buttons work during the preceding SDK build. The release 1 application SHA256 is
`286d1bd5f7f810df885393c1af66f38202a0f311cf8f051f3630394937e8d1f1`.

All five example apps were physically installed over USB with matching
closed-file SD readback SHA256 confirmations. On-device `appsdiag` passed the
real Lua VM, runaway instruction quota and each source's load/tick/draw check.
ESP32 peaks were 14,344 bytes for the self-test, 16,784 for hello, 17,936 for
stopwatch, 20,832 for the RF dashboard, 23,464 for Snake and 25,024 for 2048,
within the 49,152-byte VM bank. Final diagnostics restored the main menu.
Radio start is disabled during that diagnostic. A UART interruption recovered
after USB reconnection; results were recorded after reconnection.

### Russian release 2 evidence at `c5779ee`

All **eleven installed Russian SD apps passed 120 diagnostic input/tick/draw
frames each on ESP32** with radio starts disabled, followed by return to the
main menu. These are real VM/app checks rather than host simulation. They do
not exercise RF dashboard reception, every interactive game state, favorite
persistence or every physical button workflow. Native tests cover software
bounds separately; hardware claims remain narrower.

The user confirmed **ESP32-to-Flipper and Flipper-to-ESP32 chat both still
failed on `c5779ee`**, despite local TX completion and register/SPI diagnostics.
The connected Flipper CLI identified `release-cfw unlshd-086e`, commit
`e7e4e179`, built 08-03-2026. Its backed-up Enhanced Sub-Ghz Chat binary is
app1.2/API87.6. Binary review confirms a plaintext CRLF path and firmware-worker
imports; the exact installed firmware's
[worker](https://github.com/DarkFlippers/unleashed-firmware/blob/e7e4e179/lib/subghz/subghz_tx_rx_worker.c)
selects the expected GFSK9.99 profile. This narrows a source comparison and does not prove RF compatibility
or identify a unique physical cause. The supplied official1.5.1/API88.2 ESP ACK
companion FAP is distinct from this Enhanced app.

### Later candidate `4d1fd3a`: pending final device record

The release owner will append the candidate's application size/hash, matched
build result, flash verification, boot state, paginated SD workflow and actual
reference-clock probe. The 246 clock tests above use mocks and must not be
reported as a measured 26 MHz crystal. Earlier `c5779ee` app/CI results do not
validate the later image. No successful physical chat exchange, known-waveform
reception, RF edge-jitter measurement or calibrated RF/power result is recorded
here. Follow `HARDWARE_VALIDATION.md` and retain failed attempts.

See [Pocket UI](POCKET_UI.md), [SD utilities](SD_UTILITIES.md) and
[SD games](SD_GAMES.md) for controls and limits.
