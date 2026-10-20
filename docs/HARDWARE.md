# Existing hardware and wiring

This release targets the user's assembled **ESP32 DevKit V1 / ESP-WROOM-32**
device with USB-C, CC1101, SH1106, four buttons and a MicroSD module. The user
reported FaradayUkraine provenance. That provenance does not establish the
CC1101 module's matching-network band, antenna specification, crystal accuracy
or shielding. No new hardware is required for basic ESP32/Flipper functional
tests. Hardware and RF validation results belong in the test report; this wiring
document does not certify a completed backup, flash, boot or RF test.

## Fixed GPIO map

The source of truth is `src/CONFIG.h`. GPIO numbers are ESP32 GPIO numbers, not
positions on a generic DevKit header.

| Device signal | ESP32 GPIO / setting |
| --- | --- |
| SH1106 SDA | 21 |
| SH1106 SCL | 22 |
| OLED address / size | I2C 0x3C / 128 × 64 monochrome |
| OLED reset | No separate reset GPIO (`OLED_RESET=-1`) |
| Up | 27 |
| Down | 26 |
| OK | 33 |
| Back | 32 |
| CC1101 SCK | 18 |
| CC1101 MISO / SO | 19 |
| CC1101 MOSI / SI | 23 |
| CC1101 CSn | 5 |
| CC1101 GDO0 | 4 |
| CC1101 GDO2 | No physical connection required by this board |
| MicroSD SCK | 14 |
| MicroSD MISO | 17 |
| MicroSD MOSI | 13 |
| MicroSD CS | 15 |

Buttons use the existing normally open, active-LOW wiring and pull-ups. CC1101
logic and supply are 3.3 V; do not substitute a 5 V logic connection. Keep the
already assembled wiring. The source does not require swapping MISO to a
default pin or adding GDO2.

CC1101 uses **VSPI**, mode 0, 4 MHz. MicroSD uses a separate **HSPI** instance
and its own pins; initialization requests 4 MHz. OLED uses I2C. Consequently,
SD/CC1101 electrical bus sharing is not the identified reception defect.
Synchronous SD work previously blocked GPIO polling; interrupt capture now
continues while the UI/storage consumes a bounded queue.

The release fixes CC1101 GDO0/CS at **4/5**. A saved NRF24 configuration cannot
move them to 16/2. Another application must release its radio/interrupt ownership
before reconfiguring CC1101; the Radio Lab and legacy hooks implement this.

## Installed and absent peripherals

- Present as described by the user: ESP-WROOM-32 DevKit, CC1101, SH1106,
  four navigation buttons, MicroSD module, genuine Flipper Zero.
- MicroSD card presence, filesystem, free space and health need confirmation;
  a module being installed does not mean a card is mounted successfully.
- No IR receiver/transmitter is installed. Existing reserved IR pins are 35/16;
  those menu entries cannot demonstrate IR functionality without the hardware.
- No NRF24L01 is installed for this release's validation. Future default NRF24
  CE/CSn are 16/2 with SPI 18/19/23; this is a future wiring option, not evidence
  that a module is present. CC1101 pins 4/5 remain reserved.

No IR, NRF24, added GDO2 wire, different display or different ESP32 model is
needed to test the implemented CC1101 applications.

## Radio assumptions and limits

The CC1101 calculations assume a **26 MHz crystal**, as do the official Flipper
presets. The exact module marking, matching network, antenna band, crystal
marking and measured drift are unknown. Chip VERSION/PARTNUM and a responsive
SPI bus cannot identify antenna tuning or establish clock accuracy.

The software accepts the CC1101 silicon bands 300–348, 387–464 and 779–928 MHz.
This is not a claim that the installed module and antenna perform across all
three. Begin functional work only on a channel appropriate to the known antenna
and test conditions. Report the missing markings/specification rather than
treating an RSSI peak as proof of compatibility.

The ESP32's `Shielded laboratory` TX profile is an explicit opt-in control, with
channel/duration/power limits. It does not detect a shielded enclosure, establish
attenuation, or certify regional permission in Ukraine or anywhere else.
433.920/868.350 MHz are software laboratory choices, not universally authorized
channels. Flipper retains its official region checks. Nominal PA settings are
datasheet entries; conducted power, antenna gain and radiated power are
unmeasured. Leave `Receive only` selected unless the proposed test is permitted.

The CC1101 is a narrowband transceiver, not an SDR: RSSI sweeps do not provide IQ
samples, simultaneous wideband capture or reliable automatic modulation
identification. GPIO RAW captures demodulated levels rather than RF audio.

## First boot when SD initialization fails

The boot dialog says **`SD Card init failed.`** and **`ERROR: 0x000`**. Its two
on-screen choices are **`OK`** and **`Retry`**. There is no button labelled Skip:
the left **OK** action performs the skip when skipping is allowed.

1. Release all four physical buttons.
2. Leave the left on-screen **OK** selected (the default). If **Retry** is
   highlighted, press Up or Down once to select **OK**.
3. Press and release the physical **OK** button. The boot process continues
   without a mounted SD card; wait for the logo/menu to finish.
4. To retry instead, select **Retry** with Up/Down, then press and release OK
   after the existing card is properly seated. Back does not dismiss this dialog.

After skipping, open **SubGHz → Radio Lab → CC1101 Diagnostics** for live radio
checks. File browsing, import/export and saving require a healthy mounted card.
Legacy Read/Read RAW can request the SD dialog again. Do not interpret skipping
the dialog as proof that SD works, and do not format or overwrite existing
recordings just to run a radio check.

## Validation and references

Use [HARDWARE_VALIDATION.md](HARDWARE_VALIDATION.md) for the exact OLED paths,
laboratory waveform, logging and pending test matrix. See
[RF_AUDIT.md](RF_AUDIT.md) for demonstrated software defects and capture limits.

- [TI CC1101 datasheet](https://www.ti.com/lit/gpn/cc1101).
- [Official Flipper preset register source, 1.5.1](https://github.com/flipperdevices/flipperzero-firmware/blob/1.5.1/lib/subghz/devices/cc1101_configs.c).
- [Original ESP-HACK wiring reference](https://github.com/Teapot174/ESP-HACK/blob/main/README.md).
