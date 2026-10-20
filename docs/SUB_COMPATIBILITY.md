# Flipper `.sub` compatibility

The bounded parser follows the official [Flipper SubGhz file format](https://developer.flipper.net/flipperzero/doxygen/subghz_file_format.html), together with the exact 1.5.1 presets documented in [RADIO_PRESETS.md](RADIO_PRESETS.md). Files are parsed a byte at a time; RAW timings do not accumulate in a heap buffer.

| Format/feature | Inspection | Export | Playback/decoding |
|---|---|---|---|
| Version 1 RAW + AM270/AM650 | Frequency, preset, all timings and statistics | Compatible RAW | Radio application's bounded test policy/timing backend required |
| Version 1 RAW + FM238/FM476/FM12K | Same; exact 2-FSK configuration resolved | Compatible RAW | Async FSK timing backend required; not audio FM reception |
| RAW + validated CC1101 custom async ASK/OOK/2-FSK/GFSK | Register/PA image and statistics | Compatible custom RAW | Requires exact supported radio configuration and TX policy |
| Validated custom FIFO preset with RAW timings | Metadata and statistics; `rawCompatible=false` | Rejected by RAW writer | Unsupported RAW playback |
| Version 1 Key + a known preset | Protocol name, `Bit`, `Key` up to 8 bytes, `TE` | Not implemented in this module | Metadata only; no automatic replay or universal protocol decoder |
| Unknown Key protocol | Protocol metadata retained | Not implemented | No decoder implied |
| ESPHackPreset Key metadata | Custom register image and host RSSI/deadlines | `writePresetMetadata()` supports FIFO and async settings | Configuration storage only; not a generic Flipper protocol decoder |
| Unknown preset/module | Clear unsupported error | Rejected | No substitution |
| BIN_RAW, other versions, unsupported packet formats | Clear unsupported error | Rejected | Not implemented |

`FuriHalSubGhzPresetCustom` requires `Custom_preset_module: CC1101` and bounded hex bytes. The structure is register/value pairs, `00 00`, then **exactly eight** PA bytes. See the validation rules in RADIO_PRESETS.md. Parsing a custom preset does not authorize or calibrate its power setting.

The RAW syntax requires nonzero signed 32-bit microsecond values, first pulse positive, then strict sign alternation **across line boundaries**. Each RAW_Data line has at most 512 values. The parser conservatively rejects -2147483648 and uses ±2147483647 as its maximum pulse duration, avoiding signed absolute-value overflow. The default maximum is one million pulses and is configurable downwards by the caller. Metadata values are limited to 399 characters, field names to 39, protocol/preset names to 63. CRLF and LF are accepted; embedded CR/control bytes are rejected. Metadata must precede RAW data. Duplicate metadata is rejected rather than interpreted inconsistently by different components.

`RawStatistics` reports pulse count, high/low durations, minimum/maximum duration, values shorter than 10 us, gaps at least 10000 us, total duration, and the memory a full int32 buffer would require. These are waveform timing statistics, not a claim to identify modulation or an audio codec. A gap counter is a threshold-based segmentation aid, not a protocol boundary detector.

`RawWriter` emits the canonical official preset name, 16 values per line, LF terminators, and the original timing signs. Custom export serializes only present registers, terminator, and eight PA bytes. Writer errors are sticky; close/delete an incomplete temporary file if its sink fails.

`writePresetMetadata()` saves a custom configuration as a Key metadata file with `Protocol: ESPHackPreset`, avoiding artificial RAW pulses and allowing FIFO/MSK/4-FSK preset storage. This developer-defined protocol is for the ESP-HACK preset manager; stock Flipper has no decoder for it. `settingsFromSub()` restores the radio configuration and optional bounded `X-ESP-RSSI`, `X-ESP-RX-timeout`, and `X-ESP-TX-limit` fields. Extensions must precede any RAW timing data. Missing extensions use defaults. Power/TX profile is deliberately not restored from an untrusted file.

## Caller obligations

Call `finish()` and require success before treating a file as validated. A pulse sink can receive timing records before a later error is discovered, so it must stage them for analysis/storage. **Never connect a first-pass parser callback directly to a transmitter.** Validate the complete file first and use a separate bounded playback pass, with its own duration limit, manual stop, and storage error handling. An SD file can change between passes; the application should lock it or compare its digest/size/metadata and revalidate while replaying, terminating immediately on any mismatch. Do not transmit arbitrary unknown protocol files merely because a header parsed.

The included fixtures are synthetic laboratory waveforms/metadata created for this project. They are not captured access-control signals. Official documentation has contradictory illustrative RAW examples with consecutive positive values; the implementation deliberately follows its stated first-positive/alternation rule and rejects those malformed examples.

## Automated verification

Run `python tests/portable/run_formats_tests.py --compiler PATH_TO_NATIVE_COMPILER`. GCC, Clang, and `zig c++` are supported. ESP32 cross compilation alone does not execute these tests. The suite covers corrupted metadata, numeric overflow, wrong first sign, global alternation, missing/custom presets, limits, sink failures, export round trips, all discrete modem choices, preset/host setting persistence for five modulations, and deterministic malformed-input mutations. `run_lab_tests.py` additionally exercises the actual laboratory timing decoder against CRC corruption and jitter, RMT splitting edge cases, TX profile limits, and application transitions.

No on-air compatibility, timing fidelity, sensitivity, SD throughput, or legal compliance is established by these host tests. Test with matching presets in the documented controlled physical procedure and retain both devices' logs.
