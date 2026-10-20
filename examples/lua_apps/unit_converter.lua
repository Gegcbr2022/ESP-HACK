-- @name: Unit converter
-- @category: Tools
-- @description: Twelve directions for length, speed, temperature, mass and more.
-- UP/DOWN edit input, OK next conversion, BACK step size. No hardware access.
app = {}
local pairs = {
    {"m", "ft", 1 / 0.3048, 0},
    {"km/h", "mph", 1 / 1.609344, 0},
    {"C", "F", 1.8, 32},
    {"kg", "lb", 1 / 0.45359237, 0},
    {"L", "USgal", 1 / 3.785411784, 0},
    {"Pa", "psi", 1 / 6894.757293168, 0}
}
local steps = {1, 10, 100, 1000, 10000, 100000, 1000000, 10000000}
local value, mode, step = 0, 1, 3

function app.input(button, event)
    if event ~= "press" then return end
    if button == "ok" then mode = mode % 12 + 1
    elseif button == "back" then step = step % #steps + 1
    elseif button == "up" or button == "down" then
        value = math.max(-99999999, math.min(99999999, value + (button == "up" and steps[step] or -steps[step])))
    end
end

function app.draw()
    ui.clear()
    local kind = math.floor((mode + 1) / 2)
    local pair, reverse = pairs[kind], mode % 2 == 0
    local source, target = reverse and pair[2] or pair[1], reverse and pair[1] or pair[2]
    local input = value / 100
    local output = reverse and (input - pair[4]) / pair[3] or input * pair[3] + pair[4]
    if math.abs(output) < 0.005 then output = 0 end
    ui.text(0, 0, "UNIT CONVERTER")
    ui.text(0, 13, source .. " -> " .. target .. string.format(" (%d/12)", mode))
    ui.text(0, 25, string.format("IN  %.2f %s", input, source))
    if kind == 3 and ((not reverse and value < -27315) or (reverse and value < -45967)) then
        ui.text(0, 37, "Below absolute zero")
    else
        ui.text(0, 37, string.format("OUT %.2f %s", output, target))
    end
    ui.text(0, 47, string.format("Step: %.2f", steps[step] / 100))
    ui.text(0, 56, "U/D edit OK> BK step")
end
