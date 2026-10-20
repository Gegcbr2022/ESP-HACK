-- @name: Радиопанель
-- @category: Radio
-- @description: Уровень сигнала и несущая. Только приём.
-- Receive-only laboratory dashboard. Copy to /apps/rf_dashboard.lua.
-- OK explicitly starts/stops the named 433.920 MHz / AM270 receive profile.
app = {}

local sampled_ms = 0
local status = "OK включает приём"
local pending_stop = false
local snapshot = radio.snapshot()

function app.tick(dt_ms)
    sampled_ms = sampled_ms + dt_ms
    if sampled_ms >= 100 then
        sampled_ms = sampled_ms % 100
        snapshot = radio.snapshot()
        if pending_stop and not snapshot.active then
            pending_stop = false
            status = "Приём остановлен"
        end
    end
end

function app.input(button, event)
    if event ~= "press" then return end
    if button == "back" then
        radio.stop()
        sdk.quit()
    elseif button == "ok" then
        if snapshot.active then
            radio.stop()
            pending_stop = true
            status = "Остановка приёма"
        elseif not pending_stop then
            local ok = radio.start_rx(433920000, "AM270")
            status = ok and "Запуск приёма" or "Ошибка приёма"
            snapshot = radio.snapshot()
        end
    end
end

function app.draw()
    ui.clear()
    ui.text(0, 0, "РАДИО / ПРИЁМ")
    ui.line(0, 10, 127, 10)
    ui.text(0, 14, "433.920 МГц / AM270")
    if snapshot.active and snapshot.connected then
        ui.text(0, 26, string.format("%d", snapshot.rssi_dbm), 2)
        ui.text(62, 32, "дБм")
        local width = math.floor(math.max(0, math.min(1, (snapshot.rssi_dbm + 120) / 90)) * 27)
        ui.rect(95, 27, 31, 12)
        if width > 0 then ui.rect(97, 29, width, 8, true) end
        ui.text(0, 44, string.format("Нес. %s %d", snapshot.carrier and "да" or "нет", snapshot.samples))
    else
        ui.text(0, 29, status)
        ui.text(0, 44, snapshot.connected and "Приём выключен" or "Нет радиомодуля")
    end
    ui.text(0, 56, "OK приём BK выход")
end
