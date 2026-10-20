-- @name: Minesweeper
-- @category: Games
-- @description: Seven mines, flags and a protected first reveal.
-- Mines: UP/DOWN previous/next cell, OK reveal, BACK flag. Hold BACK exits.
-- Seven mines on an8x5 board; the first reveal and its neighbours are safe.
app = {}
local g={mines={},numbers={},open={},flags={},cursor=1,revealed=0,flagged=0,
         placed=false,mode="help",random=clock.millis()%4294967296}
app.state=g
local candidates,queue={},{}
for i=1,40 do g.mines[i]=false;g.numbers[i]=0;g.open[i]=false;g.flags[i]=false end
local function roll(n)
    g.random=(g.random*1664525+1013904223)%4294967296
    return g.random%n+1
end
local function restart()
    for i=1,40 do
        g.mines[i],g.numbers[i],g.open[i],g.flags[i]=false,0,false,false
        queue[i],candidates[i]=0,0
    end
    g.cursor,g.revealed,g.flagged,g.placed,g.mode=1,0,0,false,"play"
end
local function place(first)
    local fx,fy=(first-1)%8,(first-1)//8
    local count=0
    for i=1,40 do
        local x,y=(i-1)%8,(i-1)//8
        if math.abs(x-fx)>1 or math.abs(y-fy)>1 then count=count+1;candidates[count]=i end
    end
    -- Sample without replacement from a finite list; there are no reroll loops.
    for n=1,7 do
        local chosen=roll(count)
        g.mines[candidates[chosen]]=true
        candidates[chosen]=candidates[count];count=count-1
    end
    for cell=1,40 do
        if g.mines[cell] then
            local x,y=(cell-1)%8,(cell-1)//8
            for dy=-1,1 do for dx=-1,1 do
                local nx,ny=x+dx,y+dy
                if nx>=0 and nx<8 and ny>=0 and ny<5 then
                    local index=ny*8+nx+1;g.numbers[index]=g.numbers[index]+1
                end
            end end
        end
    end
    g.placed=true
end
local function reveal()
    local first=g.cursor
    if g.flags[first] or g.open[first] then return end
    if not g.placed then place(first) end
    if g.mines[first] then g.mode="over";return end
    local head,tail=1,1;queue[1]=first;g.open[first]=true
    -- Mark when queued: each cell is visited at most once, without recursion.
    while head<=tail and head<=40 do
        local cell=queue[head];head=head+1;g.revealed=g.revealed+1
        if g.numbers[cell]==0 then
            local x,y=(cell-1)%8,(cell-1)//8
            for dy=-1,1 do for dx=-1,1 do
                local nx,ny=x+dx,y+dy
                if nx>=0 and nx<8 and ny>=0 and ny<5 then
                    local index=ny*8+nx+1
                    if not g.open[index] and not g.flags[index] and not g.mines[index] then
                        tail=tail+1;queue[tail]=index;g.open[index]=true
                    end
                end
            end end
        end
    end
    if g.revealed==33 then g.mode="won" end
end
function app.input(button,event)
    if event~="press" then return end
    if g.mode~="play" then if button=="ok" then restart() end;return end
    if button=="up" then g.cursor=(g.cursor+38)%40+1
    elseif button=="down" then g.cursor=g.cursor%40+1
    elseif button=="ok" then reveal()
    elseif button=="back" and not g.open[g.cursor] then
        if g.flags[g.cursor] then g.flags[g.cursor]=false;g.flagged=g.flagged-1
        elseif g.flagged<7 then g.flags[g.cursor]=true;g.flagged=g.flagged+1 end
    end
end
function app.draw()
    ui.clear();ui.text(0,0,"MINES")
    if g.mode=="help" then
        ui.text(0,12,"UP/DOWN move cursor")
        ui.text(0,24,"OK reveal BACK flag")
        ui.text(0,36,"First reveal is safe")
        ui.text(0,48,"Hold BACK exits")
        ui.text(0,56,"OK starts");return
    end
    for cell=1,40 do
        local x,y=2+(cell-1)%8*9,10+(cell-1)//8*9
        ui.rect(x,y,9,9)
        if g.open[cell] then
            if g.numbers[cell]>0 then ui.text(x+2,y+1,tostring(g.numbers[cell])) end
        elseif g.mode=="over" and g.mines[cell] then ui.text(x+2,y+1,"*")
        elseif g.flags[cell] then ui.text(x+2,y+1,"F")
        else ui.rect(x+3,y+3,3,3,true) end
    end
    local x,y=2+(g.cursor-1)%8*9,10+(g.cursor-1)//8*9
    ui.rect(x-1,y-1,11,11)
    ui.text(80,12,"F:"..tostring(g.flagged).."/7")
    ui.text(80,24,"Open:"..tostring(g.revealed))
    ui.text(80,36,g.mode=="over" and "BOOM" or g.mode=="won" and "YOU WIN" or "7 mines")
    if g.mode~="play" then ui.text(80,48,"OK retry") end
    ui.text(0,56,"UP/DN OKopen BKflag")
end
