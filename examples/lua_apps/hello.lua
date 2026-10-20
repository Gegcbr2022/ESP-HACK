-- @name: Приветствие
-- @category: Tools
-- @description: Приветствия и время текущего сеанса.
-- Copy to /apps/hello.lua on the SD card. No radio operations.
app = {}

local greetings = {"ПРИВЕТ!", "ДОБРОГО!", "ВПЕРЁД!"}
local selected = 1
local elapsed = 0

function app.tick(dt_ms)
    elapsed = elapsed + dt_ms
end

function app.input(button, event)
    if event ~= "press" then return end
    if button == "ok" or button == "down" then
        selected = selected % #greetings + 1
    elseif button == "up" then
        selected = (selected + #greetings - 2) % #greetings + 1
    elseif button == "back" then
        sdk.quit()
    end
end

function app.draw()
    ui.clear()
    ui.text(0, 0, "ПРИВЕТ / LUA")
    ui.text(103, 0, string.format("%d/3", selected))
    ui.line(0, 10, 127, 10)
    ui.rect(0, 15, 128, 28)
    ui.text(8, 21, greetings[selected], 2)
    ui.text(0, 46, string.format("Сеанс: %d с", math.floor(elapsed / 1000)))
    ui.text(0, 56, "OK далее  BK выход")
end
