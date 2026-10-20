# Flipper plaintext chat compatibility

Radio Lab's simple chat automatically selects **Flipper plaintext at
433.920 MHz**, matching the stock Flipper CLI / Enhanced Sub-GHz Chat
plaintext stream. During this chat session it selects the **ShieldedLab** TX
profile at nominal **0 dBm**. This is the user's closed, shielded laboratory
configuration; the value is a radio setting, not a calibrated power measurement.
It sends no text automatically. Exiting chat stops its radio worker and restores
the previous Radio Settings, radio preset, TX profile, power and chat mode.

Controlled physical exchange now passes in both directions with the installed
**Unleashed 086e (`e7e4e179`) CLI** at nominal 433.920 MHz. The tested ESP32
module uses its explicitly saved **+115 ppm** correction; other devices still
default to zero. The Enhanced GUI itself was not exercised in this exchange.

**ESP ACK** remains available in **Дополнительно** for the framed ESP-HACK protocol
and its companion FAP. Both participants must use the same format. Switching
format restarts the worker and clears the displayed history.

| Mode | Peer | Delivery meaning |
|---|---|---|
| ESP ACK | ESP-HACK or the supplied ESP-HACK FAP | Peer acknowledgement, with bounded retries |
| Flipper plaintext | Official CLI or Enhanced chat with encryption disabled | Local transmission completed; no peer acknowledgement |

## Open a compatible peer

1. Open **Радио SubGHz → Чат Flipper** on ESP32. The simple conversation
   screen applies 433.920 MHz and the plaintext profile automatically; no prior
   Radio Settings changes are needed.
2. Connect the Flipper CLI and run `subghz chat 433920000 0` for its internal
   radio; this is the path physically checked on Unleashed 086e. Alternatively open
   Enhanced Sub-GHz Chat, choose **433.920 MHz** and **No encryption**. ESP32
   cannot change the Flipper's encryption setting remotely.
3. On ESP32 press **OK**, then **OK** again to send **TEST123**. The send menu
   initially selects **Тест TEST123** and also offers **Написать текст**,
   **Привет (Hello)**, **Ответ OK** and **Дополнительно**. Selecting a message queues one finite send; entering
   chat or opening the menu does not transmit a message.
4. **UP/DOWN** read message pages and older/newer history. **BACK** exits.
   **Дополнительно** holds the technical mode, peer and delivery diagnostics controls.
5. Compare complete received text both ways. **Отправлено** reports local RF
   completion only. Plaintext has no discovered peer IDs or application ACKs.

The Russian screen shows readiness, sending, completion, failure or a radio error, with a blocking
reason when relevant. The existing limits remain: at least 100 ms between RF
packets and a one-second transmission-attempt budget per 60-second window.
Attempts, including failures, consume that budget; a plaintext message reserves
100 ms per remaining chunk before its first chunk starts. Carrier-busy waiting
and a depleted budget can delay or expire a send.

Source `d63f1ca` uses the host RSSI threshold **-75 dBm** in the automatic chat
profile and always defers while GDO0 indicates an active synchronized packet.
The decision uses the worker's RSSI sample, refreshed every 10 ms and immediately
on first entry. It no longer treats the hardware CS bit alone as busy: that bit
was observed asserted at a quiet -88 dBm and prevented all queued sends.
This changes the software gate; `MCSM1=30`, RF modulation and packet framing
remain unchanged. The bounded packet transmitter enters IDLE before requesting
TX. Manual send, the packet gap, queue deadline and airtime governor still apply.

The official CLI command and terminal-colored name prefix are documented by
the pinned [official CLI source](https://github.com/flipperdevices/flipperzero-firmware/blob/9e810dc399e027e48ce07751df72168abbb19065/applications/main/subghz/subghz_cli.c).
The [catalog manifest](https://github.com/flipperdevices/flipper-application-catalog/blob/main/applications/Sub-GHz/esubghz_chat/manifest.yml)
selects Enhanced chat source commit
`1066d17ad1e8fbf75cea5c6612d79ba539ec8a33`; its
[plaintext send path](https://github.com/xMasterX/all-the-plugins/blob/1066d17ad1e8fbf75cea5c6612d79ba539ec8a33/apps_source_code/esubghz_chat/esubghz_chat.c)
appends CRLF. Enhanced encrypted messages are unsupported here. This reference
does not establish compatibility with every future app/firmware revision.

## Installed Unleashed and source comparison

The physical peer was identified as **Unleashed 086e**, commit `e7e4e179`.
Its pinned [CLI chat path](https://github.com/DarkFlippers/unleashed-firmware/blob/e7e4e179/applications/main/subghz/subghz_cli.c)
uses the [shared TX/RX worker](https://github.com/DarkFlippers/unleashed-firmware/blob/e7e4e179/lib/subghz/subghz_tx_rx_worker.c),
which selects the [GFSK9_99Kb preset](https://github.com/DarkFlippers/unleashed-firmware/blob/e7e4e179/lib/subghz/devices/cc1101_configs.c)
and sends chunks of at most 60 bytes. The installed Enhanced Sub-GHz Chat v1.2
uses that firmware transport. Its No encryption byte stream is compatible by
source comparison; the completed physical test used the firmware CLI, not the
Enhanced application's GUI. The ESP-HACK companion FAP targets official 1.5.1
API 88.2; no physical launch of that FAP on this older firmware is claimed.

An earlier source comparison also checked
[Unleashed unlshd-094](https://github.com/DarkFlippers/unleashed-firmware/releases/tag/unlshd-094),
released on 2026-10-14 at commit
`2f62b3ab5d767c41e09ab17c6b5fbd175c8c4c85`. Its
[chat worker](https://github.com/DarkFlippers/unleashed-firmware/blob/2f62b3ab5d767c41e09ab17c6b5fbd175c8c4c85/lib/subghz/subghz_tx_rx_worker.c)
loads GFSK9_99Kb and splits the stream into packets of at most 60 bytes. Its
[CC1101 preset](https://github.com/DarkFlippers/unleashed-firmware/blob/2f62b3ab5d767c41e09ab17c6b5fbd175c8c4c85/lib/subghz/devices/cc1101_configs.c)
has the matching RF parameters listed below.

Unleashed's linked
[application pack release 18oct2026](https://github.com/xMasterX/all-the-plugins/releases/tag/18oct2026)
is pinned to `1aa52e2a30efaf3f16d59abce9803d9b0f769972`. The release index lists
`esubghz_chat` in the extra pack, and its
[app manifest](https://github.com/xMasterX/all-the-plugins/blob/1aa52e2a30efaf3f16d59abce9803d9b0f769972/apps_source_code/esubghz_chat/application.fam)
identifies **Enhanced Sub-Ghz Chat v1.2**. Its
[No encryption selector](https://github.com/xMasterX/all-the-plugins/blob/1aa52e2a30efaf3f16d59abce9803d9b0f769972/apps_source_code/esubghz_chat/scenes/esubghz_chat_key_menu.c)
disables encryption; its
[send path](https://github.com/xMasterX/all-the-plugins/blob/1aa52e2a30efaf3f16d59abce9803d9b0f769972/apps_source_code/esubghz_chat/esubghz_chat.c)
then appends CRLF and passes the bytes to that firmware transport. These sources
match the ESP32 plaintext format. These 094/application-pack references are
historical source comparisons, not the firmware installed on the tested peer.

## Radio and text format

The [official 1.5.1 worker](https://github.com/flipperdevices/flipperzero-firmware/blob/9e810dc399e027e48ce07751df72168abbb19065/lib/subghz/subghz_tx_rx_worker.c)
loads the [GFSK9_99Kb preset](https://github.com/flipperdevices/flipperzero-firmware/blob/9e810dc399e027e48ce07751df72168abbb19065/lib/subghz/devices/cc1101_configs.c)
and splits its byte stream into RF packets of at most 60 bytes. ESP32's
`shared/flipper_chat_compat.h` follows that stream format.

| Parameter | Flipper plaintext mode |
|---|---|
| Modulation / rate | GFSK / nominal 9,992.60 bit/s at 26 MHz crystal |
| Rate registers | `MDMCFG4=C8`, `MDMCFG3=93` |
| Deviation / RX bandwidth | 19,042.97 Hz / 101,562.5 Hz |
| Sync / preamble | `464C`, 16/16-bit qualification / four bytes |
| Packet framing | Variable length, hardware CRC, whitening disabled |
| Application payload | Plain text bytes; no ESP-HACK binary header or software CRC |

ESP32 limits `PKTLEN` to 60 and enables local CRC autoflush/status append
(`PKTCTRL1=0C`). Stock source specifies `PKTLEN=00` and leaves `PKTCTRL1` at its
reset value `04`. These are receiver/FIFO handling differences; the transmitted
length, text bytes and CRC format remain the same. The receive-only diagnostic
below uses `PKTCTRL1=04` so bad-CRC packets can reach the bounded manual CRC
check and appear in its counters; it does not change the wire format.

Outgoing messages contain `ESP-HACK: `, 1–160 printable ASCII text bytes and
CRLF: at most 172 bytes, split into at most three 60-byte RF chunks. A failed
chunk fails the message without automatic retry. A queued send expires after
10 seconds, including carrier-busy waiting. There is no encryption,
authentication, addressing, discovery exchange or ACK in this format.

Incoming ANSI CSI sequences are stripped across packet boundaries, with a
24-byte escape limit. Tabs become spaces; CR and NUL padding are ignored.
LF completes a line, and 500 ms of silence completes an unfinished terminal
fragment. Other controls, non-ASCII bytes and malformed/incomplete escapes
reject the line. Continuous streams exceeding four seconds are rejected until
a line/silence boundary. Text beyond 160 bytes is visibly marked
` [truncated]`. Timing comparisons tolerate normal 32-bit millisecond wrap.

## USB diagnostics

Send these newline-terminated commands through the firmware's existing USB
serial connection. Only the explicit `chatsend TEST123` command below requests
a radio text send; opening chat and reading diagnostics remain silent.

| Command | Behavior |
|---|---|
| `chatopen` | From the idle main menu, opens the same automatic simple chat UI as the menu shortcut. It sends no text. |
| `chatsend TEST123` | Requests one fixed TEST123 message through the normal UI history/queue path, only while the chat UI owns an active, connected chat worker. The existing TX permission, queue, deadline and airtime limits still apply. Arbitrary serial text sends are not supported. Queued does not mean transmitted or received by the peer. |
| `chatclose` | Requests radio stop and exits the chat screen or its dialogs, restores the previous radio settings/profile, then returns to the main menu. |
| `chatstat` | Prints a cached worker snapshot: active/connected state, format, frequency, TX permission and reason, RSSI, sync/rearm counts, packet/CRC/FIFO/SPI/MISO errors, message counters, last packet length/LQI, configured packet registers and last received text. For a completed packet it also reports appended packet RSSI, frequency error, and trailer CRC versus separately read status CRC/LQI captured before RX rearm. It performs no SPI access. |
| `chatlisten plain433` | From the main menu with no active radio worker, starts a receive-only plaintext diagnostic at 433.920 MHz for at most 30 seconds. |
| `chatlisten plain868` | Same bounded receive-only diagnostic at 868.350 MHz; the Flipper must be set to that frequency. This does not change simple chat's 433.920 MHz default. |
| `chatlisten hz <decimalHz>` | Same receive-only diagnostic at a supported decimal nominal frequency, for at most 30 seconds. The loaded per-device trim also applies. Useful for a controlled center-frequency measurement with zero trim; it never sends RF traffic. |
| `rfcal` / `rfcal ppm <signedDecimal>` | Reads the loaded trim, or explicitly saves -250..+250 ppm from the idle main menu. Default zero; no automatic correction from RSSI or noise. See [calibration](CALIBRATION.md). |
| `rfclock` | Finite radio-IDLE XOSC/192 clock identification through three 50 ms PCNT windows. This is not a ppm calibration; see [measurement limits](CALIBRATION.md). |
| `chatstop` | Requests the current worker to stop. In the chat UI, exit and reopen chat to restart it. |
| `screen` | Prints `SCREEN12864`, the cached 1024-byte monochrome OLED framebuffer as hexadecimal, then `ENDSCREEN`. It reads the UI buffer without a radio SPI operation or display refresh. |

`chatstat`, `screen`, `chatstop`, `chatclose` and the fixed `chatsend TEST123`
are accepted while the chat screen and its dialogs are open. Sending is rejected
once closing begins. `chatopen` is rejected from an existing chat or active
diagnostic; `chatsend TEST123` is rejected at the main menu and during RX-only
diagnostics. A new `chatlisten` is rejected while another radio worker or
the chat UI owns the radio. On its normal timeout or stop, the diagnostic joins
its worker before restoring legacy reception. Diagnostic TX is disabled even
if the normal chat profile enables TX. It prints its start configuration,
received text as `[Chat] RX text=...`, and a final status on completion.

For a receive check, leave ESP32 at the main menu, issue
`chatlisten plain433`, send a unique short message from the Flipper during the
30-second window, then collect `chatstat`. The diagnostic keeps bad-CRC packets
for manual rejection instead of silently flushing them. Sync, CRC and FIFO
counters can narrow a failure; a zero counter alone does not establish a radio
fault or successful interoperability. Register values labeled configured are
the applied configuration cache, not a fresh SPI register dump.
FSCAL3–0 and TEST2–0 are cached after the first successful calibration/RX entry;
those initial results are not continuous live measurements.

For an explicit local UI/TX check, issue `chatopen`, wait until `chatstat`
reports an active, connected worker, then issue `screen`. Issue
`chatsend TEST123` once to request one message and inspect `chatstat`/`screen`
again. Finish with `chatclose`; `[Chat] CLOSED; main menu restored` confirms
that the synchronous UI wrapper has returned. Local completion still does not
confirm Flipper reception.

## Validation status

Run `python tests/flipper_chat_compat/run.py --compiler g++` for C99 and C++11
tests of the actual shared header. The checked Zig and Ubuntu GCC runs each
passed 205,361 checks per language. Tests cover terminal golden vectors at every
split, exact 60-byte boundaries, CRLF/silence, control rejection, truncation,
continuous-stream limits, clock wrap, every outgoing length, local-send status,
failure/no-retry and late-result rejection after timeout. Each language also
executes 100,000 malformed-stream events with state bounds and memory canaries.

The actual automatic profile and RSSI/sync gate additionally passed **1,048,643
checks** under Windows Zig and Linux GCC, covering all signed 16-bit RSSI values,
eight thresholds, threshold equality, sync priority, and the measured noise/peer
levels. Calibration arithmetic/persistence has its own tests documented in
[CALIBRATION.md](CALIBRATION.md).

Controlled physical validation at source `d63f1ca` used the normal ESP32 chat UI,
nominal **433.920 MHz**, normal `PKTCTRL1=0C`, and the installed Unleashed 086e
CLI. ESP32 received the controlled Flipper text messages with **three packets,
zero CRC errors and last packet FREQEST zero**. One manually requested TEST123
send completed (`TX=1`), and the physical Flipper CLI displayed the complete
`ESP-HACK: TEST123` line. The device's +115 ppm NVS setting survived reset;
saving while busy was rejected, no stack panic occurred, and closing chat
restored the main menu. This verifies short plaintext exchange through the
shared firmware transport in both directions on that device.

Enhanced GUI interaction, a physical companion-FAP launch, 160-byte/multiple
packet RF messages, long-range/loss characterization and adverse-condition
FIFO recovery were not part of that completed exchange. Keep those limits
separate from the passing host codec tests and short-message hardware result.
