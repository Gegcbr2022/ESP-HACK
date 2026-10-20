-- @name: Таймер
-- @category: Tools
-- @description: Обратный отсчёт с паузой и повторным запуском.
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
    if remaining == 0 then notice = "Задайте время > 0"; return end
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
    ui.text(0, 0, "ТАЙМЕР")
    local label = mode == "set" and "ВВОД" or mode == "run" and "ИДЁТ" or mode == "pause" and "ПАУЗА" or "ГОТОВО"
    ui.text(128 - (#label // 2) * 6, 0, label)
    local total = math.ceil(remaining / 1000)
    ui.text(34, 17, string.format("%02d:%02d", math.floor(total / 60), total % 60), 2)
    if mode == "set" then
        ui.line(field == 0 and 34 or 70, 34, field == 0 and 57 or 93, 34)
        ui.text(0, 40, notice ~= "" and notice or field == 0 and "Минуты: от 0 до 99" or "Секунды: от 0 до 59")
        ui.text(0, 56, "UP/DN ввод OK далее")
    elseif mode == "done" then
        ui.text(0, 40, "ВРЕМЯ ВЫШЛО!")
        ui.text(0, 56, "OK заново BK ввод")
    else
        ui.text(0, 40, mode == "run" and "Идёт отсчёт" or "Отсчёт на паузе")
        ui.text(0, 56, mode == "run" and "OK пауза BK ввод" or "OK дальше BK ввод")
    end
end
