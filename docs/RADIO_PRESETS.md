# Radio configuration and preset provenance

The portable configuration module has no hardware I/O or dynamic allocation. All frequency calculations assume a **26 MHz crystal**. The original hardware must be checked for its actual crystal marking/calibration before interpreting RF accuracy. `supportedFrequency()` accepts the CC1101 silicon's three bands (300–348, 387–464, 779–928 MHz); this is not a claim about the module's antenna/matching network or permission to transmit.

## Exact Flipper presets

The sparse register/PA arrays in `src/research/flipper_presets.inc` were extracted from official Flipper firmware **1.5.1**, commit `9e810dc399e027e48ce07751df72168abbb19065`, with no guessed register values. Source file SHA256: `79e22c21e42a8a866177d8f57073c163a76277ad8f083657832d63a27a3a6747`. GPL-3.0-or-later provenance is retained in the generated file.

| Name | Flipper enum suffix | Modulation | RX bandwidth | Symbol rate | Deviation |
|---|---|---|---:|---:|---:|
| AM270 | Ook270Async | ASK/OOK | 270833.333 Hz | 3793.716 Baud | Not applicable |
| AM650 | Ook650Async | ASK/OOK | 650000 Hz | 3793.716 Baud | Not applicable |
| FM238 | 2FSKDev238Async | 2-FSK | 270833.333 Hz | 4797.935 Baud | 2380.371 Hz |
| FM476 | 2FSKDev476Async | 2-FSK | 270833.333 Hz | 4797.935 Baud | 47607.422 Hz |
| FM12K | 2FSKDev12KAsync | 2-FSK | 270833.333 Hz | 4797.935 Baud | 12695.313 Hz |

All five use asynchronous serial operation, `PKTCTRL0=0x32`, `IOCFG0=0x0D`. The names are labels, not reliable numeric specifications. In particular FM238 does not mean a 238 kHz RX filter.

**Applying a preset:** stop capture, put the radio in IDLE, reset to the silicon defaults, write the image's `present[]` registers, write PA if appropriate, then set frequency from the `.sub` header and calibrate before RX. Reset is necessary because the official arrays are sparse: writing them over arbitrary previous configuration can retain incompatible settings. The HAL must implement bounded SPI-ready waits and explicit RX recovery. The module does not perform those operations.

The exact source PA tables include the Flipper source's original output settings. They are retained for file fidelity, not granted for transmission. `encodeRadioSettings()` produces an all-zero PA table. The application must install a separately validated power table and enforce its configured test profile before TX. A PA byte is not a universal dBm value: output depends on band, supply, matching, and hardware. Custom ASK shaping must preserve the intended PA ramp; replacing arbitrary entries changes the waveform.

## Advanced settings

`RadioSettings` exposes modulation, FIFO/asynchronous format, frequency, bandwidth, symbol rate, deviation, MSK phase, AGC bytes, sync word/mode, CRC, whitening, Manchester, packet length, UI RSSI threshold, and host RX/TX deadlines. `encodeRadioSettings()` finds the closest supported register combination and returns its realized values in `actual`. Review them before applying. A request whose rounded filter becomes too narrow is rejected, not silently assigned a wider profile.

The data-rate parameter is the **symbol rate**. 4-FSK carries two payload bits per symbol; its preamble/sync use 2-FSK. ASK and OOK share `MOD_FORMAT=3`; their amplitude depth/shaping is controlled by the PA table and `FREND0`, not a separate modulation selector.

Validation includes:

- 2-FSK 600–500000 Baud; GFSK/ASK/OOK 600–250000 Baud; 4-FSK 600–300000 Baud; MSK strictly above 26000 up to 500000 Baud.
- MSK requires FIFO mode. This driver also restricts 4-FSK to FIFO mode. Synchronous serial and PN9 random TX formats are rejected.
- Asynchronous mode disables CRC/whitening/Manchester/FEC and sync insertion. MSK/4-FSK with Manchester are rejected; this bounded variable-length FIFO implementation rejects FEC.
- FIFO payload is limited to 60 bytes, leaving room for the length byte and two appended status bytes in the 64-byte FIFO.
- FSK filter bandwidth must cover at least `2 * deviation + symbol rate`. This engineering guard does not guarantee sensitivity, spectral compliance, or crystal tolerance.
- RX bandwidth is one of 16 hardware filter choices. Modem registers use TI's mantissa/exponent formulas; rate/deviation do not use approximated preset names.
- Host RSSI threshold is -130..0 dBm; host RX deadline 1..3600000 ms; host TX deadline 1..10000 ms. These fields require application enforcement. The RSSI threshold is not advertised as an absolute CC1101 carrier-sense register, and host RX timeout is not the WOR timer.
- Custom presets reject duplicate register writes, unsupported formats/modulations, missing modem configuration, status/strobe/FIFO addresses, and a GDO1 setting that would drive the shared SPI MISO line. Frequency/channel offsets are rejected; the file's Frequency header is authoritative.

Custom import/export also rejects writes to IOCFG2 (address zero): the actual pinned Flipper HAL treats any zero register address as the terminator. The advanced encoder therefore omits IOCFG2. Flipper controls that pin separately for its RF switch. This source-level constraint is stricter than a literal reading of the file-format documentation's `00 00` marker.

The configuration encoder is not an RF optimizer. In particular, TI DN022 says OOK/ASK gain-control settings require measurements over the full input dynamic range. Retaining or choosing a register profile cannot establish that a receive failure is physically fixed.

## Sources

- [Pinned official CC1101 preset arrays](https://github.com/flipperdevices/flipperzero-firmware/blob/9e810dc399e027e48ce07751df72168abbb19065/lib/subghz/devices/cc1101_configs.c)
- [Pinned Flipper preset labels](https://github.com/flipperdevices/flipperzero-firmware/blob/9e810dc399e027e48ce07751df72168abbb19065/lib/subghz/subghz_setting.c)
- [Pinned Flipper custom-preset loader](https://github.com/flipperdevices/flipperzero-firmware/blob/9e810dc399e027e48ce07751df72168abbb19065/targets/f7/furi_hal/furi_hal_subghz.c)
- [TI CC1101 datasheet SWRS061I](https://www.ti.com/lit/ds/symlink/cc1101.pdf), sections 12–17, 27, 29 and Table 3.
- [TI DN022 OOK/ASK register settings](https://www.ti.com/lit/an/swra215e/swra215e.pdf)

Native tests cover byte-level preset values, all 16 bandwidth codes, all 64 deviation codes, frequency boundaries, rounded settings, incompatible combinations, and custom register validation. RF behavior remains pending physical measurement.
