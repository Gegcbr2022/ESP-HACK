# v1.3-rf-lab.2

- Added Pocket as the default main menu: original category icons, three visible
  rows, selection/scroll indicators and SD status. All five menu styles remain
  selectable. Appearance migration retains valid unrelated settings.
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
  prior settings; no automatic beacon is transmitted. Other laboratory TX
  functions retain their explicit profile selection. Enhanced peers must use
  No encryption. ESP ACK mode and its companion FAP remain under Advanced.
- Added clearer chat state/blocking reasons and packet/CRC/FIFO/state
  diagnostics for reproducing the user's failures in both RF directions.
  Successful physical delivery is still unresolved; local Sent is not a peer
  acknowledgement in plaintext mode. Finite TX governors remain active.
- Expanded `appsdiag` to all eleven installed examples, up to 120 input/tick/draw
  frames per app with radio start disabled, followed by return to the main menu.
  Device results are recorded separately in the test report.
- Passed native suites on Windows Zig and Ubuntu GCC: 91,617 Lua checks and
  16,500 sample frames, 758,543 catalog checks and 22,235 menu/migration checks.
  Game tests now release prior host-only assertion chunks before compiling
  another; twenty independent GCC runs passed without changing production
  memory limits or the normal-GC app frame tests.

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
