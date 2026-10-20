# SD Apps SDK

The SD Apps menu runs small Lua programs from the existing microSD card. Once
firmware containing this runtime is installed, adding or replacing a Lua app
does **not** require rebuilding or reflashing firmware. The SDK targets the
existing 128×64 monochrome OLED and four buttons; it does not load Flipper `.fap`
files or add missing peripherals.

## Install and open an app

Create `/apps` on the SD card and copy these source examples there:

| File | What it does | Controls |
|---|---|---|
| `examples/lua_apps/hello.lua` | Three greetings and a session counter | OK/DOWN next, UP previous, BACK exit |
| `examples/lua_apps/stopwatch.lua` | Millisecond-clock stopwatch with one lap | OK start/pause, UP lap, DOWN reset, BACK exit |
| `examples/lua_apps/rf_dashboard.lua` | Receive-only 433.920 MHz / AM270 RSSI and carrier view | OK start/stop receiver, BACK stop/exit |

Then open **SD Apps** from the main menu and select the filename. Hold BACK to
leave any app. Files must have an ASCII stem containing only letters, numbers,
`_` or `-`, followed by lowercase `.lua`; maximum filename length is 40 bytes.
For example, `/apps/stopwatch.lua` is valid. Use UTF-8/ASCII text source, not a
compiled Lua bytecode file. An app may occupy at most 32 KiB of source.

The USB installer is an alternative to removing the SD card. Leave the device
on its main menu, close other serial monitors, and use a Python environment
with `pyserial` installed:

```powershell
python scripts/install_app.py examples/lua_apps/hello.lua --port COM4
python scripts/install_app.py examples/lua_apps/stopwatch.lua --port COM4
python scripts/install_app.py examples/lua_apps/rf_dashboard.lua --port COM4
```

Replace COM4 with the device's current port. `--name my_app.lua` installs under
a different valid name. The installer sends only the Lua file at 115200 baud;
it does not flash the ESP32. Firmware verifies the size/SHA256 into a temporary
SD file before promoting it, preserving the previous file if promotion fails.
Require the matching `APP_INSTALLED` confirmation. That confirmation establishes
file transfer integrity, not successful Lua execution or hardware validation.

## App lifecycle

Define a global table named `app`. Its callbacks are optional:

```lua
app = {}

function app.tick(dt_ms)
    -- Update small pieces of state, then return.
end

function app.input(button, event)
    if button == "back" and event == "press" then sdk.quit() end
end

function app.draw()
    ui.clear()
    ui.text(0, 0, "My SD app")
    ui.text(0, 18, "Hello!", 2)
    ui.text(0, 56, "BACK exits")
end
```

Top-level code initializes local state once when the file loads. There are no
`init` or `close` callbacks. The host calls callbacks at approximately 30 Hz and
presents the display after `draw`. `dt_ms` is the time delta supplied to `tick`,
limited to 100 ms; it is suitable for animation updates, not precise timing.
Use `clock.millis()` for elapsed wall time and account for its 32-bit wrap.

Button strings are `up`, `down`, `ok`, `back`. Event strings are `press`,
`release`, `hold`. Test for the event you need so one press does not also trigger
on release. Native hold-BACK always exits, independently of an app's handler.
`sdk.quit()` requests a return to the app list; a callback should return after
requesting it. There is no sleep API: finish the callback and let the host loop
run. State in local Lua variables is discarded when the app exits.
Normal exit, hold-BACK and Lua errors all request stop of the app-owned receiver
and wait for its native worker to finish outside Lua execution. There is no
application cleanup callback to implement.

## API reference

All drawing is white on the monochrome framebuffer. Coordinates are pixels;
`(0,0)` is the top-left corner. Keep content within `0..127`, `0..63`. The default
font uses approximately 6×8 pixels per character at scale 1, so a row holds
about 21 ASCII characters. At scale 2 it holds about 10. Text starts at the
requested cursor coordinate. Drawing does not itself refresh the display.

| Call / value | Behavior |
|---|---|
| `ui.width`, `ui.height` | `128`, `64` |
| `ui.clear()` | Clear the framebuffer |
| `ui.text(x, y, text [, scale])` | Draw text; scale is 1 or 2, default 1 |
| `ui.line(x1, y1, x2, y2)` | Draw a white line |
| `ui.rect(x, y, width, height [, filled])` | Draw an outline; `filled=true` draws a filled rectangle |
| `ui.pixel(x, y)` | Draw one white pixel |
| `clock.millis()` | MCU uptime in milliseconds; wraps after about 49.7 days |
| `sdk.quit()` | Request app exit |
| `radio.start_rx(frequency_hz, preset_name)` | Start a named receive profile; returns `true`, or `false, error` |
| `radio.stop()` | Request radio stop; completion is asynchronous |
| `radio.snapshot()` | Return the current status table described below |

`radio.start_rx` accepts frequency in integer Hz within supported CC1101 bands
and the official named presets `AM270`, `AM650`, `FM238`, `FM476`, `FM12K`.
The example uses `433920000, "AM270"`. A conflicting active receiver returns an
error; wait for the current operation to stop before requesting another
profile. A successful start request does not establish chip detection or RF
reception; inspect `active`, `connected` and diagnostics. Do not assume
`radio.stop()` has completed until a snapshot reports
`active=false`.
Repeated starts of the same app-owned active profile are idempotent. Profile
changes/restarts are limited to four per second; wait at least 250 ms after a
change before starting again. A Lua receive session has a native one-hour
deadline and may also end on errors or application exit.

The snapshot table contains:

| Field | Meaning |
|---|---|
| `active` | The radio worker is active |
| `connected` | The most recent radio probe/configuration detected the chip |
| `rssi_dbm` | Current nominal RSSI estimate in dBm |
| `carrier` | Carrier-sense indication |
| `frequency_hz` | Configured/observed frequency in Hz |
| `samples` | Radio status samples taken; this is **not** a packet count |
| `spi_errors`, `fifo_errors` | Diagnostic counters |

RSSI/carrier activity does not identify a modulation or protocol. These values
are estimates until RF calibration and physical testing establish their
accuracy. The SDK provides no TX, packet replay, power-control, register-write,
or arbitrary SPI interface. A Lua radio app cannot enable the laboratory TX
profile through this API.

## Execution limits and supported Lua facilities

The runtime uses Lua 5.4 with a 48 KiB allocation budget for the entire app VM,
including compiled chunks, strings, tables and library state. A source file that
fits 32 KiB can still exceed the runtime heap. Keep tables small, reuse state,
and avoid allocating new large strings every frame. Standard automatic Lua
garbage collection remains managed by the runtime.

The available standard-library subset is base, math, string and table. There
is no module/package loader, filesystem/network/process access, coroutine API,
debug interface, dynamic code loader, metatable access or application-visible
protected-call API. `print` and `collectgarbage` are unavailable. The runtime
also removes `string.dump`, `rep`, `find`, `match`, `gmatch`, and `gsub`.
`string.format`, `string.sub`, math functions and ordinary small tables suffice
for the included examples.

Callbacks have a 100,000-instruction limit and a time check near 5 ms. The time
check is cooperative through Lua hooks, not hard preemption inside a native C
call. Native bindings have their own bounded behavior. Do not write blocking
loops or assume this VM is an operating-system security boundary. Install
reviewed source you trust. Radio capture and packet timing remain native
firmware work, never Lua timing loops.

## Validation and troubleshooting

Host validation checks source loading, allowed APIs, callbacks, memory/instruction
limits and sample behavior using native mocks. It cannot establish SD-card
reliability, OLED legibility, button timing or actual RF reception. Those remain
separate physical checks until reported for the installed firmware.

Run the actual vendored Lua VM with the production sandbox header on a native
host (Zig example):

```powershell
python tests/lua_sandbox/run.py --compiler <zig.exe>
```

The checked host run passed 13,959 checks, including 4,500 sample frames,
allocation/coalescing/canary checks, 25 VM reopen/close cycles, quota interruption,
safe OOM handling, blocked APIs/bytecode and callback errors. Mocks also require
sample text/geometry to remain visible on 128×64 and permit only the example's
named receive profile. Peak VM use on the 64-bit host was 25,512 bytes for hello,
26,200 for stopwatch, and 48,592 of 49,152 bytes for the RF dashboard. These
figures are host results, not measured ESP32 heap usage or hardware executions.

If a file is absent from the list, check the `/apps` directory, safe filename,
lowercase `.lua` suffix and source size. An install error can mean SD unavailable,
wrong port, another serial monitor, an invalid filename, timeout or digest/write
failure. A Lua load/runtime error can indicate a syntax error, unsupported API,
heap exhaustion or a callback budget violation. Retain the displayed error and
serial output while diagnosing; never treat a failed app as successfully run.
