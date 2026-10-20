# Flipper plaintext chat compatibility

Radio Lab's chat starts in **Flipper plaintext**, matching the user's Unleashed
chat setup. This mode uses the stock Flipper CLI / Enhanced Sub-GHz Chat
plaintext stream. **ESP ACK** remains selectable for the framed ESP-HACK
protocol and its companion FAP. Select the same mode and frequency on the
participants. Switching mode restarts the radio worker and clears the displayed
chat history.

| Mode | Peer | Delivery meaning |
|---|---|---|
| ESP ACK | ESP-HACK or the supplied ESP-HACK FAP | Peer acknowledgement, with bounded retries |
| Flipper plaintext | Official CLI or Enhanced chat with encryption disabled | Local transmission completed; no peer acknowledgement |

## Open a compatible peer

1. Choose the matching frequency in Radio Settings, then open
   **SubGHz → Radio Lab → Sub-GHz Chat** on ESP32. It starts in
   **Flipper plaintext**; the mode item shows **Mode: Flipper plain**.
   Pressing that mode item switches formats, so leave it unchanged for Unleashed.
2. For official firmware 1.5.1, connect the Flipper CLI and run
   `subghz chat 433920000 0` for its internal radio. Alternatively open
   Enhanced Sub-GHz Chat, choose the same frequency and **No encryption**.
3. Start with reception and enable the existing laboratory TX profile only
   for a permitted test. Compose a short unique ASCII message on either peer.
4. Compare complete received text. **Sent** reports local RF completion only.
   **Send presence** is a plaintext broadcast; there are no discovered peer IDs
   or application acknowledgements in this mode.

The official CLI command and terminal-colored name prefix are documented by
the pinned [official CLI source](https://github.com/flipperdevices/flipperzero-firmware/blob/9e810dc399e027e48ce07751df72168abbb19065/applications/main/subghz/subghz_cli.c).
The [catalog manifest](https://github.com/flipperdevices/flipper-application-catalog/blob/main/applications/Sub-GHz/esubghz_chat/manifest.yml)
selects Enhanced chat source commit
`1066d17ad1e8fbf75cea5c6612d79ba539ec8a33`; its
[plaintext send path](https://github.com/xMasterX/all-the-plugins/blob/1066d17ad1e8fbf75cea5c6612d79ba539ec8a33/apps_source_code/esubghz_chat/esubghz_chat.c)
appends CRLF. Enhanced encrypted messages are unsupported here. This reference
does not establish compatibility with every future app/firmware revision.

## Verified Unleashed sources

The checked stable firmware is
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
match the ESP32 plaintext format. The user's installed Unleashed version is
unknown, and this source comparison does not replace a physical RF test.

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
length, text bytes and CRC format remain the same.

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

## Validation status

Run `python tests/flipper_chat_compat/run.py --compiler g++` for C99 and C++11
tests of the actual shared header. The checked Zig and Ubuntu GCC runs each
passed 205,361 checks per language. Tests cover terminal golden vectors at every
split, exact 60-byte boundaries, CRLF/silence, control rejection, truncation,
continuous-stream limits, clock wrap, every outgoing length, local-send status,
failure/no-retry and late-result rejection after timeout. Each language also
executes 100,000 malformed-stream events with state bounds and memory canaries.

These are host codec/state-machine tests. Bidirectional RF reception with a
physical Flipper, app UI behavior, loss, FIFO recovery and measured radio
parameters remain pending until separately recorded in the hardware report.
For a physical check, exchange unique short and 160-byte messages both ways,
compare exact text, then repeat with ANSI-colored CLI names and interrupted
traffic. Record failures as well as successes.
