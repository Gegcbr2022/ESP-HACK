# Games on the microSD card

The **Приложения SD → Игры** section runs Lua games from `/apps/`. A firmware with the
SD Apps runtime is required once; installing or editing another game afterwards
does not require reflashing the ESP32. Games use the display and buttons only.

| Game | Controls |
|---|---|
| Block Fall (`blockfall.lua`) | UP rotates, DOWN drops one row, hold DOWN drops to the floor; OK moves right, short BACK moves left. OK starts or restarts. |
| Minesweeper (`mines.lua`) | UP/DOWN select the previous/next cell, OK reveals, short BACK toggles a flag. OK starts or restarts after a win/loss. |
| Snake (`snake.lua`) | UP turns left, DOWN turns right; OK starts/pauses, short BACK exits. |
| 2048 (`game2048.lua`) | UP/DOWN move vertically, OK moves right, short BACK moves left. OK starts/restarts. |

**Hold BACK exits every game through the native host**, including games that use
short BACK as a playing control. Each game opens with its controls on screen.
Scores and boards last for the current session; games do not save progress.
The shipped titles and help text are Russian: **Змейка**, **2048**,
**Падающие блоки** and **Сапёр**. DN means DOWN, BK means BACK on their compact
help screens. The [Russian quick guide](QUICK_START_RU.md) lists all controls.

## Install the new games

Return the device to its main menu and use the existing USB uploader, replacing
`COM4` with the device's actual port when needed:

```powershell
python scripts/install_app.py examples/lua_apps/blockfall.lua --port COM4
python scripts/install_app.py examples/lua_apps/mines.lua --port COM4
```

Alternatively copy both files into `/apps/` on the existing microSD card. Their
metadata names them **Block Fall** and **Minesweeper** in the Games category.
See [APPS_SDK.md](APPS_SDK.md) for the runtime and upload protocol.

## Game rules and limits

Block Fall has a 10×16 board with seven tetrominoes, a shuffled seven-piece bag,
next-piece preview and gravity that accelerates with the level. Soft/hard drops
score one/two points per row. Clearing one/two/three/four lines scores
100/300/500/800 times the current level. Every ten lines advances the level, up
to 20. A piece that cannot spawn or locks above the board ends the game.
Rotation tries the current position, then horizontal offsets −1, +1, −2, +2;
this is a small custom kick system, not full SRS. Sixteen row bitmasks store the
board, with no growing collection or recursive search.

Minesweeper has an 8×5 board with exactly seven mines. Placement happens at the
first reveal and excludes that cell and all its neighbours, so the opening is
safe and empty. A finite candidate list selects mines without repeated random
attempts. Revealing an empty cell opens connected empty space and its numbered
border using an iterative queue of at most 40 cells. Flags prevent accidental
reveals; at most seven flags may be placed. Reveal all 33 safe cells to win.

Both new games use fixed board bounds, printable text, fewer than 256 drawing
operations per callback and the runtime's 48 KiB VM bank. Each invocation has a
100,000-instruction limit. Input/tick and top-level Lua execution retain a 5 ms
wall-time budget; drawing has 10 ms for framebuffer work. Time checks are
cooperative, including a check after return, and do not preempt native C calls.
There is no RF use.

## Validation

The real Lua VM host suite loads every app and runs 1,500 frames per app with
button events, drawing bounds, heap bounds and quota checks. Additional game
fixtures cover Block Fall's seven-piece bag, wall collisions/kicks, one-, two-
and four-line clears, scoring/level caps, top-out and restart. Minesweeper tests
all 40 protected first-reveal positions, independently checks adjacent mine
counts, flood-fill counts, flags, cursor wrapping, wins, losses and restart.

Run the complete suite with a native compiler:

```sh
python tests/lua_sandbox/run.py --compiler gcc
```

These are host tests of the actual Lua sources. They do not measure physical
button feel, LCD readability or frame timing on a particular device.
