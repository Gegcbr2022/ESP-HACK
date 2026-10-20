# Per-device radio frequency calibration

The Radio Lab applies a signed frequency trim to this ESP32's CC1101 module.
The default is **0 ppm**. No measured correction is built into the firmware or
applied automatically from noise, RSSI, a bad-CRC packet, or the clock probe.
Calibrate each module against a known peer or suitable frequency reference.

## Stored value and commands

The signed 32-bit value uses the separate Preferences/NVS namespace
`esphack_rf`, key `ppm`. Accepted values are **-250 through +250 ppm** inclusive.
Loading and saving perform no radio SPI operations and reject an active Radio
Lab worker. The serial save command additionally requires the idle main menu.

| USB command | Result |
|---|---|
| `rfcal` | Prints the currently loaded correction. |
| `rfcal ppm 115` | Saves +115 ppm for this device and immediately uses it for later Radio Lab configuration. |
| `rfcal ppm -20` | Saves a negative correction. |
| `rfcal ppm 0` | Stores the uncorrected setting. |

The parser accepts an optional `+` or `-` followed by decimal digits, with at
most ten digits. Spaces within the value, fractions, suffixes, overflow and
out-of-range values are rejected. Rejection does not change the current trim.
The complete line parser requires newline termination.

At startup, a missing key loads zero. A namespace open error, unexpected NVS
type, or stored value outside the accepted range returns failure and preserves
the current in-memory value; on a fresh boot that value is zero. Saving updates
the in-memory trim only if `Preferences::putInt` reports all four bytes saved.
An unsuccessful save reports an error and keeps the current correction. NVS
calibration is separate from SD app files, imported `.sub` files and the main
settings namespace. Back up or record it before erasing device NVS.

## Nominal frequency and programmed frequency

Positive ppm **raises the programmed synthesizer frequency**. Calculation uses
64-bit integers in two stages:

```
programmedHz = round(nominalHz * (1000000 + ppm) / 1000000)
word = quantize(programmedHz * 65536 / 26000000)
```

The existing synthesizer quantizer rounds to the nearest word, with boundary
clamping that keeps the quantized frequency inside a supported hardware band.
For example, nominal 433,920,000 Hz with +115 ppm produces 433,969,901 Hz before
PLL-word quantization. This programs a higher value to compensate a module
whose actual RF output and receiver center are low relative to the nominal
26 MHz assumption. The sign must come from a controlled measurement.

UI channels, the worker's nominal `frequencyHz`, imported/exported file
frequencies and the TX policy remain nominal. The calibrated frequency is
applied by Radio Lab's shared `frequency()` path, including its receive modes,
chat packet TX and bounded RAW/static-file TX. The previous menu's independent
legacy radio-driver paths do not use this helper. Cached FREQ register bytes
show the actual programmed word. TX permission and airtime limits remain in
force; calibration does not authorize a new channel or transmit a message.

Both the requested nominal frequency and the corrected integer frequency must
be inside one of these hardware ranges, inclusively:

- 300,000,000–348,000,000 Hz
- 387,000,000–464,000,000 Hz
- 779,000,000–928,000,000 Hz

The correction must not push a boundary request into a gap or outside a band.
For example, 928,000,000 Hz with positive trim is rejected, and 300,000,000 Hz
with negative trim is rejected. A valid saved trim may therefore be unsuitable
for a particular boundary request. Configuration then fails with
`Calibrated frequency outside hardware bands` before writing frequency
registers or requesting TX. An unsupported nominal frequency cannot be made
valid by trimming it into a band.

## Finite receive-only measurement

Use a known peer on a fixed nominal channel, preferably with independently
verified frequency accuracy. Record the existing trim first. If measuring from
an uncorrected baseline, save `rfcal ppm 0` from the idle main menu and restore
the old setting if the measurement is inconclusive.

`chatlisten hz <decimalHz>` starts a **receive-only, at most 30-second** plaintext
diagnostic. After it ends, use another finite window at a nearby programmed
frequency while the known peer sends unique, short plaintext test messages on
the fixed channel. A new window is rejected while the preceding worker still
owns the radio; use `chatstop` and wait for completion when needed. Do not infer
center from RSSI alone: compare packets with valid CRC, exact received text,
LQI, and the cached packet frequency estimate. Sweep a narrow bounded interval
around the expected channel and identify consistent valid-CRC reception with
frequency error crossing zero. The diagnostic never sends radio traffic.

With zero trim during that measurement, a useful initial correction is:

```
ppm = round((bestProgrammedHz - peerNominalHz) * 1000000 / peerNominalHz)
```

This is a correction relative to that peer, not a traceable oscillator
calibration. Save it explicitly at the idle main menu, return to the nominal
channel, and repeat the receive test. Then test a manually requested short send
in the opposite direction and compare exact text at the peer. A local TX
completion indicator alone does not establish reception. Recheck after reboot
to verify persistence and after substantial temperature or supply changes.

The `rfclock` probe measures GDO's XOSC/192 signal in three finite 50 ms PCNT
windows. It distinguishes a roughly 26 MHz clock from a substantially different
clock; it is **not a ppm calibration**. One count over 50 ms corresponds to
about 3,840 Hz at the crystal, or about 64 kHz of carrier uncertainty at
433.92 MHz. Software gate timing adds further error. A displayed 25.995 MHz
estimate alone is insufficient evidence to save a frequency correction.

## Automated checks and physical evidence

Run `python tests/radio_calibration_mock/run.py --compiler g++` or provide the
portable Zig compiler path. The actual production math and persistence module
passed **6,730,289 checks** with Windows Zig and Linux GCC. Tests cover every
accepted ppm value, hardware-band boundaries and gaps, rounding, extreme
integers, strict parsing, unchanged outputs on rejection, nominal TX policy,
busy-state rejection, missing/corrupt NVS values, open failures, and failed or
short writes. These are arithmetic/NVS-mock tests, not RF measurements.

Controlled physical testing before saving a trim found valid Flipper-to-ESP
plaintext packets at an ESP programmed frequency of 433.960, 433.970 and
433.980 MHz while the peer stayed at nominal 433.920 MHz. The corresponding
packet frequency estimates were approximately +9,521, 0 and -9,521 Hz, with
two valid-CRC messages at each point and LQI 46–47. Based on this controlled
measurement, **+115 ppm was explicitly saved on that particular module**.
It does not establish a correction for other modules; their default stays zero.

Post-save physical testing at source `d63f1ca` passed at nominal 433.920 MHz
using the normal ESP32 chat UI and Unleashed **086e / `e7e4e179` CLI** on the
peer. ESP32 accepted the controlled Flipper messages with three packets, zero
CRC errors and last packet frequency estimate zero. One manual TEST123 send
completed, and the physical Flipper CLI displayed `ESP-HACK: TEST123`.
The saved +115 ppm value survived reset; a save attempted while the worker was
busy was rejected. Closing chat restored the main menu, and the test completed
without a stack panic. This verifies short plaintext traffic in both directions
on this calibrated device, using normal CRC-autoflush reception (`PKTCTRL1=0C`).

The software clear-channel gate was also corrected in `d63f1ca`: automatic
chat uses measured RSSI against -75 dBm and always defers during packet sync.
The hardware CS bit alone had remained asserted at the -88 dBm background
level and blocked sending. The change uses cached 10 ms RSSI samples and does
not alter `MCSM1=30` or the RF wire profile. See
[chat compatibility](FLIPPER_CHAT_COMPATIBILITY.md) for controls and validation
limits. Enhanced GUI operation and a companion-FAP physical launch were not
tested by this CLI exchange; calibration is relative to this known peer, not
an absolute carrier-frequency or output-power measurement.
