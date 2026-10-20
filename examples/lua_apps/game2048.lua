-- @name: 2048
-- @category: Games
-- @description: Merge tiles with four buttons
-- 2048: UP/DOWN move vertically, OK right, BACK left. Hold BACK exits.
app = {}

local board, values = {}, {}
local score, moves, mode, won = 0, 0, "help", false
local random = clock.millis() % 2147483648
local labels = {"2", "4", "8", "16", "32", "64", "128", "256", "512", "1024", "2048", "4096", "8192"}

local function roll(n)
    random = (random * 1103515245 + 12345) % 2147483648
    return random % n + 1
end

local function spawn()
    local empty = 0
    for i = 1, 16 do if board[i] == 0 then empty = empty + 1 end end
    if empty == 0 then return end
    local chosen = roll(empty)
    for i = 1, 16 do
        if board[i] == 0 then
            chosen = chosen - 1
            if chosen == 0 then board[i] = roll(10) == 1 and 2 or 1; return end
        end
    end
end

local function restart()
    for i = 1, 16 do board[i] = 0 end
    score, moves, mode, won = 0, 0, "play", false
    spawn()
    spawn()
end

local function index(line, position, direction)
    if direction == "up" then return (position - 1) * 4 + line end
    if direction == "down" then return (4 - position) * 4 + line end
    if direction == "back" then return (line - 1) * 4 + position end
    return (line - 1) * 4 + 5 - position
end

local function blocked()
    for i = 1, 16 do
        if board[i] == 0 then return false end
        if board[i] < 31 then
            if i % 4 ~= 0 and board[i] == board[i + 1] then return false end
            if i <= 12 and board[i] == board[i + 4] then return false end
        end
    end
    return true
end

local function move(direction)
    local changed = false
    for line = 1, 4 do
        local count = 0
        for position = 1, 4 do
            local value = board[index(line, position, direction)]
            if value ~= 0 then count = count + 1; values[count] = value end
        end
        local source, target = 1, 1
        while source <= count do
            local value = values[source]
            if source < count and value == values[source + 1] and value < 31 then
                value, source = value + 1, source + 1
                if value >= 11 then won = true end
                score = math.min(999999, score + math.floor(2 ^ math.min(value, 20)))
            end
            local cell = index(line, target, direction)
            if board[cell] ~= value then changed = true end
            board[cell] = value
            source, target = source + 1, target + 1
        end
        for position = target, 4 do
            local cell = index(line, position, direction)
            if board[cell] ~= 0 then changed = true end
            board[cell] = 0
        end
    end
    if changed then moves = math.min(99999, moves + 1); spawn() end
    if blocked() then mode = "over" end
end

function app.input(button, event)
    if event ~= "press" then return end
    if mode == "help" or mode == "over" then
        if button == "ok" then restart() end
    elseif button == "up" or button == "down" or button == "ok" or button == "back" then
        move(button)
    end
end

function app.draw()
    ui.clear()
    ui.text(0, 0, (won and "2048! S:" or "2048 S:") .. tostring(score))
    if mode == "help" then
        ui.text(0, 14, "UP/DOWN move up/down")
        ui.text(0, 26, "OK right   BACK left")
        ui.text(0, 38, "Hold BACK to exit")
        ui.text(0, 56, "OK starts")
        return
    end
    ui.text(86, 0, "M:" .. tostring(moves))
    for row = 0, 3 do
        for col = 0, 3 do
            local value = board[row * 4 + col + 1]
            ui.rect(col * 32, 12 + row * 10, 31, 10)
            if value ~= 0 then
                local label = labels[value]
                if not label then
                    if value < 20 then label = string.format("%d", 2 ^ (value - 10)) .. "K"
                    else label = string.format("%d", 2 ^ (value - 20)) .. "M" end
                end
                ui.text(col * 32 + math.floor((31 - #label * 6) / 2), 13 + row * 10, label)
            end
        end
    end
    if mode == "over" then ui.text(0, 56, "GAME OVER OK retry")
    else ui.text(0, 56, "UP/DN OK> BACK<") end
end
