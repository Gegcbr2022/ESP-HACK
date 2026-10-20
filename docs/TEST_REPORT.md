# Test report

Recorded for the development branch on **2026-10-20**. These results distinguish
host tests, actual target compilation, and connected-device evidence. A passing
mock does not establish RF sensitivity, pulse accuracy or physical interoperability.

## Build results

| Target / source state | Result | Evidence and limits |
|---|---|---|
| Upstream `8c57584c6a39fc3ac948ce2277337a5dd2915baf`, original unpinned environment | Failed | PlatformIO selected espressif32 8.0.0 / Arduino 3.3.12; the bundled OneWireHub `MEM_SIZE` identifier conflicts with an SDK macro |
| Same original firmware sources, compatible external build configuration | PASS | SH1106 linked using espressif32 6.12.0, Arduino 2.0.17, Xtensa GCC 8.4.0; 88,072 static RAM bytes and 1,475,809 flash bytes reported; source baseline only |
| Integrated RF lab build before the Lua SDK | PASS | SH1106 target linked and full image merged; one checked intermediate reported 115,872 static RAM bytes and 1,531,693 flash bytes; these are not the final Lua firmware's sizes |
| Final ESP32 image including Lua table-operation guards and installer changes | PASS | Release-owner target build linked successfully: 117,384 static RAM bytes, 1,649,241 flash bytes; application binary 1,655,824 bytes. Artifact hashes and device results are recorded by the release owner |
| Flipper chat FAP | PASS | uFBT 0.2.6, official firmware/SDK 1.5.1, target 7 / API 88.2; actual `.fap` produced and strict import check passed |

The official Flipper source reference is
`9e810dc399e027e48ce07751df72168abbb19065`. FAP compilation/import validation is
not a physical Flipper launch or a radio interoperability test. The exact
hardware, pins and rollback procedure are documented separately.

## Native results

The recorded runs used native Windows x64 executables built with official
Zig 0.13.0 (`zig cc` for C99, `zig c++` for C++11). The C++ project harnesses
compile with warnings treated as errors. Lua's vendored C VM is compiled as C,
then linked with the C++ test harness; it is not replaced by a Python emulator.

| Suite | Result | What was exercised |
|---|---|---|
| Portable radio settings and `.sub` support | **20,799 checks PASS** | Exact official presets, all bandwidth/deviation codes, frequency boundaries, invalid combinations, parser bounds/signs/corruption, four synthetic fixtures, five-modulation preset persistence; includes 20,000 deterministic malformed-input mutations |
| Lab limits, FIFO policy and LAB-OOK24 decoder | **9,306 checks PASS** | TX opt-in/channel/duration bounds, synthesized band limits, RMT splitting, IDLE empty/partial recovery, complete FIFO counts 3–63, RX/non-RX deadlines, overflow, CRC bit flips, timing tolerance and 52-interval generator termination |
| Legacy payload / replay guards | PASS | First/changed key length packing, exact OOK preset application, unsupported formats/protocols, payload and timing bounds; this harness does not export a numeric check count |
| Shared chat, C99 | **394 checks PASS** plus **100,000 malformed-frame trials** | Serialization/CRC, bounds, delivery/ACK/retry/duplicates, fragmentation, delayed/lost traffic, boot/session changes and time wrap |
| Shared chat, C++11 | **394 checks PASS** plus **100,000 malformed-frame trials** | Same shared production protocol/session code compiled and executed in C++ |
| Actual vendored CC1101 driver with SPI mocks | **43 checks PASS** | 5 ms ready deadlines, microsecond wrap, task yielding, CS release, sticky error accounting, zeroed failed reads and initialization abort |
| Actual RAW capture engine with GPIO/IRQ/clock mocks | PASS | Signed durations, timer wrap, queue bounds/overflow, reset, GPIO banks and rejected timing; the harness prints a coverage summary rather than a numeric check count |
| Triggered capture policy | **96 checks PASS** | Noise gating, ordered/wrapped prehistory, burst quiet windows, limits and corruption |
| Actual Lua VM + production sandbox | **13,983 checks PASS** | Bounded allocator/coalescing/canaries, 25 VM reopen/close cycles, instruction/wall quotas, source/bytecode/API restrictions, OOM, callback errors and safe numeric/table errors including retained allocations over 75% of the heap; table-operation guards reject giant move ranges and sparse lengths above 32 bits, while ordinary/overlapping moves retain correct results |
| Three Lua source examples | **4,500 host frames PASS**, included in Lua suite | Load/tick/draw/input, printable ASCII and visible 128×64 geometry, clock wrap, receive-only mock API and exit requests |
| Actual app installer with mocked SD/serial/digest adapters | **22 scenarios PASS** | Transfer framing, bounded lengths, integrity failures, cancellation, temporary-file promotion and recovery; digest backend is a control-flow double, while target firmware uses mbedTLS |
| Offline flash artifact validation | **20 unittest cases PASS** | Valid OTA0 layout, required/unique offsets, protected NVS write scope, size caps, SHA256/declared-size mismatch, path escape rejection and actual app0 capacity |

The Lua example peak allocations on the **64-bit host** were 26,008 bytes for
hello, 26,376 for stopwatch and 48,656 of the 49,152-byte VM budget for the RF
dashboard. These are not measured ESP32 heap figures. Native SDK mocks do not
perform GPIO, serial uploads or transmissions.

Run the checked suites from the repository root:

```text
python tests/portable/run_formats_tests.py --compiler <zig.exe>
python tests/portable/run_lab_tests.py --compiler <zig.exe>
python tests/portable/run_legacy_tests.py --compiler <zig.exe>
python tests/run_chat_tests.py --compiler <zig.exe>
python tests/spi_driver_mock/run_spi_tests.py --compiler <zig.exe>
python tests/raw_capture_mock/run.py --cxx <zig.exe> --zig
python tests/triggered_capture/run_triggered_tests.py --compiler <zig.exe>
python tests/lua_sandbox/run.py --compiler <zig.exe>
python -m unittest discover -s tests/host -p test_device_flash.py -v
```

The final affected Lua and legacy suites were rerun after their runner/CI
changes. Full local stdout logs are retained outside the repository; raw device
logs and backup data are not public source artifacts.

## CI configuration

`.github/workflows/ci.yml` configures Ubuntu 24.04 / Python 3.12 jobs for native
tests, the SH1106 firmware and the official 1.5.1 FAP. Native C++ suites use
`g++`; chat explicitly selects C99/C++11 with `-x`; Lua uses `gcc` for the VM's
25 C translation units and `g++` for its harness. The RAW runner accepts `--cxx`
and does not require Zig on Linux. The legacy guard and Lua suites are included.

The configured action release tags were checked against the official
[checkout release](https://github.com/actions/checkout/releases/tag/v7.0.1),
[setup-python release](https://github.com/actions/setup-python/releases/tag/v7.0.0)
and [upload-artifact release](https://github.com/actions/upload-artifact/releases/tag/v7.0.2).
Configuration review and local runs do not constitute a successful GitHub Actions
run. Remote CI status remains pending until a published revision actually runs.

## Connected-device and RF status

The connected device's original **4 MiB full host dump** completed and an
independent full-image `esptool verify_flash` MD5 comparison succeeded; the
saved host file also has a SHA256 receipt. An interrupted second host transfer
is not counted as a second successful independent dump. Private receipt/logs
are retained separately from source/release code.

Final ESP32 flash/readback, boot and device Lua self-test results require the
release owner's final update after the newest build. Physical SD app upload,
OLED/button observation, RF dashboard behavior, Flipper launch, cross-device
chat, known-waveform reception, pulse jitter and calibrated RF/power tests
remain pending unless explicitly supported by later device evidence. Follow
`HARDWARE_VALIDATION.md`; do not infer these outcomes from host tests or SPI
register access.
