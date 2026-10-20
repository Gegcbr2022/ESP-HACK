-- @name: Field calculator
-- @category: Tools
-- @description: Edit two decimal operands and choose +, -, x or division.
-- Two editable decimal operands. UP/DOWN edit, OK next field, BACK step size.
app = {}
local a, b, field, step = 0, 0, 1, 3
local steps = {1, 10, 100, 1000, 10000, 100000, 1000000, 10000000}
local operations, operation = {"+", "-", "x", "/"}, 1

local function decimal(value) return string.format("%.2f", value / 100) end
local function result()
    if operation == 4 and b == 0 then return "DIVIDE BY ZERO" end
    local value
    if operation == 1 then value = (a + b) / 100
    elseif operation == 2 then value = (a - b) / 100
    elseif operation == 3 then value = a / 100 * (b / 100)
    else value = a / b end
    if math.abs(value) < 0.005 then value = 0 end
    return "= " .. string.format("%.2f", value)
end

function app.input(button, event)
    if event ~= "press" then return end
    if button == "ok" then field = field % 3 + 1
    elseif button == "back" then step = step % #steps + 1
    elseif button == "up" or button == "down" then
        local delta = button == "up" and 1 or -1
        if field == 2 then operation = (operation - 1 + delta) % 4 + 1
        elseif field == 1 then a = math.max(-99999999, math.min(99999999, a + steps[step] * delta))
        else b = math.max(-99999999, math.min(99999999, b + steps[step] * delta)) end
    end
end

function app.draw()
    ui.clear()
    ui.text(0, 0, "FIELD CALCULATOR")
    ui.text(0, 13, (field == 1 and ">A " or " A ") .. decimal(a))
    ui.text(0, 23, (field == 2 and ">OP " or " OP ") .. operations[operation])
    ui.text(0, 33, (field == 3 and ">B " or " B ") .. decimal(b))
    ui.text(0, 45, result())
    ui.text(0, 56, "U/D edit OK> BK step")
    -- The selected increment is shown separately to keep large operands readable.
    local increment = decimal(steps[step])
    ui.text(128 - #increment * 6, 23, increment)
end
