# Audited SmartRC 3.0.2 copy

Source: the PlatformIO registry package `lsatan/SmartRC-CC1101-Driver-Lib@3.0.2`,
upstream https://github.com/lsatan/SmartRC-CC1101-Driver-Lib. Original license,
headers, author attribution and legacy include/object aliases are retained.

ESP-HACK patches:

- Ready-pin wait uses wrap-safe `micros()` and a 5 ms deadline instead of a
  200 ms tight loop on every SPI operation. It yields in task context.
- `spiTimeouts()` exposes a cumulative saturating count; `spiReady()` reports
  the sticky fault flag, cleared at the next `Init()` attempt.
- Every failed transaction still releases CS and the SPI transaction lock.
  Failed scalar reads return zero and burst reads clear the entire output.
- Initialization abandons configuration after the first ready-pin timeout.
  Reset's first failed ready wait releases CS immediately, with no second wait.
  Nested setters skip subsequent transactions after an initialization fault.
- Reset holds CS low while waiting for CHIP_RDYn after SRES. SO is high
  impedance when CS is high, so the upstream post-reset check after releasing
  CS could report a false timeout on modules with a MISO pull-up.

The library keeps its existing public methods and normal register behavior.
SPI methods are task-context only; GPIO capture ISRs must never invoke them.
`spiReady()` does not prove radio presence; PARTNUM/VERSION/readback validation
remains the application's responsibility. An isolated successful transaction
does not clear a previously recorded fault; successful `Init()` does.

Native tests compile this actual driver with Arduino/SPI mocks, checking timeout
duration, microsecond wrap, yielding, CS release, error counts, zeroed reads,
late ready recovery and initialization abort. These tests do not validate
electrical SPI or physical CC1101 behavior.
