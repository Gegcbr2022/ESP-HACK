-- @name: Секундомер
-- @category: Tools
-- @description: Пуск, пауза, промежуточное время и сброс.
-- Copy to /apps/stopwatch.lua. Values are session-only; no storage writes.
app = {}

local elapsed = 0
local running = false
local lap = nil
local previous = clock.millis()

local function format_time(ms)
    local minutes = math.floor(ms / 60000)
    local seconds = math.floor(ms / 1000) % 60
    local tenths = math.floor(ms / 100) % 10
    return string.format("%02d:%02d.%d", minutes, seconds, tenths)
end

local function update_elapsed()
    local now = clock.millis()
    local delta = (now - previous) % 4294967296
    previous = now
    if running then
        -- MCU milliseconds, including delayed frames; never an RF timing instrument.
        elapsed = math.min(elapsed + delta, 5999990)
        if elapsed == 5999990 then running = false end
    end
end

function app.tick(dt_ms)
    update_elapsed()
end

function app.input(button, event)
    if event ~= "press" then return end
    update_elapsed()
    if button == "ok" then
        running = not running
    elseif button == "up" then
        lap = elapsed
    elseif button == "down" then
        elapsed, running, lap = 0, false, nil
    elseif button == "back" then
        sdk.quit()
    end
end

function app.draw()
    ui.clear()
    ui.text(0, 0, "СЕКУНДОМЕР")
    ui.text(91, 0, running and "ИДЁТ" or "ПАУЗА")
    ui.line(0, 10, 127, 10)
    ui.text(22, 18, format_time(elapsed), 2)
    ui.text(0, 38, lap and ("КРУГ " .. format_time(lap)) or "UP запомнить круг")
    ui.text(0, 48, "OK пуск / пауза")
    ui.text(0, 56, "DN сброс  BK выход")
end
