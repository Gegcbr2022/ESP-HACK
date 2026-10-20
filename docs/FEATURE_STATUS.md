# Feature status and known limits

This records **v1.3-rf-lab.2** for the specified ESP32/SH1106 board. “Built”
means a real target binary linked, not merely a source syntax check. Native
tests exercise portable logic or explicit GPIO/SPI mocks. Neither implies RF
verification. Connected-device backup/flash/boot evidence is recorded separately
in the release's device-operation report, not generalized to all features.
Release 1 hardware results below are historical. The Russian `c5779ee` build
passed its three-job CI run and all eleven installed SD apps passed 120
diagnostic frames each on ESP32, with radio starts disabled. The user confirmed
that physical chat still failed in both directions on that build. A later
verified `e23c31d` image received controlled Flipper-to-ESP32 packets with valid
CRC at an adjusted programmed receive frequency. This is one-way evidence,
not a completed calibrated two-way test. All three CI jobs passed at
`28e4d46` and `d63f1ca`, including the latter's host carrier-gate correction.
The final bidirectional device record remains pending.

| Area | Implemented / compiled | Native validation | Physical status |
|---|---|---|---|
| Original source baseline | Original sources built with pinned compatible platform | Build logs retained | Baseline image not flashed |
| Original GPIO / SH1106 target | Existing mapping retained; accidental NRF remapping removed | Source/build review | Release 1: user confirmed main-menu OLED and buttons; release 2 Pocket interaction pending |
| CC1101 reset / SPI timeouts | Same3.0.2 driver, local bounded fixes, sticky faults, counters | Real driver under deterministic SPI mocks | Release 1 boot probe PASS: PART00 / VERSION14, RX MARC0D, zero SPI/ready timeouts; RF behavior remains unverified |
| Legacy Standard Read | Correct first-frame packing, coherent rc-switch snapshot, no invalid512-entry scan | Packing/protocol guards tested | RF payload reception pending |
| Legacy Read RAW | Interrupt ring replaces UI polling, post-burst RSSI issue fixed, damaged frames rejected | Real ISR code under GPIO/clock mocks | RF timing/drop rate/SD latency pending |
| Exact AM270/AM650/FM238/FM476/FM12K | Pinned official1.5.1 register arrays | Exact bytes, arithmetic and boundaries | Receiver behavior pending |
| Advanced modem settings | OOK/ASK,2FSK,GFSK,4FSK,MSK configuration with combination checks | Valid/invalid settings and register round trips | RF behavior and sensitivity pending |
| Preset storage | Save/load/edit/reset/rename/move/duplicate/delete/import/export via `.sub` metadata | All5 modulation types and host-field persistence | Real SD workflow pending |
| `.sub` compatibility | Streaming headers/custom presets/RAW stats, explicit format errors | Valid fixtures, limits, corruption,20k mutations | Stock Flipper import/readback pending |
| Lab Read / Protocol Analyzer | LAB-OOK24 registry and CRC decoder; developer-defined packet transport | Bit flips, jitter, CRC and generator round trip | Known-waveform RX pending |
| Frequency Analyzer | Current/peak RSSI, carrier/activity indication, CSV | No RF mock for RSSI values | Frequency/RSSI calibration pending |
| Spectrum Analyzer |64 sequential bins, step choices, graph, peak hold, CSV | Channel bounds/parameter checks | Settling time and measured peaks pending |
| RAW Analyzer | Count/duration/min/max/gaps and pulse-index view | Statistics/decoder tests | Visual usability pending |
| Signal Manager |39 lightweight directory identities per page plus a separate Next Page row; page/total header, hold DOWN/UP for pages, BACK previous page before parent; complete UTF-8 paths up to 1023 bytes; metadata/rename/move/duplicate/delete | 1,108,737 pagination/path checks plus 5,026 actual-browser mock checks; directory sizes 0–1000, stale identities, long names and page bounds | Final FAT/SD/button workflow pending on the latest image |
| Static `.sub` Send | RcSwitch (protocol 1, 1..32 bits) / Princeton 1..32 / CAME 12/24, six periods; exact file frequency and binary OOK config, manual hold OK, finite RMT; CAME LOW-header rotation disclosed | 1,066,348 encoder/parser/config checks, period decoding and bounds | Physical waveform/reception pending; KeeLoq/Alutech/StarLine and other dynamic protocols have no encoder |
| Simple RAW file Send | One HIGH-first binary OOK RAW pass, at most 2048 intervals / 5 seconds; temporary lab profile / nominal 0 dBm restored after send/cancel | Parser/staging/config/time bounds; unsupported presets, shaping and FSK refused | Physical reproduction and reception pending |
| Signal Generator | Finite user LAB-OOK24 payload,52 intervals including terminator | Known payload/CRC, receiver-edge simulation | Actual waveform/RMT jitter pending |
| Modulation Explorer | Sequential four-preset RSSI/edge comparison | Preset/config tests | Physical comparison pending; no automatic identification claim |
| Music Lab | RAW inspection and finite binary OOK RMT playback | Parser/staging/timing bounds | ESP→Flipper RAW reception/timing pending |
| ESP ACK chat | Shared protocol,160 ASCII bytes,4fragments, ACK/retry/backoff/discovery/duplicates, retained in Advanced | C99/C++11 loss/duplicate/delay/reboot/wrap/bounds tests | Supplied companion FAP/ESP ACK physical exchange remains unverified; distinguish it from the user's Enhanced plaintext app |
| Simple Flipper plaintext chat | Read/send screen, automatic 433.920 MHz / plaintext / nominal 0 dBm TX-ready setup for the authorized Faraday test; manual sends, finite governor and packet/CRC/FIFO/state diagnostics | 205,361 C99 checks and 205,361 C++11 checks on Windows/Linux; profile/carrier-gate suite 1,048,643 checks; idle UART dispatcher 1,489 checks | BOTH directions failed on `c5779ee`; later verified `e23c31d` received controlled valid-CRC Flipper packets at an adjusted programmed RX frequency; calibrated two-way exchange pending |
| CC1101 reference-clock diagnostic | Finite idle-only GPIO4/PCNT probe, restores radio pin configuration and reports errors; no RF transmit | 246 checks of real probe under hardware mocks, including counter faults, timing and cleanup | Coarse reference-clock evidence is separate from fine RF accuracy; mocks do not measure the module |
| Device-local frequency correction | Bounded signed-ppm correction, band-edge validation and NVS persistence; no global default offset | 6,730,289 arithmetic, persistence and guard checks | Controlled one-way RX offset observed; final saved-correction readback and two-way test pending |
| Flipper FAP | Official1.5.1, API88.2, public exports, independent radio task | Shared protocol tests; actual strict FAP import check | Physical Flipper launch/UI/RF pending |
| New OLED lab UI | Russian friendly receive/files/chat/settings home, with advanced tools and previous menu retained; keyboard, scrollable metadata, release guards | State helpers tested; no complete lab OLED/input simulator | Final device workflow checks pending; source/helper tests do not prove physical RF behavior |
| Pocket main menu / appearance | Default Pocket plus Pages, List, Wii and DSi; same eight categories, icons, counters, scrollbar, SD status and retained settings migration | 21,892 renderer/model/migration checks on Windows Zig and Ubuntu GCC | Release 2 physical OLED/button/style check pending |
| Categorized SD launcher | All apps/Tools/Games/Radio/Favorites/Recent; bounded comment metadata, safe filenames, 32-entry catalog, eight favorites and four recent apps persisted on SD | 758,551 checks, including 10,000 metadata mutations and 10,000 preference operations, filters/path/scroll bounds | Release 2 SD persistence, app info and navigation pending |
| SD Apps / Lua SDK | Bounded VM, Russian drawing/input/clock and receive-only radio bindings; eleven source apps, including four games and four new utilities | Actual Lua VM/sandbox: 91,990 host checks and 16,500 sample frames; meaningful game/math/timer fixtures | All eleven Russian apps PASS 120 diagnostic frames each on `c5779ee`, radio starts disabled; latest candidate's final device check pending |
| Backup / flash utility | Full host read + independent esptool full-flash MD5 verification and saved-file SHA256; optional two-host-read mode; identity/segment gates | Host utility guards tested / reviewed as reported | Connected 4 MiB dump and independent verification succeeded; interrupted second host dump is not counted |

## Explicit incomplete items

- No additional named commercial sensor decoders were implemented. LAB-OOK24
  and the documented chat packets are the new developer formats; rc-switch's
  existing decoders remain available. Unsupported protocol metadata can be
  inspected but is never advertised as decoded or replayable.
  The new static RcSwitch (protocol 1, 1..32 bits) / Princeton 1..32 / CAME 12/24 encoder is an explicit narrow exception:
  it generates supported static keys, not dynamic-code replay or a general
  decoder. Official Flipper Princeton is 24 bits; other supported Princeton
  widths are an ESP-HACK legacy-input extension. Names are matched explicitly,
  without protocol aliases. RAW playback also requires the documented bounded
  binary OOK format. Refusals show the actual Protocol; a channel-policy error
  does not classify a file as dynamic.
- Chat is plaintext, unauthenticated ASCII. Authenticated encryption, UTF-8,
  persistent message history and multi-user collision stress testing remain
  future work. CRC and short boot/session identifiers are not security controls.
- Music Lab does not stream arbitrary-length files: it stages2048 intervals and
  rejects larger playback requests. Inspection streams up to1million pulses.
  Binary OOK RAW TX is supported; async FSK/GFSK RAW TX, shaped multi-level ASK
  TX, speaker note packets, audio codecs and conventional FM audio are absent.
- RMT tick resolution is not measured RF accuracy. The software completion
  delta includes scheduling/polling overhead; use a logic analyzer or scope to
  establish edge jitter and a receiver to establish RF reproduction.
- Advanced4FSK/MSK settings compile and round-trip, but there is no general
  developer packet composer for those modulations. ESP ACK chat uses 2FSK,
  Flipper plaintext uses pinned GFSK, and the waveform generator uses LAB-OOK24.
- Synchronous serial, PN9/infinite test transmissions, hardware FEC and generic
  fixed-length FIFO formats are intentionally outside the supported driver
  subset. Unsupported requests return errors.
- Scan history is the legacy analyzer history plus current/peak sweep data and
  SD exports. This is not a real-time SDR spectrum or calibrated power meter.
- There is no authoritative Ukrainian channel/duty-cycle/ERP database embedded
  in the ESP32. Radio tools retain the shielded-laboratory profile and two
  finite channels. Simple Chat and file Send temporarily apply the user-authorized
  Faraday setup with nominal 0 dBm TX ready; sends remain manual and finite.
  Chat alone adds the 1-second/60-second attempt-time governor. The Flipper
  separately applies its installed firmware's TX permission settings.
  Nominal PA values do not establish measured output or legal compliance.
- The new modules separate radio task, capture, presets, parsing, transport,
  app UI and storage paths. Existing WiFi/BLE/IR/GPIO/games code remains largely
  in its upstream architecture. The entire legacy UI was not rewritten.
- Existing unrelated upstream applications compile, but were not regression
  tested on hardware. This work does not claim those peripherals are installed.
- Noise, supply ripple, antenna mismatch, crystal tolerance, RF frontend quality
  and clone-module differences remain possible hardware causes. Source defects
  are established; a unique physical root cause requires actual measurements.

See `RF_AUDIT.md`, `SUB_COMPATIBILITY.md`, `CHAT_PROTOCOL.md` and the physical
validation checklist for narrower compatibility rules and reproducible tests.

See [Pocket interface](POCKET_UI.md), [SD utilities](SD_UTILITIES.md) and
[SD games](SD_GAMES.md) for release 2 controls and limits.
