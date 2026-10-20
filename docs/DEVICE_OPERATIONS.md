# Connected-device operations

This report concerns one attached ESP32 DevKit / SH1106 board, not all boards
or all functions. Private dumps, device identifiers and raw serial/flashing
logs are retained locally and are excluded from the public fork and release.

## Backup and first hardware check

- Windows initially exposed a CP2102 bridge without a driver. A matching,
  catalog-signed driver was installed, and the bridge became COM4.
- The chip identified as ESP32-D0WD-V3 revision 3.1 with 4 MiB flash.
- A complete 4,194,304-byte host dump was saved before any firmware write.
  Its saved-file SHA256 and independent full-image esptool device MD5
  verification succeeded. An interrupted second host read is not counted.
- The first RF laboratory build was written as four verified segments, leaving
  the NVS gap intact. The original full-flash backup remains the rollback image.
- Actual boot output reported CC1101 PART=00 / VERSION=14, no SPI errors or
  ready timeouts, and AM270 asynchronous RX with MARCSTATE=0D. The SD card
  mounted and SH1106 UI initialization reached the main menu.

## Historical release 1 SDK build

The final Lua SDK image was written and all four segments passed explicit
esptool verification. The final image includes Flipper plaintext mode and
five-app diagnostics. Its application image is 1,658,160 bytes, SHA256
`286d1bd5f7f810df885393c1af66f38202a0f311cf8f051f3630394937e8d1f1`.
This last image defaults the chat UI to Flipper plaintext for the reported
Unleashed app. The original ESP ACK format remains selectable.
After reset, the receive-only probe passed again with CC1101 VERSION=14,
MARCSTATE=0D and zero ready timeouts. SD mounted and UI initialization reported
121,544 free heap bytes. The user confirmed that the OLED menu and buttons work
during the preceding SDK build; final startup reached the same main menu.

The USB installer wrote `hello.lua` (895 bytes), `stopwatch.lua` (1,522 bytes)
and `rf_dashboard.lua` (1,996 bytes), followed by `snake.lua` (3,541 bytes) and
`game2048.lua` (4,230 bytes). Each received a matching `APP_INSTALLED`
size/SHA256 confirmation after the SD file was closed and read back.

Actual `appsdiag` output then reported:

```text
[Lua] SELFTEST PASS version=Lua 5.4.9 peak=14344/49152 instructionQuota=100000 wallUs=5000
[Lua] SAMPLE name=hello.lua result=PASS peak=16784/49152
[Lua] SAMPLE name=stopwatch.lua result=PASS peak=17936/49152
[Lua] SAMPLE name=rf_dashboard.lua result=PASS peak=20832/49152
[Lua] SAMPLE name=snake.lua result=PASS peak=23464/49152
[Lua] SAMPLE name=game2048.lua result=PASS peak=25024/49152
```

The sample check loads source from SD and calls one tick/draw cycle in each
fresh VM. Radio start is disabled in this diagnostic context. These results
establish actual installation/VM execution and quota rejection, not interactive
button coverage or physical RF measurements. UART communication stopped once
after the reset/port sequence and recovered after the user reconnected USB;
the successful sample checks were captured after that reconnection.

## Release 2 connected-device checks

At source `c5779ee`, all eleven Russian Lua sources were installed with matching
closed-file SD readback SHA256 confirmations. The real ESP32 VM passed 120
input/tick/draw frames per application. Its largest app memory peak was
32,872 / 49,152 bytes; the longest observed callback was 6,085 us, within the
draw-only 10,000 us budget. Logic callbacks retain their 5,000 us limit.

Source `4d1fd3a` was written as four explicitly verified segments, preserving
the NVS gap. Its application image was 1,694,528 bytes, SHA256
`651fc599b70090dba9a004cdf9feb6f6b0fd0bec0bfe229f3d0879b09a648141`.
After reset the SD mounted and the Russian Pocket menu was captured directly
from the OLED framebuffer. The radio probe again reported PART00 / VERSION14
and zero SPI/ready timeouts.

A finite, IDLE-only XOSC/192 probe counted clock edges on GPIO4 and restored
IOCFG0. The median estimate was 25,995,040 Hz, identifying a nominal 26 MHz
module. This short gated count is not a ppm calibration or a measured carrier.

Controlled USB tests then used the Flipper's internal radio at 433.920 MHz:

- During a known synthetic Flipper OOK transmission, the ESP's receive-only
  monitor measured a peak of -33 dBm versus approximately -84 dBm background.
- The ESP transmitted six finite Princeton periods for the synthetic test
  value A53A53 at nominal 0 dBm. The Flipper decoded that exact value twice,
  with TE 389/390 us and guard factor30. This establishes that waveform's RF
  reception; the ESP's 782 us wall-completion delta is not edge jitter.
- Plain GFSK chat reception still failed: a controlled Flipper CLI send raised
  ESP RSSI to -48 dBm, but no sync/CRC-valid packet was accepted. The following
  USB-triggered ESP chat send hit a loopTask stack canary; that direction of
  that attempt is invalid. Deferred serial UI dispatch was subsequently added;
  its successful final device run is recorded below.

The attached Flipper identifies as Unleashed086e, commit `e7e4e179`. Its
Enhanced Chat app uses the firmware's radio worker. This differs from the
separately supplied official1.5.1/API88.2 companion FAP build target.

## Pre-keyboard release 2 image and controlled two-way chat

The final runtime source is `d63f1ca96d7af20b336cee8065ea862468adc343`.
All four flash segments passed explicit esptool readback verification. The
application image is **1,703,456 bytes**, SHA256
`0759a2b353ef54feed546e7b24de061c9eb389f7da61f9fcfc808ff642a484fb`.
The Windows target reports 118,872 static RAM bytes and 1,696,885 linked flash
bytes; binary length also includes image metadata/padding. The manifest records
the runtime source separately from subsequent documentation-only release commits.

Controlled fine RX tests kept the Flipper at nominal433.920MHz. At ESP
programming433.960/433.970/433.980MHz, valid packet frequency estimates were
+9,521/0/-9,521Hz. A measured, device-specific **+115ppm** correction was saved
in its separate NVS namespace. It survived reset and was preserved by segment
flashing. Other devices retain the firmware default0ppm; no correction is
inferred automatically from noise or the short clock probe.

An additional send blocker was corrected: the CC1101 hardware carrier flag
remained asserted near the observed -88dBm background. Chat now uses cached RSSI
and sync activity, with an automatic -75dBm host threshold. An active sync always
blocks sending. The modem/wire profile, manual-send requirement, packet gap,
deadlines and attempt budget remain unchanged.

The final test used the **normal ESP chat UI and queue**, not its receive-only
diagnostic, at nominal433.920MHz and PKTCTRL1=0C. The connected Flipper ran
`subghz chat 433920000 0` on its internal radio:

- Both distinct synthetic Flipper messages appeared byte-for-byte in the ESP
  message snapshots. The ESP counted three CRC-valid packets, no CRC/FIFO/SPI
  errors, and a zero last-packet frequency estimate.
- One manually requested ESP message completed with TXcount1, and the Flipper
  actually printed **ESP-HACK: TEST123**. This is peer reception evidence.
- The test produced no panic or stack-canary error. A calibration-save request
  while chat was active was rejected; the saved value remained115. Closing
  chat restored the main menu.

The CLI and installed Enhanced Chat share the verified Unleashed086e radio
worker. This test establishes that controlled plaintext radio path in both
directions. It does **not** constitute a test of the Enhanced GUI, encryption,
long messages, range or the separate official1.5.1/API88.2 ESP ACK FAP.

After this flash, all eleven installed Russian SD apps again passed **120
input/tick/draw frames each** in the real Lua VM at240MHz. Self-test memory
peaked at14,144/49,152 bytes; the largest app peaked at32,872/49,152 and the
longest observed callback was4,982us. The final reboot loaded115ppm, mounted SD,
reported PART00/VERSION14 with zero SPI/ready timeouts, and reached the Russian
Pocket menu with120,280 free heap bytes. A fresh framebuffer capture verified
the actual menu rendering.

## Final image with the visible keyboard

The final runtime source is now `087f9b68fd8f2242e26fdab1054e8d0e72b3134c`.
Its application image is **1,706,208 bytes**, SHA256
`8e14bc05f2c7ac5342035ddc9185c69ef376e270dc00e8968f0cf79772ba569a`.
All four segments again passed explicit device readback verification. The
Windows build reports 118,872 static RAM bytes and 1,699,637 linked flash bytes.

The actual OLED framebuffer showed the new8x4 keyboard, selected key, input
preview/count and SP/delete/page/Done actions. A deferred editor preview opened
and closed with TXcount0 throughout, no panic, and the main menu restored.
The capture is included in [the Russian keyboard guide](KEYBOARD_RU.md).

The controlled short plaintext test above was repeated successfully on this
image: both distinct Flipper CLI messages reached the normal ESP chat UI;
three packets passed CRC with no CRC/FIFO/SPI errors and FREQEST0. The peer
actually printed ESP-HACK:TEST123 for one manually requested ESP send.
The115ppm value survived reset and an active-chat save attempt was rejected.
One earlier repeat did not start the Flipper CLI command while Enhanced Chat
was running; it is retained as an invalid test, not an RF failure. The app was
temporarily closed for the valid CLI test and then relaunched from its existing
SD file. Relaunching it is not an Enhanced GUI chat/RF test.

All eleven SD apps again passed120frames each after this keyboard flash. The
largest VM peak remained32,872/49,152 bytes; the longest observed callback in
this run was4,592us. Boot reached the SH1106 main menu with SD mounted,
120,280 free heap bytes, loaded115ppm and no radio SPI/ready timeouts.

## Release 3: recorded history to SD and physical replay

On **2026-10-21**, runtime source **`9b93f08`** passed a controlled
Flipper-to-ESP32 recording and ESP32-to-Flipper RAW replay check. The same
friendly four-slot auto-RSSI path completed 64 baseline samples; the idle/noise
precheck left the history empty. Two synthetic Princeton bursts, **A53A53** and
**A53B53**, were sent by the Flipper with ten repeats, TE 390 us and
Guard_time 30. They produced separate slots of **445 / 451 RAW intervals**.

Both slots were saved using the normal SD path and their recorded data was
reopened. One manual `captureplay` for the last-saved selected slot 2 reopened
that file and transmitted its RAW data once in **433,963 us**. The physical
Flipper receiver decoded **A53B53** and reported **four packets**. Before/after
checks confirmed the tested **45 CC1101 registers**, PA table and prior RX/IDLE
state were restored. No crash occurred.

The 45-register comparison excludes **FSTEST (0x29)** and **AGCTEST (0x2B)**.
These test registers are snapshot-only: the guard neither writes them nor
requires identical readback, because writes are prohibited and AGCTEST can
change during RX. The other 45 configuration bytes are restored and checked
strictly; PA-table and stable RX/IDLE checks are separate.

The visible workflow selects with UP/DOWN and saves the selected entry with OK.
It listens until four slots fill, a save pauses reception, or the user exits.
Other entries remain available after saving. A new listening session requires
an explicit clear confirmation; unsaved exit is protected. Complete RAW bursts
including the necessary repeats were used here. This does not validate all
protocols or dynamic-code replay. This test concerns OOK/RAW recording and
playback, not Enhanced Chat GUI RF or the companion FAP. The actual OLED
history screen is preserved in the [Russian capture guide](FILE_PLAYBACK_RU.md).

Additional checks on this same release 3 image passed:

- Controlled plaintext chat in both directions between the normal ESP32 UI
  and the Flipper CLI, with valid received CRC and the ESP32's manual TEST123
  received by the Flipper. Saved +115 ppm persisted; no panic occurred.
- Nine of nine finite frequency/restoration checks across nominal
  906,400,000 / 433,889,000 / 433,920,000 Hz, with six synthetic repeats and
  identical actual before/after FREQ bytes, MARC state and PA table. Tested
  RX/IDLE restoration passed.
- Actual OLED keyboard preview/cancel with TX count zero, no panic and main
  menu restored.
- All eleven SD apps at 120 diagnostic frames each, then Lua self-test PASS
  and main-menu return on the ESP32 at 240 MHz.

At publication, these checks did not establish Enhanced Chat GUI RF or
companion-FAP operation. The later GUI-only follow-up is recorded below.
Earlier release 2 evidence above remains historical and separate.

## Post-publication GUI follow-up, 2026-10-21

The Flipper was updated to Unleashed **094e / `2f62b3ab` / API 88.19**.
With **Enhanced Chat GUI v1.2**, **No encryption**, **433,920,000 Hz** and
**Internal** radio, a controlled short-ASCII exchange passed in both directions
without changing the ESP32 firmware. Its per-device **+115 ppm** remained loaded.

The GUI's short `test94` message reached ESP32: baseline `RX=0 / packets=0`
became `RX=2 / packets=2`, and both trailer CRC and separately sampled status
CRC flags were valid. One explicit `chatsend TEST123` was visibly received in
the Flipper GUI as **`ESP-HACK: TEST123`**. Independent inspection confirmed the
GUI message lines and selected settings. Final ESP32 counters were `RX=3 / TX=1`;
`chatclose` reported CLOSED and restored the main menu. A separate command
refused while busy is excluded from this evidence.

This is a short plaintext GUI test on that pair, not encryption, long-message,
range or companion-FAP validation. Private raw logs/screens are not published.
The published `.3` tag and six release assets remain immutable; this is a dated
documentation follow-up, not a replacement firmware package.

## Physical checks still needed

Serial boot and register observations establish startup and bus access. They
do not establish OLED appearance or all button workflows, RF waveform timing,
receiver sensitivity, antenna matching, calibrated power, spectrum behavior,
or RF behavior beyond the finite OOK, recorded RAW and controlled plaintext tests above. Follow HARDWARE_VALIDATION.md
with a known transmitter/receiver and measurement equipment for those checks.
