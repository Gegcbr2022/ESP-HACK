-- @name: Countdown
-- @category: Tools
-- @description: Visual session timer with pause, restart and 32-bit clock wrap.
-- Visual session timer. UP/DOWN edit, OK next/start/pause, BACK edit. No radio.
app = {}
local minutes, seconds, field = 5, 0, 0
local remaining, previous, mode, notice = 300000, clock.millis(), "set", ""

local function update()
    local now = clock.millis()
    local delta = (now - previous) % 4294967296
    previous = now
    if mode == "run" then
        remaining = math.max(0, remaining - delta)
        if remaining == 0 then mode = "done" end
    end
end

local function start()
    remaining = (minutes * 60 + seconds) * 1000
    if remaining == 0 then notice = "Set a nonzero time"; return end
    previous, mode, notice = clock.millis(), "run", ""
end

function app.tick(dt_ms) update() end

function app.input(button, event)
    if event ~= "press" then return end
    update()
    if button == "back" then
        mode, field, notice = "set", 0, ""
        remaining = (minutes * 60 + seconds) * 1000
    elseif button == "ok" then
        if mode == "set" then
            if field == 0 then field = 1 else start() end
        elseif mode == "done" then start()
        else mode = mode == "run" and "pause" or "run" end
        previous = clock.millis()
    elseif mode == "set" and (button == "up" or button == "down") then
        local delta = button == "up" and 1 or -1
        if field == 0 then minutes = math.max(0, math.min(99, minutes + delta))
        else seconds = math.max(0, math.min(59, seconds + delta)) end
        remaining, notice = (minutes * 60 + seconds) * 1000, ""
    end
end

function app.draw()
    ui.clear()
    ui.text(0, 0, "COUNTDOWN")
    local label = mode == "set" and "SET" or mode == "run" and "RUN" or mode == "pause" and "PAUSE" or "DONE"
    ui.text(128 - #label * 6, 0, label)
    local total = math.ceil(remaining / 1000)
    ui.text(34, 17, string.format("%02d:%02d", math.floor(total / 60), total % 60), 2)
    if mode == "set" then
        ui.line(field == 0 and 34 or 70, 34, field == 0 and 57 or 93, 34)
        ui.text(0, 40, notice ~= "" and notice or field == 0 and "Edit minutes (0-99)" or "Edit seconds (0-59)")
        ui.text(0, 56, "U/D set OK next/start")
    elseif mode == "done" then
        ui.text(0, 40, "TIME UP! Visual alert")
        ui.text(0, 56, "OK restart BACK edit")
    else
        ui.text(0, 40, mode == "run" and "Timing session" or "Timer paused")
        ui.text(0, 56, mode == "run" and "OK pause BACK edit" or "OK resume BACK edit")
    end
end
