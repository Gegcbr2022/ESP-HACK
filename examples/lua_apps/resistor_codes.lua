-- @name: Resistor codes
-- @category: Tools
-- @description: Read conventional 4/5-band resistor values and tolerance.
-- Conventional nonzero-leading-digit 4/5-band resistor color-code reader.
-- UP/DOWN color, OK next band, BACK 4/5 bands. Hold BACK exits natively.
app = {}
local names = {"BLACK", "BROWN", "RED", "ORANGE", "YELLOW", "GREEN", "BLUE", "VIOLET", "GRAY", "WHITE", "GOLD", "SILVER"}
local abbreviations = {"BLK", "BRN", "RED", "ORG", "YLW", "GRN", "BLU", "VIO", "GRY", "WHT", "GLD", "SLV"}
local tolerance_colors = {2, 3, 6, 7, 8, 9, 11, 12}
local tolerances = {"1", "2", "0.5", "0.25", "0.1", "0.05", "5", "10"}
local digits, multiplier, tolerance, selected, five = {1, 0, 0}, 2, 7, 1, false

local function color(band)
    local count = five and 3 or 2
    if band <= count then return digits[band] + 1 end
    if band == count + 1 then
        return multiplier == -2 and 12 or multiplier == -1 and 11 or multiplier + 1
    end
    return tolerance_colors[tolerance]
end

function app.input(button, event)
    if event ~= "press" then return end
    local count = five and 3 or 2
    if button == "ok" then selected = selected % (count + 2) + 1
    elseif button == "back" then five, selected = not five, 1
    elseif button == "up" or button == "down" then
        local delta = button == "up" and 1 or -1
        if selected == 1 then digits[1] = (digits[1] - 1 + delta) % 9 + 1
        elseif selected <= count then digits[selected] = (digits[selected] + delta) % 10
        elseif selected == count + 1 then multiplier = (multiplier + 2 + delta) % 12 - 2
        else tolerance = (tolerance - 1 + delta) % #tolerances + 1 end
    end
end

function app.draw()
    ui.clear()
    local count = five and 3 or 2
    ui.text(0, 0, five and "RESISTOR 5 BAND" or "RESISTOR 4 BAND")
    for band = 1, count + 2 do ui.text((band - 1) * 24, 14, abbreviations[color(band)]) end
    ui.line((selected - 1) * 24, 23, (selected - 1) * 24 + 17, 23)
    ui.text(0, 28, string.format("Band %d: %s", selected, names[color(selected)]))
    local number = digits[1] * 10 + digits[2]
    if five then number = number * 10 + digits[3] end
    number = number * 10 ^ multiplier
    local unit = "ohm"
    if number >= 1000000000 then number, unit = number / 1000000000, "Gohm"
    elseif number >= 1000000 then number, unit = number / 1000000, "Mohm"
    elseif number >= 1000 then number, unit = number / 1000, "kohm" end
    ui.text(0, 41, string.format("%.2f %s +/-%s%%", number, unit, tolerances[tolerance]))
    ui.text(0, 56, "U/D color OK> BK 4/5")
end
