# ESP-HACK Sub-GHz Chat protocol v1

Status: codec and transport compiled as C99 and in a real Flipper FAP using
official SDK1.5.1/API88.2. Host tests pass. RF interoperability and hardware
timing remain **pending physical validation**.

## Radio profile

`shared/chat_radio.h` is the single source of register pairs for both devices.
The frequency registers are computed by each hardware HAL using the same
26MHz crystal assumption. Measured module crystal error has not been established.

| Parameter | Value |
|---|---|
| Modulation | 2-FSK, no Manchester |
| Bit rate | 9595.87097bit/s (`MDMCFG4=CA`, `MDMCFG3=83`) |
| Deviation | 19042.96875Hz (`DEVIATN=34`) |
| RX bandwidth | 101562.5Hz |
| Sync | D391, 30/32-bit qualification |
| Preamble | 4bytes |
| Packet length | Variable, maximum60bytes excluding length byte/hardwareCRC |
| Whitening | Enabled |
| Hardware CRC | Enabled, bad packets autoflush |
| RX FIFO status | RSSI/LQI appended; length+60data+2status=63bytes |
| RX/TX completion | Return to IDLE, explicit software rearm |
| GDO0 | Sync detected/end of packet (`IOCFG0=06`) |
| Default PA | Typical -10dBm table value; not measured/calibrated |
| Frequency options | 433.920/868.350MHz, subject to hardware and local rules |

TX begins disabled. Each application must opt in locally and gate frequency,
power, duration, airtime and regional restrictions. The portable session engine
does not imply RF permission. Hardware CRC, length byte, preamble and sync are
generated/removed by CC1101; they are not part of the wire frame below.

## Binary frame

Integers are unsigned, little-endian. No C structure is sent directly.
Every frame is18bytes header +0..40bytes payload +2bytes softwareCRC, at most60.

| Offset | Size | Meaning |
|---|---|---|
| 0 | 1 | Magic EC |
| 1 | 1 | Version1 |
| 2 | 1 | Type1 discovery,2 text,3 ACK |
| 3 | 1 | Flags, must be0 |
| 4 | 2 | Sender ID,1..FFFE |
| 6 | 2 | Recipient ID; FFFF only for discovery |
| 8 | 2 | Origin sender's random boot-session ID,1..FFFF |
| 10 | 4 | Message ID, nonzero |
| 14 | 1 | Fragment index,0-based |
| 15 | 1 | Fragment count,1..4 |
| 16 | 1 | Payload length,0..40 |
| 17 | 1 | Reserved, must be0 |
| 18 | N | Payload |
| 18+N | 2 | CRC16-CCITT-FALSE over header+payload, little-endian |

Software CRC uses polynomial1021, initialFFFF, no reflection and no finalXOR.
The check value for ASCII`123456789` is29B1. Both hardwareCRC and softwareCRC
must pass. SoftwareCRC also permits deterministic host testing of serialized
frames independently of radio hardware.

Text is1..160 printable ASCII bytes (20..7E), unicast only. A nonfinal fragment
contains exactly40bytes. The final fragment contains1..40bytes. A discovery
frame has one fragment and0..16 printable ASCII bytes naming the device.
ACK payload is empty. ACK swaps sender/recipient and echoes the text origin's
boot session, message ID, fragment index and fragment count. The ACK session is
therefore the original sender's session, not the ACK transmitter's boot session.

## Session behavior and bounds

`chat_session.h` is a header-only C99/C++ engine without heap allocation,
hardware, thread or clock dependencies. Caller supplies monotonic32-bit
milliseconds, which may wrap normally. Time comparisons require elapsed times
shorter than2^31ms, comfortably above the configured timeouts.

- One outgoing message, at most160bytes; stop-and-wait per fragment.
- Four attempts per fragment including the first transmission.
- ACK timeout700ms; deterministic PRNG backoff30..150ms using caller-provided
  random seed. Total outgoing deadline20seconds.
- Caller indicates carrier busy before poll; busy channel postpones sending.
  RSSI/carrier sensing reduces collisions but cannot eliminate hidden nodes.
- Four concurrent reassembly slots. Each slot expires12seconds after its last
  accepted fragment; expiry is checked when another text frame arrives.
- Four queued ACKs; full queue drops additional ACKs. Sender retries recover
  dropped ACKs. A full reassembly table rejects a new message without ACK.
- Eight completed `(sender, boot-session, message)` identities suppress duplicate
  delivery. Old identities can be evicted; this is a bounded accidental-duplicate
  window, not replay protection.
- Final fragment ACK requires complete reassembly. If a receiver reboots after
  acknowledging earlier fragments, the sender times out rather than falsely
  reporting delivery. Resending the complete message is a user action.
- Eight peer entries, oldest replaced when full. Peer presence is observational,
  not authenticated. Discovery is manual; there is no automatic RF beacon.

`chat_session_poll()` returns one frame and marks it in flight. Caller must call
`chat_session_tx_result(success, now)` once for every returned frame, including
region rejection and radio timeout. Failed data transmissions consume attempts.
A complete receive returns true exactly once within the duplicate window and
puts text/length/source/message in`session.received`; caller must consume it
before processing the next packet. Incoming completions are not heap-queued.
Only enable TX if ACK transmission is permitted; receive-only operation still
receives text, but the other device cannot obtain delivery confirmation.

Random16-bit boot sessions and IDs can collide. Flipper's device ID is a CRC of
its UID; this is identification convenience, not a security identity. Message
IDs are seeded and incremented. Very old delayed packets can be delivered again
after restart or duplicate-window eviction. No durable inbox/ack database exists.

## Security

There is no encryption or authentication in v1. A shared audited AEAD library
and key-provisioning workflow were not available across both stock SDK targets
without additional design and review. This release deliberately avoids custom
cryptography. CRC is not authentication; an attacker can create valid text,
discovery and ACK frames. Use only nonsensitive laboratory messages. Spoofing,
injection, replay protection and authenticated peer enrollment remain unsupported.

## Automated and physical tests

`tests/test_chat.c` checks known CRC, encoding/endianness, output bounds,
every single-bit corrupted sample frame, malformed sizes/controls, clean160-byte
delivery, first data/ACK loss, duplicates, delayed ACKs, tick wrap, out-of-order
fragments, reassembly/ACK bounds, boot-session change, receive-only behavior,
carrier timeout, four failed attempts and wrong-session ACK rejection.
It also performs100000 malformed-frame trials. Host simulation does not test
RF, interrupt latency, crystal accuracy, CC1101 FIFO, antenna, range or regulation.

Reproduce both C99 and C++11 host builds with a native GCC/Clang or Zig compiler:

```text
python tests/run_chat_tests.py --compiler /path/to/zig
```

The Flipper HAL/GDO/FIFO recovery path is compiled and documented as a pending
hardware regression in the app README. It is not covered by the host engine mock.

Pending physical procedure: match this exact profile on both devices, enable
permitted lab TX, exchange discovery, send unique1/40/41/160-byte messages both
directions and compare exact text and delivery count. Record frequency, nominal
PA, actual firmware versions, device IDs, distance/attenuation, RSSI and failures.
Repeat at least100short messages while respecting local duty/channel rules.
Then switch a receiver off during a fragmented message and confirm sender failure
and successful explicit resend after reboot. Use controlled attenuation to study
loss; do not interfere with unrelated users. No such hardware tests have been run.

## Sources

- TI CC1101 datasheet: https://www.ti.com/lit/ds/symlink/cc1101.pdf
- Official1.5.1 HAL: https://github.com/flipperdevices/flipperzero-firmware/blob/1.5.1/targets/f7/furi_hal/furi_hal_subghz.c
- Official1.5.1 packet FIFO implementation: https://github.com/flipperdevices/flipperzero-firmware/blob/1.5.1/lib/drivers/cc1101.c
- Official build tool: https://github.com/flipperdevices/flipperzero-ufbt
