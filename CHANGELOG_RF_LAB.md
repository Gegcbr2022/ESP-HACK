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
