# Sub-GHz Chat for official Flipper Zero firmware

Built for official firmware **1.5.1**, source commit
`9e810dc399e027e48ce07751df72168abbb19065`, SDK target **f7**, exported API **88.2**.
The release build passed uFBT's strict FAP import check. It uses exported SDK
HAL, GUI, keyboard, text box, thread, mutex, notification and region APIs.
There is no import of firmware-private Sub-GHz internals.

## Build

Windows, Python 3 installed, with the full repository including `shared/`:

```powershell
powershell -ExecutionPolicy Bypass -File .\flipper_apps\subghz_chat\build.ps1
```

This installs uFBT 0.2.6 and the official 1.5.1 SDK in the app's ignored
`.tooling/` directory. Output: `dist/esphack_subghz_chat.fap`.
Alternatively install `ufbt==0.2.6`, run `ufbt update --branch=1.5.1`, then run
`ufbt` inside this app directory. The shared headers are included directly;
do not copy only this app directory without `shared/`.

Copy the FAP to `/ext/apps/Sub-GHz/` on a Flipper running official 1.5.1.
This build has not been launched on a physical Flipper. Other firmware versions
can require rebuilding against their SDK; API compatibility has not been tested.

## Controls

- Up: discovered peers and manual discovery transmission.
- Down: RF settings, receive-only/TX opt-in, frequency and nominal output power.
- OK: compose up to160 printable ASCII bytes after selecting a peer.
- Left/Right: select one of the eight messages held in RAM.
- Hold OK: read the selected complete message in a scrollable text box.
- Back: return from a secondary view, or exit from the chat view.

All messages and peer entries are volatile. Reception of a complete new message
produces a short vibration. The screen reports queued, waiting for ACK,
delivered, or failed status for the current outgoing message.

## Laboratory setup

1. Check local RF rules, antenna band, conducted/ERP power and test conditions.
   Use shielded or attenuated equipment where appropriate.
2. Select the same frequency on ESP-HACK and Flipper. The shared profile is
   2-FSK, 9595.87bit/s, 19.043kHz deviation, 101.5625kHz bandwidth and sync D391.
3. Both apps launch in receive-only mode. Enable permitted lab TX on each;
   ACKs require TX on the receiving device too. Frequency or power changes
   reset the Flipper TX opt-in.
4. Send discovery once on each device, select the other device's ID, then send
   a short unique test message. Keep IDs different.
5. Hold OK to inspect the whole received message. Send a160-byte message to
   exercise fragmentation. Record sent/received text and delivery status.

The Flipper firmware's regional frequency check gates every transmission and
the HAL itself also rejects region-blocked TX. The app adds a100ms gap and a
finite local60-second airtime budget with reservation for the200ms TX timeout.
This fixed-window guard is **not a guarantee of regulatory compliance** with
sliding-window duty cycle, occupied bandwidth, channel, antenna or ERP rules.
Frequency choices433.920/868.350MHz are laboratory options, not universal legal
presets. PA settings are typical datasheet entries, not measured output power.

## Radio ownership and limitations

The app owns the internal radio while open. A dedicated worker polls every10ms;
GUI and keyboard handling run independently. GDO0 signals packet sync/end.
RX and TX return to IDLE; the worker reads or flushes FIFO and rearms RX.
The official HAL packet reader ignores caller capacity, so the app provides a
64-byte scratch buffer and subsequently enforces the60-byte protocol limit.
CRC-bad packets autoflush, and a1-second recovery bounds stuck partial RX.
TX waits at most200ms in the radio worker before idle/flush/rearm.

This is unencrypted, unauthenticated laboratory text chat. Do not send secrets.
IDs, discovery and ACKs can be spoofed. CRCs detect accidental corruption, not
attackers. Reception, packet loss, FIFO recovery, PA output, regional behavior,
keyboard usability and notification behavior remain pending hardware validation.
See `docs/CHAT_PROTOCOL.md` and `tests/test_chat.c` for the shared transport.
