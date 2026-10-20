# SD utilities

These four Lua tools use the existing 128x64 screen and four buttons. Copy their
source files from `examples/lua_apps/` to `/apps/` on the SD card, or use the
existing USB app installer while the device is on its main menu. Open them from
SD Apps. **Hold BACK to exit any tool**; a short BACK has the function below.
All values are session-only. These apps make no radio or other hardware calls.

| App | UP / DOWN | OK | Short BACK |
|---|---|---|---|
| `countdown.lua` | Change selected minutes or seconds | Next field, start, pause/resume; restart after completion | Return to editing the configured duration |
| `calculator.lua` | Change selected operand or operation | Next field: A, operation, B | Cycle the operand increment |
| `unit_converter.lua` | Change the input value | Next conversion direction | Cycle the input increment |
| `resistor_codes.lua` | Change the selected band's color | Next band | Toggle four/five bands; select the first band |

Each tool displays its controls in the bottom row. Color names and all other
labels are ASCII text suitable for the monochrome display.

## Countdown

The initial duration is five minutes. Edit minutes first, press OK to edit
seconds, then press OK to start. The range is 00:01 through 99:59. A zero
duration prompts for a nonzero time. While running, OK pauses/resumes; BACK
returns to editing the original configured duration. On completion, **TIME UP!**
is a visual alert, and OK restarts the same duration. There is no sound or
background alarm after leaving the app.

The countdown uses the MCU millisecond clock, accounts for elapsed time between
callbacks and handles its normal 32-bit wrap. Paused time is excluded. The
remaining seconds round upward, so the display reaches 00:00 at expiry.

## Field calculator

The `>` marker selects A, the operation or B. UP/DOWN changes that field; OK
advances the marker. Operations cycle through addition, subtraction,
multiplication and division. Both operands range from -999999.99 to 999999.99
and retain two decimal places. Division by zero displays an explicit error.
Results are rounded to two decimal places; this is not arbitrary-precision
decimal arithmetic.

BACK cycles increments 0.01, 0.10, 1, 10, 100, 1000, 10000 and 100000. The
selected increment appears at the right of the operation row. For example,
enter A=12, operation `/`, B=3 to display 4.00. Use small increments to enter
decimal values and larger increments to reach a value quickly. At either
operand limit, further changes clamp to the limit.

## Unit converter

OK cycles both directions of six conversion pairs. The input remains unchanged
when switching direction. BACK selects the same eight increments as the
calculator; the input range is -999999.99 through 999999.99. Input and output
are displayed to two decimal places, so small nonzero results can round to
0.00. Temperatures below absolute zero display an error.

| Pair | Relationship used |
|---|---|
| Meter / international foot | 1 ft = 0.3048 m |
| km/h / mile per hour | 1 mph = 1.609344 km/h |
| Celsius / Fahrenheit | F = C x 1.8 + 32 |
| Kilogram / avoirdupois pound | 1 lb = 0.45359237 kg |
| Liter / US liquid gallon | 1 USgal = 3.785411784 L |
| Pascal / psi | 1 psi is approximately 6894.757293168 Pa |

The units follow [NIST conversion factors](https://www.nist.gov/pml/special-publication-811/nist-guide-si-appendix-b-conversion-factors/nist-guide-si-appendix-b9)
and the [international foot definition](https://www.nist.gov/pml/us-surveyfoot).
USgal means a US liquid gallon; it does not mean an imperial gallon.

## Resistor color codes

The first two bands are significant digits in four-band mode; the first three
are significant digits in five-band mode. The next band is the multiplier and
the last is tolerance. An underline marks the selected band, and its full color
name appears beneath the abbreviated band names. The result uses ohm, kohm,
Mohm or Gohm plus the tolerance percentage.

The reader follows the conventional
[Vishay color-code chart](https://www.vishay.com/docs/49411/resistor_color_code_calculator.pdf).
Multiplier colors include gold (x0.1) and silver (x0.01). Tolerance choices are
1%, 2%, 0.5%, 0.25%, 0.1%, 0.05%, 5% and 10%, using their corresponding colors.
The leading digit is 1–9. Zero-ohm jumpers, leading-zero special codes, absent
tolerance bands, military marking schemes and six-band temperature coefficients
are outside this tool's scope. It calculates the nominal code; it does not
measure a component.

## Host validation

`tests/lua_sandbox/utilities_test.inc` runs these actual source files inside the
production Lua VM and 48 KiB sandbox, using the host harness's display, clock
and SDK adapters. It checks timer pause/restart/zero/range/wrap behavior,
arithmetic and decimal edges, division by zero and operand limits, all twelve
conversion directions, absolute-zero rejection, large pressure values, and
resistor four/five-band values, fractional multipliers and tolerance limits.
Every checked draw must stay inside 128x64 and within the callback quota.

These tests establish software behavior and bounds. Installation, screen
legibility and physical button usability on the device are separate checks.
