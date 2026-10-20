-- @name: Block Fall
-- @category: Games
-- @description: Falling blocks, seven-bag pieces and line scoring.
-- Blockfall: UP rotate, DOWN soft drop, hold DOWN hard drop, OK right, BACK left.
-- Hold BACK exits through the native app host. No radio or saved data.
app = {}
local g = {board={}, mode="help", score=0, lines=0, level=1, elapsed=0,
           x=3, y=-1, kind=1, rotation=1, next=1, random=clock.millis()%4294967296}
app.state = g
local pieces = {
    {0x00F0,0x4444,0x0F00,0x2222}, {0x0660,0x0660,0x0660,0x0660},
    {0x0072,0x0262,0x0270,0x0232}, {0x0036,0x0462,0x0360,0x0231},
    {0x0063,0x0264,0x0630,0x0132}, {0x0071,0x0226,0x0470,0x0322},
    {0x0074,0x0622,0x0170,0x0223}
}
local bag, bagAt = {}, 8
local kicks = {0,-1,1,-2,2}
local awards = {100,300,500,800}
for row=1,16 do g.board[row]=0 end

local function roll(n)
    g.random=(g.random*1664525+1013904223)%4294967296
    return g.random%n+1
end
local function take()
    if bagAt>7 then
        for i=1,7 do bag[i]=i end
        for i=7,2,-1 do local j=roll(i);bag[i],bag[j]=bag[j],bag[i] end
        bagAt=1
    end
    local kind=bag[bagAt];bagAt=bagAt+1;return kind
end
local function fits(x,y,rotation)
    local mask=pieces[g.kind][rotation]
    for row=0,3 do for col=0,3 do
        if (mask&(1<<(row*4+col)))~=0 then
            local bx,by=x+col,y+row
            if bx<0 or bx>=10 or by>=16 then return false end
            if by>=0 and (g.board[by+1]&(1<<bx))~=0 then return false end
        end
    end end
    return true
end
local function spawn()
    g.kind,g.next=g.next,take()
    g.rotation,g.x,g.y,g.elapsed=1,3,-1,0
    if not fits(g.x,g.y,g.rotation) then g.mode="over" end
end
local function restart()
    for row=1,16 do g.board[row]=0 end
    g.score,g.lines,g.level,g.elapsed,g.mode=0,0,1,0,"play"
    bagAt=8;g.next=take();spawn()
end
local function lock()
    local mask=pieces[g.kind][g.rotation]
    -- A piece locked above the visible board ends the game without partial writes.
    for row=0,3 do
        if g.y+row<0 and (mask&(15<<(row*4)))~=0 then g.mode="over";return end
    end
    for row=0,3 do for col=0,3 do
        if (mask&(1<<(row*4+col)))~=0 then
            local index=g.y+row+1
            g.board[index]=g.board[index]|(1<<(g.x+col))
        end
    end end
    local cleared,write=0,16
    for read=16,1,-1 do
        if g.board[read]==1023 then cleared=cleared+1
        else g.board[write]=g.board[read];write=write-1 end
    end
    for row=write,1,-1 do g.board[row]=0 end
    if cleared>0 then
        g.score=math.min(9999999,g.score+awards[cleared]*g.level)
        g.lines=math.min(9999,g.lines+cleared)
        g.level=math.min(20,1+g.lines//10)
    end
    spawn()
end
local function drop(soft)
    if fits(g.x,g.y+1,g.rotation) then
        g.y=g.y+1
        if soft then g.score=math.min(9999999,g.score+1) end
    else lock() end
end
function app.input(button,event)
    if g.mode~="play" then
        if button=="ok" and event=="press" then restart() end
        return
    end
    if button=="down" and event=="hold" then
        local steps=0
        while steps<19 and fits(g.x,g.y+1,g.rotation) do g.y=g.y+1;steps=steps+1 end
        g.score=math.min(9999999,g.score+steps*2);lock();return
    end
    if event~="press" then return end
    if button=="up" then
        local rotation=g.rotation%4+1
        for i=1,5 do
            if fits(g.x+kicks[i],g.y,rotation) then
                g.x,g.rotation=g.x+kicks[i],rotation;break
            end
        end
    elseif button=="down" then drop(true)
    elseif button=="ok" then if fits(g.x+1,g.y,g.rotation) then g.x=g.x+1 end
    elseif button=="back" then if fits(g.x-1,g.y,g.rotation) then g.x=g.x-1 end end
end
function app.tick(dt)
    if g.mode~="play" then return end
    g.elapsed=g.elapsed+math.min(100,math.max(0,dt))
    local interval=math.max(100,700-(g.level-1)*30)
    if g.elapsed>=interval then g.elapsed=g.elapsed%interval;drop(false) end
end
function app.draw()
    ui.clear();ui.text(0,0,"BLOCKFALL")
    if g.mode=="help" then
        ui.text(0,12,"UP rotate DOWN drop")
        ui.text(0,24,"OK right BACK left")
        ui.text(0,36,"Hold DOWN hard drop")
        ui.text(0,48,"Hold BACK exits")
        ui.text(0,56,"OK starts");return
    end
    ui.rect(0,7,32,50)
    for row=0,15 do for col=0,9 do
        if (g.board[row+1]&(1<<col))~=0 then ui.rect(1+col*3,8+row*3,2,2,true) end
    end end
    if g.mode=="play" then
        local mask=pieces[g.kind][g.rotation]
        for row=0,3 do for col=0,3 do
            if (mask&(1<<(row*4+col)))~=0 and g.y+row>=0 then
                ui.rect(1+(g.x+col)*3,8+(g.y+row)*3,2,2,true)
            end
        end end
    end
    ui.text(38,9,"S:"..tostring(g.score));ui.text(38,18,"Lines:"..tostring(g.lines))
    ui.text(38,27,"Level:"..tostring(g.level));ui.text(38,36,"NEXT")
    local mask=pieces[g.next][1]
    for row=0,3 do for col=0,3 do
        if (mask&(1<<(row*4+col)))~=0 then ui.rect(72+col*3,36+row*3,2,2,true) end
    end end
    ui.text(38,46,g.mode=="over" and "GAME OVER" or "Hold DN drops")
    ui.text(0,56,g.mode=="over" and "OK retry Hold BK exit" or "UP^ DNv OK> BK<")
end
