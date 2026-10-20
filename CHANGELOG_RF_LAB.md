# v1.3-rf-lab.2

- Added Pocket as the default main menu: original category icons, three visible
  rows, selection/scroll indicators and SD status. All five menu styles remain
  selectable. Appearance migration retains valid unrelated settings.
- Added Russian Pocket/basic settings labels, new radio screens and all eleven
  Lua apps, with an original Cyrillic font and safe UTF-8 clipping. Existing
  technical protocol names and advanced legacy labels remain unchanged.
- Opened the simple radio home directly from **Радио SubGHz**, with the old
  radio menu and technical tools under **Дополнительно**.
- Added a visible 8 × 4 ASCII keyboard with character pages, selected-key
  highlight, text tail/length counter and SP / <- / >> / OK actions. Hold
  UP/DOWN changes pages; hold BACK cancels without changing the original field.
  Normal-chat acceptance of nonempty text queues a message, while the deferred
  USB `chatedit` preview always discards it. See [keyboard controls](docs/KEYBOARD_RU.md).
- Added file pages of 39 actual paths plus a separate Next Page row, previous
  page navigation, hold-UP/DOWN page shortcuts and visible page/total counts.
  Full names resolve from lightweight directory identities into one selected
  path bank up to 1023 bytes; legacy Send uses the same browser.
- Added manual file Send for one binary OOK RAW pass or six periods of
  RcSwitch (protocol 1, 1..32 bits) / Princeton 1..32 / CAME 12/24, preserving exact supported file frequency and modem
  settings. CAME's leading LOW is explicitly rotated to the period end.
  Non-24 Princeton is a documented ESP-HACK legacy-input extension; protocol
  aliases are not guessed and official Flipper Princeton remains 24-bit.
  Dynamic protocols remain inspection-only; unsupported requests are refused.
- Added a categorized SD launcher with All apps, Tools, Games, Radio, Favorites
  and Recent sections, bounded comment metadata, descriptions, eight favorites
  and four recent apps persisted on SD.
- Expanded the bundle from five to eleven Lua apps. New games are Block Fall
  and Minesweeper; new tools are a visual countdown, decimal calculator, unit
  converter and four/five-band resistor code reader. The runtime retains its
  48 KiB heap, 32 KiB source limit and existing callback/drawing quotas.
- Simplified Chat into a read/send screen and quick-message menu. For the
  authorized Faraday setup, entry temporarily applies 433.920 MHz, Flipper
  plaintext and nominal 0 dBm with TX ready for manual sends. Exit restores
  prior settings; no automatic beacon is transmitted. File Send also temporarily
  selects the authorized lab profile / 0 dBm, requires a fresh hold OK, and is
  limited to 2048 intervals / 5 seconds. Other transmit tools require explicit
  profile selection. Chat's separate airtime governor remains active. Enhanced peers must use
  No encryption. ESP ACK mode and its companion FAP remain under Advanced.
- Added clearer chat state/blocking reasons and packet/CRC/FIFO/state
  diagnostics for reproducing the user's failures in both RF directions.
  Both RF directions failed on the Russian `c5779ee` build; later controlled
  one-way valid-CRC reception required a device-local frequency correction.
  The earlier verified `d63f1ca` image passed controlled two-way plaintext exchange with
  the installed Flipper's internal-radio CLI at nominal 433.920 MHz using this
  ESP32's saved +115 ppm correction. Enhanced Chat GUI and ESP ACK/FAP delivery
  remain unverified. Local Sent is not a peer acknowledgement; finite TX governors remain active.
- Expanded `appsdiag` to all eleven installed examples, up to 120 input/tick/draw
  frames per app with radio start disabled, followed by return to the main menu.
  All eleven Russian apps passed 120 frames each on the final `d63f1ca` ESP32 build;
  this does not validate the RF dashboard's physical radio measurements.
- Passed native suites on Windows Zig and Ubuntu GCC: 91,990 Lua checks and
  16,500 sample frames, 758,551 catalog checks and 21,892 menu/migration checks.
  Game tests now release prior host-only assertion chunks before compiling
  another; twenty independent GCC runs passed without changing production
  memory limits or the normal-GC app frame tests.
- Passed 1,066,348 static encoder, 1,108,737 pagination, 1,048,643 chat profile/carrier-gate
  and 246 mock clock probe checks. Added finite idle-only USB clock diagnostics;
  mock tests do not establish the installed module's crystal frequency.
- Actual-browser, editor/UART and calibration math/persistence suites passed
  5,026, 2,429 and 6,730,289 checks. Keyboard-model tests passed 1,460,550 checks
  under Zig/GCC and GCC address/undefined-behavior sanitizers. Device frequency trim is stored locally;
  other modules keep their own settings rather than inheriting this unit's trim.
- All three CI jobs passed at `c5779ee`. The verified-flashed `e23c31d`
  diagnostic candidate received valid-CRC Flipper packets at an adjusted
  programmed RX frequency. All three CI jobs also passed at `28e4d46`, including
  full-path browsing and device calibration. All three jobs also passed at
  `d63f1ca`; its host carrier gate uses cached RSSI/sync state. The final image's
  four flash segments verified, normal chat UI/Flipper CLI passed both RF
  directions without panic, and chat close restored the main menu.
- The later `087f9b6` visible-keyboard source requires a new physical keyboard,
  RF and eleven-app check; the recorded `d63f1ca` hardware result remains historical.

See [Pocket UI](docs/POCKET_UI.md), [SD games](docs/SD_GAMES.md),
[SD utilities](docs/SD_UTILITIES.md) and [test results](docs/TEST_REPORT.md).
Release 1 flash, probe and five-app evidence remains historical and is not
presented as release 2 validation.

# v1.3-rf-lab.1

- Reproduced the unmodified source baseline; pinned the working ESP32 platform
  and library versions. Added safe Python-based image merging and rollback tag.
- Audited the existing CC1101 paths, retained SmartRC3.0.2, fixed reset-ready
  sequencing, bounded MISO waits and surfaced errors.
- Corrected decoded payload packing and removed the out-of-bounds rc-switch
  RAW timing scan. Restored complete async receive configuration and safe GPIO
  ownership after TX.
- Replaced legacy RAW GPIO polling with a bounded interrupt ring; documented
  overflows, rejection, RSSI filtering, file metadata and SD error behavior.
- Added exact official Flipper1.5.1 presets, validated advanced modem settings,
  streaming `.sub` parsing/export and portable custom-preset persistence.
- Added the Radio Lab application suite and its separate radio worker,
  finite RMT waveform player, generated test waveform, analyzers, diagnostics,
  file/preset workflows and four-button UI.
- Added shared versioned chat codec/session logic and a real official-SDK FAP
  with public radio/GUI APIs, discovery, fragmentation, ACKs, retries, duplicate
  suppression, history, text entry and receive notifications.
- Added native mock/portable tests, Flipper/ESP32 builds, source provenance,
  hardware validation instructions, explicit limitations and private device
  backup / verified flashing tools.
- Added SD Apps across all four main-menu styles, a bounded Lua 5.4 runtime,
  drawing/buttons/time/receive-only radio bindings, three example apps and
  SHA256-verified USB installation without reflashing.
- Added Snake and 2048 as SD apps with real-VM game-state tests, and a selectable
  plaintext chat profile for the official CLI and Enhanced Sub-GHz Chat.
  The plaintext mode has no delivery acknowledgements or encryption.

Physical RF reception, timing, sensitivity, legal RF parameters and Flipper
interoperability are not inferred from compilation or host tests.
