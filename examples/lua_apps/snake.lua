-- @name: Змейка
-- @category: Games
-- @description: Собирайте еду, поворачивайте и растите.
-- Snake: UP turns left, DOWN turns right, OK starts/pauses. No radio use.
app = {}

local cells, head, tail, food = {}, 109, 107, 1
local direction, queued, length, score = 0, nil, 3, 0
local mode, elapsed, previous = "ready", 0, clock.millis()
local random = previous % 4294967296

local function place_food()
    random = (random * 1664525 + 1013904223) % 4294967296
    food = random % 200 + 1
    for i = 1, 200 do
        if cells[food] == nil then return end
        food = food % 200 + 1
    end
    mode = "won"
end

local function restart()
    cells = {[107] = 108, [108] = 109, [109] = 0}
    head, tail, direction, queued, length, score = 109, 107, 0, nil, 3, 0
    elapsed, previous, mode = 0, clock.millis(), "play"
    place_food()
end

local function step()
    direction, queued = queued or direction, nil
    local x, y = (head - 1) % 25, math.floor((head - 1) / 25)
    if direction == 0 then x = x + 1
    elseif direction == 1 then y = y + 1
    elseif direction == 2 then x = x - 1
    else y = y - 1 end
    if x < 0 or x >= 25 or y < 0 or y >= 8 then mode = "over"; return end
    local next_head = y * 25 + x + 1
    local growing = next_head == food
    if cells[next_head] ~= nil and (next_head ~= tail or growing) then
        mode = "over"
        return
    end
    if growing then
        length, score = length + 1, score + 1
    else
        local old_tail = tail
        tail = cells[tail]
        cells[old_tail] = nil
    end
    cells[head], cells[next_head], head = next_head, 0, next_head
    if growing then place_food() end
end

function app.tick(dt_ms)
    local now = clock.millis()
    local delta = (now - previous) % 4294967296
    previous = now
    if mode ~= "play" then return end
    -- A delayed frame advances at most one cell; a clock wrap is ordinary time.
    elapsed = elapsed + math.min(delta, 250)
    local interval = math.max(90, 180 - score * 3)
    if elapsed >= interval then
        elapsed = elapsed % interval
        step()
    end
end

function app.input(button, event)
    if event ~= "press" then return end
    if button == "back" then sdk.quit(); return end
    if button == "ok" then
        if mode == "ready" or mode == "over" or mode == "won" then restart()
        else mode = mode == "play" and "pause" or "play" end
        previous, elapsed = clock.millis(), 0
    elseif mode == "play" and queued == nil then
        if button == "up" then queued = (direction + 3) % 4
        elseif button == "down" then queued = (direction + 1) % 4 end
    end
end

function app.draw()
    ui.clear()
    ui.text(0, 0, "ЗМЕЙКА " .. tostring(score))
    if mode == "ready" then
        ui.text(0, 16, "UP поворот влево")
        ui.text(0, 28, "DN поворот вправо")
        ui.text(0, 40, "OK пуск / пауза")
        ui.text(0, 56, "BK выход")
        return
    end
    if mode == "pause" then ui.text(91, 0, "ПАУЗА")
    elseif mode == "over" then ui.text(73, 0, "ПРОИГРЫШ")
    elseif mode == "won" then ui.text(79, 0, "ПОБЕДА!") end
    ui.rect(0, 11, 127, 42)
    for cell = 1, 200 do
        if cells[cell] ~= nil then
            ui.rect(1 + (cell - 1) % 25 * 5, 12 + math.floor((cell - 1) / 25) * 5, 4, 4, true)
        end
    end
    if mode ~= "won" then
        ui.rect(2 + (food - 1) % 25 * 5, 13 + math.floor((food - 1) / 25) * 5, 2, 2, true)
    end
    if mode == "over" or mode == "won" then ui.text(0, 56, "OK заново  BK выход")
    elseif mode == "pause" then ui.text(0, 56, "OK дальше  BK выход")
    else ui.text(0, 56, "UP< DN> OK пауза") end
end
