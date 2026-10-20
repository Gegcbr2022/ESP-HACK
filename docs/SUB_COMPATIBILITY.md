# Flipper `.sub` compatibility

The bounded parser follows the official [Flipper SubGhz file format](https://developer.flipper.net/flipperzero/doxygen/subghz_file_format.html), together with the exact 1.5.1 presets documented in [RADIO_PRESETS.md](RADIO_PRESETS.md). Files are parsed a byte at a time; RAW timings do not accumulate in a heap buffer.

| Format/feature | Inspection | Export | Playback/decoding |
|---|---|---|---|
| Version 1 RAW + AM270/AM650 | Frequency, preset, all timings and statistics | Compatible RAW | One finite OOK playback when within the file-Send limits below |
| Version 1 RAW + FM238/FM476/FM12K | Same; exact 2-FSK configuration resolved | Compatible RAW | Inspection/export only; current file Send does not play FSK RAW |
| RAW + validated CC1101 custom async ASK/OOK/2-FSK/GFSK | Register/PA image and statistics | Compatible custom RAW | Only binary async OOK passing the stricter file-Send register/PA checks can play |
| Validated custom FIFO preset with RAW timings | Metadata and statistics; `rawCompatible=false` | Rejected by RAW writer | Unsupported RAW playback |
| Version 1 Key: exact RcSwitch/Princeton1..32 or CAME12/24 + compatible OOK preset | Protocol, bit count, key bytes, timing metadata | Key export not implemented in this module | Six finite generated periods; RcSwitch means protocol1 only; non24 Princeton is an ESP-HACK legacy-input extension, not official Flipper support; [exact subset, timing and phase](STATIC_SUB_PLAYBACK.md) |
| Other Version 1 Key + a known preset | Protocol name, `Bit`, `Key` up to 8 bytes, `TE`, optional guard/CRC metadata | Not implemented in this module | Metadata only; no universal protocol decoder |
| Alutech AT-4N: 72 bits, 8-byte `Key` and separate 8-bit `CRC` | Extended metadata accepted | Not implemented | Dynamic protocol remains read-only; no encoder |
| Unknown Key protocol | Protocol metadata retained | Not implemented | No decoder implied |
| ESPHackPreset Key metadata | Custom register image and host RSSI/deadlines | `writePresetMetadata()` supports FIFO and async settings | Configuration storage only; not a generic Flipper protocol decoder |
| Unknown preset/module | Clear unsupported error | Rejected | No substitution |
| BIN_RAW encoding, other file types or versions | Unsupported metadata/header/version error | Rejected | Not implemented |

`FuriHalSubGhzPresetCustom` requires `Custom_preset_module: CC1101` and bounded hex bytes. The structure is register/value pairs, `00 00`, then **exactly eight** PA bytes. See the validation rules in RADIO_PRESETS.md. Parsing a custom preset does not authorize or calibrate its power setting.

The RAW syntax requires nonzero signed 32-bit microsecond values, first pulse positive, then strict sign alternation **across line boundaries**. Each RAW_Data line has at most 512 values. The parser conservatively rejects -2147483648 and uses ±2147483647 as its maximum pulse duration, avoiding signed absolute-value overflow. The default inspection maximum is one million pulses; the caller can set a smaller limit. Metadata values are limited to 399 bytes, field names to 39 bytes, protocol/preset names to 63 bytes. CRLF and LF are accepted; embedded CR/control bytes are rejected. Metadata must precede RAW data. Duplicate recognized metadata is rejected rather than interpreted inconsistently by different components.

Save files as ASCII or UTF-8; a leading UTF-8 BOM is accepted. UTF-16/32 files must be converted to UTF-8 rather than renamed. File recognition accepts `.sub`, `.SUB` and mixed-case suffixes. These encoding/suffix rules do not add support for a protocol or an out-of-band frequency. The file browser retains complete paths up to 111 bytes; longer paths are skipped and counted on screen. A Cyrillic filename consumes more UTF-8 bytes than its glyph count.

## Inspection versus finite file Send

The UI streams inspection/statistics through the parser up to its one-million-pulse limit, while retaining at most **2048 intervals** for playback. A valid longer recording can therefore be inspected but cannot be sent. File Send additionally requires **no more than 5 seconds**, binary async OOK, valid packet/PA settings, and an original file frequency of exactly **433.920 or 868.350 MHz**. It never substitutes a permitted frequency for a different file frequency. RcSwitch and Princeton accept 1..32 bits; CAME accepts 12 or 24. Only the exact names are accepted, with no unknown-protocol aliases. RcSwitch retains protocol1's fixed31TE sync; Princeton retains `Guard_time`15..72/default30. Official Flipper Princeton is24-bit; other Princeton lengths preserve ESP-HACK legacy accepted input only. KeeLoq, Alutech, StarLine and other unimplemented protocols remain metadata-only.

Open **Радио SubGHz → Файлы .sub**, select a file, then choose **Отправить** or **О файле**. Send requires held OK for one attempt; BACK cancels/stops. In the user's agreed shielded test, file Send temporarily chooses ShieldedLab at nominal 0 dBm, then restores previous radio settings. RAW plays once; supported static keys generate six periods regardless of the file's `Repeat`. File Send has per-operation profile/channel/buffer/duration guards, **not** the chat worker's cumulative 1-second/60-second airtime budget. No physical reception or RF accuracy is established by a local completion message.

`RawStatistics` reports pulse count, high/low durations, minimum/maximum duration, values shorter than 10 us, gaps at least 10000 us, total duration, and the memory a full int32 buffer would require. These are waveform timing statistics, not a claim to identify modulation or an audio codec. A gap counter is a threshold-based segmentation aid, not a protocol boundary detector.

`RawWriter` emits the canonical official preset name, 16 values per line, LF terminators, and the original timing signs. Custom export serializes only present registers, terminator, and eight PA bytes. Writer errors are sticky; close/delete an incomplete temporary file if its sink fails.

`writePresetMetadata()` saves a custom configuration as a Key metadata file with `Protocol: ESPHackPreset`, avoiding artificial RAW pulses and allowing FIFO/MSK/4-FSK preset storage. This developer-defined protocol is for the ESP-HACK preset manager; stock Flipper has no decoder for it. `settingsFromSub()` restores the radio configuration and optional bounded `X-ESP-RSSI`, `X-ESP-RX-timeout`, and `X-ESP-TX-limit` fields. Extensions must precede any RAW timing data. Missing extensions use defaults. Power/TX profile is deliberately not restored from an untrusted file.

## Caller obligations

Call `finish()` and require success before treating a file as validated. A pulse sink can receive timing records before a later error is discovered, so it must stage them for analysis/storage. **Never connect a first-pass parser callback directly to a transmitter.** The current file Send stages a complete bounded waveform in RAM and sends that validated bank without rereading the SD file during transmission. A future two-pass streaming backend would need its own duration limit, manual stop and storage error handling, plus file locking or digest/size/metadata comparison and revalidation against changes between passes. Do not transmit arbitrary unknown protocol files merely because a header parsed.

The included fixtures are synthetic laboratory waveforms/metadata created for this project. They are not captured access-control signals. Official documentation has contradictory illustrative RAW examples with consecutive positive values; the implementation deliberately follows its stated first-positive/alternation rule and rejects those malformed examples.

## Automated verification

Run `python tests/portable/run_formats_tests.py --compiler PATH_TO_NATIVE_COMPILER`. GCC, Clang, and `zig c++` are supported. ESP32 cross compilation alone does not execute these tests. The suite covers corrupted metadata, numeric overflow, wrong first sign, global alternation, missing/custom presets, limits, sink failures, export round trips, all discrete modem choices, preset/host setting persistence for five modulations, and deterministic malformed-input mutations. `run_lab_tests.py` additionally exercises the actual laboratory timing decoder against CRC corruption and jitter, RMT splitting edge cases, TX profile limits, and application transitions.

No on-air compatibility, timing fidelity, sensitivity, SD throughput, or legal compliance is established by these host tests. Test with matching presets in the documented controlled physical procedure and retain both devices' logs.
