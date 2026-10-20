# Radio Lab: triggered one-burst RAW capture

The Radio Lab RAW/Protocol Read worker arms an RSSI trigger instead of filling
its waveform bank with the CC1101's continuous idle demodulator noise. The host
RSSI threshold in Radio Settings is the trigger threshold; RSSI alone does not
identify modulation or establish that a signal carries a supported protocol.

The GPIO ISR continuously timestamps signed HIGH/LOW intervals. It performs no
SPI, RF gating, allocation or UI work. The worker drains its bounded ISR queue
and reads one RSSI register per nominal 1 ms task tick. A fixed 64-interval
pretrigger ring retains recently ended intervals while waiting for the threshold.
On triggering, that prefix is copied in time order to the existing 2048-interval
bank. The first interval after arming is partial and discarded; export also skips
leading LOW intervals. There is no new heap allocation for the trigger policy.

One burst ends after at least 20 committed intervals and either a 7 ms edge-free
gap while RSSI remains strong, or a full 7 ms below-threshold RSSI window. The
configured RX deadline and manual stop also end capture. A capture stopped with
fewer than 20 committed intervals is incomplete. A full bank, lost ISR samples,
rejected edges, malformed intervals or a lost deferred interval prevent a valid
save. The worker stops and drains the ISR queue before publishing final counters.

During a short RSSI dip, up to 64 intervals are deferred in the same fixed ring.
If RSSI returns before the quiet window, all deferred intervals must survive and
are appended in order. A deferred overflow followed by returning RSSI is reported
as capture loss. If RSSI stays weak for the full 7 ms guard, the deferred tail is
deliberately excluded as noise; every excluded interval, including overwritten
ones, is counted in `captureTailDiscarded`. This is an explicit signal boundary
policy, not a claim that every below-threshold sample was physically noise.

`captureTriggered` and `captureComplete` distinguish waiting, triggered capture,
and a valid stopped burst. Existing `captureFull`, `dropped` and `rejected` fields
still report damage. UI/storage must require triggered + complete, and reject
full/dropped/rejected captures. The output bank's published prefix is immutable,
so OLED preview copies do not disable interrupts while copying the waveform.

Limitations requiring physical validation:

- RSSI settling, AGC response and module sensitivity have not been measured.
  Worker polling is nominal 1 ms, not a hard real-time guarantee.
- A 64-interval prefix cannot preserve an arbitrary high-rate preamble, and a
  weak signal may fail to trigger. Adjust the threshold against a measured noise
  floor and test with a known controlled source.
- Up to a polling interval of noise can enter the bank before a falling RSSI
  reading. A weak legitimate tail may be excluded by the 7 ms policy. Long OOK
  spaces/RSSI dips can intentionally split recordings; this mode captures one
  burst and is unsuitable for unlimited music or arbitrary long packet gaps.
- An SDR-like continuous waveform record is not provided. GPIO timing accuracy,
  first-header retention and RF end-of-burst behavior remain pending hardware
  validation. Host tests exercise the policy with deterministic inputs only.

Reproduce the portable policy tests:

```text
python tests/triggered_capture/run_triggered_tests.py --compiler /path/to/zig
```
