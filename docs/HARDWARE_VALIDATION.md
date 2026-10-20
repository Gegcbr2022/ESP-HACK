# Physical validation procedures and evidence

**RF interoperability, antenna suitability, calibration and timing measurements
are pending.** The procedures below are instructions, not reports of tests that
already passed. Backup/flashing activity is recorded separately by the release
process; no successful backup, flash or boot is asserted here. Host mocks and
successful target compilation cannot establish successful reception on this
assembled device.

## Test record and logging

Record the firmware commit/binary SHA-256, ESP32 module/CC1101/antenna markings,
Flipper firmware version, FAP build, SD card identification, selected frequency,
presets, nominal power, separation/orientation and test conditions. The hardware
is the existing FaradayUkraine-sourced ESP32/CC1101 device; its exact antenna and
RF matching-network band remain unverified. A vendor name is not evidence of a
Faraday enclosure or calibrated shielding.

Use the existing ESP32 and genuine Flipper. A calibrated instrument is optional
for functional exchange but would be needed for an absolute power/frequency
accuracy claim. Without measurements, leave those claims pending rather than
requiring another component purchase to begin.

For ESP32, open the enumerated USB serial port at **115200, 8-N-1**, with local
timestamped file logging. Keep the port exclusive while logging; do not run a
flasher/backup tool on that port at the same time. Capture from boot through the
test and preserve complete `[CC1101]`, `[RAW]`, `[SUB]` and `[RadioLab]` lines.
Do not assume that the COM number stays constant after reconnection.

For the Flipper FAP, connect the Flipper USB CLI and run **`log info`** before
launching the app. Keep the terminal's log capture active while operating the
Flipper buttons. Preserve lines with tag **`EspChat`**, including RX profile,
packet TX outcome/duration and complete-message RX entries. Ctrl+C stops the
logging command. `log debug` is available if needed, but more logging can affect
timing. No developer board is required for USB CLI logs. The command is defined
in the [official 1.5.1 CLI source](https://github.com/flipperdevices/flipperzero-firmware/blob/1.5.1/applications/services/cli/cli_main_commands.c).

All transmissions below require permission for the channel, equipment and test
conditions. Keep ESP32 **TX profile → Receive only** for receive tests. The
`Shielded laboratory` choice is an opt-in software limit, not regional or
shielding certification. Use normal Flipper region restrictions; do not bypass
them. Neither device's PA labels are a power calibration.

In Radio Lab, Up/Down selects a list item and OK opens/changes it. In a text or
numeric editor, Up/Down selects a character, short OK appends it, short Back
deletes the last character, hold OK accepts and hold Back cancels. Existing
digits are not automatically replaced: verify the displayed value before
accepting. The default frequency is already 433920000 Hz.

## OLED and SD bring-up

1. If boot stops at `SD Card init failed.`, select the left on-screen **OK**
   (the skip action), press/release physical OK, and wait for the main menu.
   Up/Down toggles between OK and Retry. See [HARDWARE.md](HARDWARE.md) for the
   exact dialog behavior.
2. On ESP32 select **SubGHz → Radio Lab → CC1101 Diagnostics**. Up/Down changes
   diagnostic pages; OK requests radio recovery; Back exits.
3. Save all diagnostic pages and serial output. PARTNUM is normally 00;
   VERSION must not be 00/FF. An active receiver should settle in MARCSTATE 0D.
   Record actual values rather than substituting those expectations.
4. Record SPI ready/readback errors, FIFO errors and timeouts. Repeat after
   entering/exiting **Radio Settings**, **Signal Manager** and **Read RAW**.
   A responsive chip does not prove antenna/receiver sensitivity.
5. For SD testing, use a new, uniquely named laboratory file. Save a capture,
   open **Radio Lab → Signal Manager**, inspect it, and duplicate/export that
   test file. Read both copies on a computer and compare bytes/hashes. Preserve
   original SD files. Test storage failures only with disposable test data;
   document any missing card or failed save explicitly.

## Reference waveform: LAB-OOK24

The laboratory encoder/decoder is `src/research/test_decoder.h`. It carries a
16-bit test payload followed by CRC-8/SMBUS (poly 07, init 00), MSB first.
Payload **1234 hex** has CRC **F1**, so the 24 transmitted bits are **1234F1**.
The complete test waveform has **52 signed intervals** and nominal duration
**31,700 us**: preamble `+1000,-5000`, 24 symbols, trailing `+300,-5000` guard.
Zero uses `+300,-300`; one uses `+900,-300`. The guard's rising edge lets an
edge-driven receiver observe the final LOW data interval.

Create a text file named `LAB_OOK24_AM270.sub` on the Flipper SD under `/subghz/`
(shown as `/ext/subghz/` by device APIs), with the following complete contents.
The 433.920 MHz frequency is only a test option; do not transmit it unless the
channel and installed antenna/test setup are appropriate.

```text
Filetype: Flipper SubGhz RAW File
Version: 1
Frequency: 433920000
Preset: FuriHalSubGhzPresetOok270Async
Protocol: RAW
RAW_Data: 1000 -5000 300 -300 300 -300 300 -300 900 -300 300 -300 300 -300 900 -300 300 -300 300 -300 300 -300 900 -300 900 -300 300 -300 900 -300 300 -300 300 -300 900 -300 900 -300 900 -300 900 -300 300 -300 300 -300 300 -300 900 -300 300 -5000
```

This is a public developer test waveform, not an access-control recording. The
short square-wave fixture `tests/fixtures/lab_ook.sub` is a separate parser
fixture and is not a LAB-OOK24 message.

### Flipper to ESP32, AM270 then AM650

1. On ESP32 open **SubGHz → Radio Lab → Radio Settings**. Select **Preset →
   AM270**, set **Frequency Hz → 433920000**, and leave **TX profile → Receive
   only**. Do not separately change modulation/bandwidth after loading the preset.
2. Prepare Flipper **Main Menu → Sub-GHz → Saved → LAB_OOK24_AM270** and its
   emulate/send view before starting the ESP32 capture.
3. On ESP32, back to Radio Lab and open **Read laboratory**. The screen title
   is `Protocol Read`; it listens for LAB-OOK24. For timing-only capture use
   **Read RAW**. Immediately make one short permitted send on Flipper. Record
   the actual number of transmissions rather than assuming a repeat count.
   The [official saved-signal guide](https://docs.flipper.net/zero/sub-ghz/read)
   describes the Emulate/Send controls.
4. Expect `LAB-OOK24: 1234 CRC OK` only if the decoder actually finds a valid
   complete frame. On ESP32, **OK** stops/saves; **Back** exits. Retain the saved
   `.sub` file, capture counters, log and the exact Flipper source file.
5. Stop/save promptly and restart capture for each of ten individually logged
   attempts. Background OOK noise can fill the bounded 2048-interval staging
   buffer; a full/damaged capture is rejected rather than saved. If the receiver
   reports a timeout/full buffer before the test, record that attempt as invalid
   and restart. Then make a second reference file
   with only `Preset` changed to `FuriHalSubGhzPresetOok650Async`, select **AM650**
   on ESP32, and repeat. These are different full register profiles; changing
   only an RX bandwidth label is insufficient.
6. Test initially with moderate device separation/orientation, then record each
   change. Very close strong signals can overload a receiver. Failure at one
   separation is not evidence of a software timing defect.

Do not claim an exact pulse count from a noisy capture without locating the
known preamble/payload/guard. Compare that segment against the 52 reference
intervals. Report median/worst absolute interval error, total-duration error,
drops, rejected samples and high-water mark. Initial partial/LOW idle intervals
are deliberately discarded by capture. Capture the final guard before stopping.

### FSK preset reception: receive-only validation

On ESP32 use **Radio Settings → Preset → FM238** or **FM476**, leave TX disabled,
then **Frequency Analyzer**, **Read RAW** or **Modulation Explorer**. On Flipper
use **Sub-GHz → Read RAW → Config**, selecting the same channel and FM238/FM476;
both radios remain receivers. Confirm settings/status and log activity from an
already available authorized test source, if present. Without such a source,
record configuration-only evidence; signal reception remains pending.

FM238 is 2-FSK with 2.380371 kHz deviation; FM476 uses 47.60742 kHz deviation.
Both official profiles have approximately 270.833 kHz RX bandwidth. LAB-OOK24 is
an OOK decoder; it is not an FSK protocol test. RSSI or edge activity alone cannot
prove the modulation, payload or successful decoding. Do not reinterpret the
OOK waveform as an FSK interoperability result.

### ESP32 to Flipper, finite OOK RAW

Copy the same laboratory `.sub` to ESP32 SD `/subghz/`. On Flipper select
**Sub-GHz → Read RAW → Config**, the matching frequency and AM270/AM650, return
and start recording. On ESP32 select **Radio Lab → Radio Settings**, opt in to
**TX profile → Shielded laboratory** only for an authorized test, choose the
lowest nominal power and a **TX limit ms** within 1000/3000/5000. Then open
**Radio Lab → Music Lab**, select the file, inspect metadata and hold OK for the
finite playback. **Back** is manual stop. The legacy RAW Send action routes to
the same bounded file-based path.

Save the Flipper capture and compare the known segment. Record ESP32 wall time
and completion delta separately from per-edge jitter: a close completion time
does not prove low jitter. The ESP32 live **Signal Generator** emits one
LAB-OOK24 frame using AM270; do not expect changing the settings preset to make
that generator emit AM650. Use the corresponding file in Music Lab for the AM650
test. No conventional FM audio reception or note playback is implied by RAW.

## ESP32 ↔ Flipper chat

The FAP targets official Flipper **1.5.1 / API 88.2**. Install the release FAP
under `/ext/apps/Sub-GHz/` on a compatible Flipper; a successful build is not
evidence that it has been installed or launched. Use `shared/chat_radio.h` and
[CHAT_PROTOCOL.md](CHAT_PROTOCOL.md), not AM270/FM238, as the chat source of truth.

| Matching chat setting | Value |
| --- | --- |
| First test channel | 433920000 Hz, only when appropriate/permitted |
| Modulation / rate | 2-FSK / 38383.48389 bit/s |
| Deviation / RX bandwidth | 19042.96875 Hz / 101562.5 Hz |
| Sync / preamble | D391, 30/32-bit qualifier / four bytes |
| Packet mode | Variable length, maximum 60 data bytes |
| Whitening / hardware CRC | On / on, bad CRC autoflush |
| GDO0 / completion | Packet sync/end / IDLE then explicit rearm |

1. ESP32: **SubGHz → Radio Lab → Radio Settings → Frequency Hz → 433920000**.
   Chat loads the shared packet profile; do not try to match it by selecting an
   asynchronous FM preset. Begin with TX disabled on both devices.
2. Flipper: open the Sub-GHz Chat FAP. **Down** opens RF settings. Select
   **433.920 MHz**, then the lowest nominal power. Changing Flipper frequency or
   power resets its TX opt-in.
3. When permitted, enable ESP32 **TX profile → Shielded laboratory** and Flipper
   **Enable permitted lab TX**. Both devices need TX permission to exchange ACKs.
   If either remains receive-only, lack of delivery confirmation is expected.
4. ESP32: **Radio Lab → Sub-GHz Chat → Discover devices**. Flipper: **Up** opens
   peers/discovery, then **Send discovery (TX opt-in)**. Select the other device's
   ID on each. Record both IDs and ensure they differ.
5. Send unique 1-, 40-, 41- and 160-byte printable ASCII messages in each
   direction. On ESP32 use **Compose message**, then **Inbox / sent history** and
   **Delivery status**. Flipper **OK** composes; Left/Right selects history and
   hold OK opens the complete message. Compare exact text, source ID, duplicate
   count, delivered/failed status and retry counts, not just an RSSI indication.
6. Disable TX on one device and repeat to observe ACK failure; restore explicit
   opt-in and retry. Reboot one device and repeat discovery with its new boot
   session. Run packet loss, delayed ACK, duplicate and CRC-error recovery cases
   from CHAT_PROTOCOL only with controlled developer test packets.

The packet workers use finite TX timeouts, spacing and local airtime budgets.
Those limits are not a regulatory duty-cycle certificate. The chat is
unencrypted and unauthenticated; use nonsecret test strings. Preserve complete
ESP32 serial and Flipper `EspChat` logs for each attempted exchange.

## Pending physical test matrix

| ID | Measurement/evidence required | Status |
| --- | --- | --- |
| HW-01 | OLED readable, four buttons, SD-skip path, return from Radio Lab | Pending physical UI check |
| HW-02 | PART/VERSION, register readback, RX state, ready timeouts and recovery | Pending physical CC1101 check |
| RF-01 | AM270 LAB-OOK24 payload/CRC and saved known timing segment | Pending physical reception |
| RF-02 | AM650 matching full-preset repeat of RF-01 | Pending physical reception |
| RF-03 | FM238/FM476 matched receive settings and known-source evidence | Pending physical reception |
| RF-04 | Antenna/matching-network band and orientation/range observations | Unknown hardware specification |
| RF-05 | Absolute/relative crystal drift and synthesizer calibration | Pending measurement |
| RF-06 | RSSI offset, sensitivity and nominal-versus-measured PA output | Pending calibrated measurement |
| TM-01 | Median/worst pulse error, edge jitter and total waveform duration | Pending timing measurement |
| SD-01 | Mount, save, reopen, copy/hash comparison, controlled failure recovery | Pending card health/latency check |
| LOAD-01 | OLED + SD activity, ring drops/high-water, long receive/watchdog run | Pending hardware stress test |
| CHAT-01 | Discovery and 1/40/41/160-byte exchange both ways | Pending physical interoperability |
| CHAT-02 | Loss, duplicate, delayed ACK, bad/truncated CRC packet, reboot recovery | Pending controlled RF test |

For each row, record date/time, setup, expected result, actual result, log/file
names, counters and pass/fail. Preserve failures as evidence. Send the complete
serial log, `EspChat` log, both source/captured `.sub` files, selected settings
and module/antenna markings; do not replace measurements with an inferred pass.
