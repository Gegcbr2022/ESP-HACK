# Radio Lab: triggered RAW capture and four-slot history

Release 3 has two receive workflows with different completion rules. Both use
an RSSI gate, signed GPIO intervals and the same bounded trigger policy. RSSI
does not identify modulation or establish that a protocol is supported. Set
the receiver frequency and preset to match the transmitter before listening.

## Friendly receive: four-slot history

**Радио SubGHz → Принять сигнал** and USB `captureopen` use the normal history
workflow. Before attaching the waveform interrupt, the worker collects 64
background RSSI samples at nominal 4 ms spacing. It takes their median and sets
the session threshold to noise +10 dB, clamped to −100..−45 dBm. Sampling has a
512 ms deadline. Wait for preparation to finish before transmitting.

History groups repeated frames into one candidate with a **30 ms** ending
quiet window: either no edges while RSSI stays strong, or continuous
below-threshold RSSI. A valid candidate must have at least **20 post-trigger
intervals**, plus a later strong RSSI reading at least 1 ms after triggering.
The 64-interval prehistory provides context and cannot satisfy that minimum.

Only live quiet-policy completions enter the four-slot history. **Manual pause,
save, close or a receive deadline does not promote an ongoing prefix to a valid
signal.** An unfinished candidate is discarded; previously retained slots stay
available. Friendly listening has no RX deadline and continues until full,
paused or exited. Advanced single-capture deadlines are separate.

UP/DOWN selects a retained entry and OK saves that exact entry with its own
radio metadata. Saving joins the worker first and leaves other entries intact.
Four slots never overwrite older entries. Comparable repeats can update an
entry's observation count; this is waveform comparison, not protocol decoding.
Hold OK and confirm clearing for a new listening session. Unsaved exit asks
for confirmation. See [the Russian workflow](FILE_PLAYBACK_RU.md).

## Advanced single capture

Advanced RAW/Protocol Read keeps the configured manual RSSI threshold and the
original **7 ms** ending quiet window. It still requires 20 post-trigger
intervals and the later strong RSSI observation. A configured deadline or
manual stop can finalize an already qualified, undamaged single capture,
including its ended intervals; a short/unqualified capture remains incomplete.
This manual-stop rule does **not** apply to the friendly history workflow.

USB `capturetraceopen` uses a single RAW diagnostic session with automatic
threshold preparation, a 30 ms grouping window and a 30-second deadline. It
does not create a four-slot list; its trace bank is separate from history.

## Shared timing, integrity and storage rules

The ISR timestamps signed HIGH/LOW intervals and performs no SPI, RF gating,
allocation or UI work. The worker drains its bounded queue using actual edge
timestamps and reads RSSI at nominal 1 ms ticks. A fixed 64-interval pretrigger
ring retains recent context. The first interval after arming is partial and
discarded; output also skips leading LOW intervals. Each candidate and each
retained slot is limited to 2048 intervals. History uses one separate 32 KiB
four-slot bank; the trigger policy itself allocates no heap memory.

During short weak-RSSI dips, up to 64 intervals are deferred. If RSSI returns
before the mode's ending window, all deferred intervals must survive and are
appended in order. A deferred overflow followed by returning RSSI is capture
loss. If RSSI stays weak for the complete window, the deferred tail is excluded
and counted in `captureTailDiscarded`. A weak legitimate tail can be excluded;
the policy cannot prove that excluded intervals were physically noise.

A full bank, active-candidate ISR loss, rejected edges, malformed intervals or
lost deferred intervals prevent a valid save. Stale armed-noise faults may be
retired only at a clean boundary; this does not repair damaged active signals.
Short false triggers rearm rather than becoming saved records. After damaged
history candidates, the worker waits for a clean 7 ms weak-RSSI or edge-quiet
boundary before starting another candidate. This **7 ms recovery boundary** is
distinct from history's **30 ms burst-ending window**.

`captureTriggered`, `captureComplete`, `captureQualifiedPulses` and
`captureRssiQualified` separate prehistory, qualification and completion.
`captureFull`, `dropped` and `rejected` report damage. The worker stops and
drains the ISR queue before final publication; history copies and SD saves
require the worker to be joined. Retained waveform data and radio metadata
remain fixed until explicit history clearing.

## Physical evidence and remaining limits

Runtime `9b93f08` passed the real friendly-history path with an empty idle/noise
precheck, two distinct Flipper Princeton bursts, SD save/reopen and one manual
RAW replay decoded by the physical Flipper. The input used complete bursts
with ten repeats, TE 390 us and Guard_time 30. Exact results are in
[the release 3 report](TEST_REPORT.md). This establishes that controlled OOK
case, not arbitrary protocols or dynamic-code replay.

- RSSI/AGC response, sensitivity and GPIO edge jitter remain uncalibrated.
  Polling is nominal 1 ms, not a hard real-time guarantee.
- A 64-interval prefix cannot retain every high-rate preamble. Weak signals
  may not trigger, and startup transmissions can contaminate the noise sample.
- Long spaces/RSSI dips can split recordings; short gaps can group repeats.
  Capture a complete burst with the repeats required by the target receiver.
  A saved fragment is not proof that the target will accept replay.
- This is not continuous SDR recording or unlimited music capture. The file
  playback limits remain 2048 intervals and 5 seconds, even if a received
  history candidate's total duration is longer.

Reproduce the portable policy tests:

```text
python tests/triggered_capture/run_triggered_tests.py --compiler /path/to/zig
```
