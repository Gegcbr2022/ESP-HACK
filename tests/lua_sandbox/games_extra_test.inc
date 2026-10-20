#ifndef ESP_HACK_GAMES_EXTRA_TEST_INC
#define ESP_HACK_GAMES_EXTRA_TEST_INC
static bool extra_lua(LuaSandbox& vm,const char* source) {
    // Fixture assertions compile additional chunks which are not part of an app.
    // Release previous assertion chunks before compiling another: otherwise GC
    // pacing/hash layout can make the 48 KiB bank fail on harness-only garbage.
    // The separate 1,500-frame app runs use normal automatic GC throughout.
    lua_gc(vm.state(),LUA_GCCOLLECT,0);
    const bool ok=load(vm,source);
    if(!ok) fprintf(stderr,"Game state fixture: %s\n",vm.error());
    CHECK(ok); CHECK(vm.instructions()<=LuaSandbox::kInstructions);
    return true;
}
static bool blockfall_game_test(const char* path) {
    std::string source; CHECK(read_file(path,source));
    alignas(16) uint8_t bank[LuaSandbox::kHeapBytes]; FakeClock clock={0,0}; MockSdk mock; current=&mock;
    LuaSandbox vm(bank,sizeof bank,clock_us,&clock); CHECK(vm.open(bindings));
    CHECK(vm.load(source.data(),source.size(),path)); CHECK(utility_text(vm,mock,"Hold DOWN hard drop"));
    CHECK(utility_press(vm,"ok"));
    CHECK(extra_lua(vm,"g=app.state;assert(g.mode=='play' and #g.board==16 and g.lines==0);"
        "local seen={};for n=1,7 do assert(g.kind>=1 and g.kind<=7 and not seen[g.kind]);seen[g.kind]=true;"
        "for r=1,16 do g.board[r]=0 end;app.input('down','hold');"
        "local cells=0;for r=1,16 do assert(g.board[r]>=0 and g.board[r]<=1023);"
        "for c=0,9 do if g.board[r]&(1<<c)~=0 then cells=cells+1 end end end;assert(cells==4) end"));
    CHECK(extra_lua(vm,"function fixture(k,r,x,y) for i=1,16 do g.board[i]=0 end;"
        "g.kind,g.rotation,g.x,g.y,g.mode,g.score,g.lines,g.level,g.elapsed=k,r,x,y,'play',0,0,1,0 end;"
        "fixture(2,1,0,3)"));
    CHECK(utility_press(vm,"back",20)); CHECK(extra_lua(vm,"assert(g.x==-1)"));
    CHECK(utility_press(vm,"ok",20)); CHECK(extra_lua(vm,"assert(g.x==7)"));
    CHECK(extra_lua(vm,"fixture(1,2,7,5);app.input('up','press');assert(g.rotation==3 and g.x==6)"));
    CHECK(extra_lua(vm,"fixture(1,1,3,14);g.board[16]=1023~(15<<3);app.input('down','press');"
        "assert(g.lines==1 and g.score==100);for i=1,16 do assert(g.board[i]==0) end"));
    CHECK(extra_lua(vm,"fixture(2,1,3,13);g.board[15]=1023~(3<<4);g.board[16]=g.board[15];"
        "app.input('down','press');assert(g.lines==2 and g.score==300)"));
    CHECK(extra_lua(vm,"fixture(1,2,1,12);for i=13,16 do g.board[i]=1023~8 end;"
        "app.input('down','press');assert(g.lines==4 and g.score==800)"));
    CHECK(extra_lua(vm,"fixture(1,1,3,14);g.lines=9;g.board[16]=1023~(15<<3);app.input('down','press');"
        "assert(g.lines==10 and g.level==2 and g.score==100)"));
    CHECK(extra_lua(vm,"fixture(2,1,3,13);g.lines=9998;g.level=20;g.score=9999998;"
        "g.board[15]=1023~(3<<4);g.board[16]=g.board[15];app.input('down','press');"
        "assert(g.lines==9999 and g.level==20 and g.score==9999999)"));
    CHECK(extra_lua(vm,"fixture(2,1,3,-2);g.board[1]=1023;app.input('down','press');"
        "assert(g.mode=='over' and g.board[1]==1023);for i=2,16 do assert(g.board[i]==0) end"));
    CHECK(utility_text(vm,mock,"GAME OVER")); CHECK(utility_press(vm,"ok"));
    CHECK(extra_lua(vm,"assert(g.mode=='play' and g.score==0 and g.lines==0);"
        "fixture(2,1,3,3);g.level=20;app.tick(-20);assert(g.elapsed==0 and g.y==3);"
        "app.tick(4294967295);assert(g.elapsed==100 and g.y==3);app.tick(100);assert(g.y==4 and g.elapsed==70)"));
    CHECK(extra_lua(vm,"g.mode='over';g.score=9999999;g.lines=9999;for i=1,16 do g.board[i]=1023 end"));
    CHECK(draw_frame(vm,mock)); CHECK(mock.drawCalls<256); CHECK(!mock.quit&&mock.starts==0&&mock.stops==0);
    CHECK(vm.peak()<=sizeof bank);
    printf("Lua game blockfall: peak %u/%u bytes\n",unsigned(vm.peak()),unsigned(sizeof bank));
    vm.close(); CHECK(vm.used()==0); return true;
}
static bool mines_game_test(const char* path) {
    std::string source; CHECK(read_file(path,source));
    alignas(16) uint8_t bank[LuaSandbox::kHeapBytes]; FakeClock clock={0,0}; MockSdk mock; current=&mock;
    LuaSandbox vm(bank,sizeof bank,clock_us,&clock); CHECK(vm.open(bindings));
    CHECK(vm.load(source.data(),source.size(),path)); CHECK(utility_text(vm,mock,"First reveal is safe"));
    CHECK(utility_press(vm,"ok"));
    CHECK(extra_lua(vm,"g=app.state;assert(g.mode=='play' and not g.placed);"
        "app.input('up','press');assert(g.cursor==40);app.input('down','press');assert(g.cursor==1);"
        "app.input('back','press');app.input('ok','press');assert(g.flags[1] and g.revealed==0 and not g.placed);"
        "app.input('back','press');assert(g.flagged==0)"));
    for(unsigned first=1;first<=40;++first) {
        char prefix[160]; snprintf(prefix,sizeof prefix,"g.mode='over';app.input('ok','press');g.random=%u;g.cursor=%u;app.input('ok','press');",first*1009,first);
        const std::string test=std::string(prefix)+
            "assert(g.placed and g.mode~='over' and g.open[g.cursor] and g.numbers[g.cursor]==0);"
            "local mines,opened=0,0;local fx,fy=(g.cursor-1)%8,(g.cursor-1)//8;"
            "for i=1,40 do local x,y=(i-1)%8,(i-1)//8;"
            "if g.mines[i] then mines=mines+1 end;if g.open[i] then opened=opened+1;assert(not g.mines[i]) end;"
            "if math.abs(x-fx)<=1 and math.abs(y-fy)<=1 then assert(not g.mines[i]) end;"
            "if not g.mines[i] then local n=0;for dy=-1,1 do for dx=-1,1 do local nx,ny=x+dx,y+dy;"
            "if nx>=0 and nx<8 and ny>=0 and ny<5 and g.mines[ny*8+nx+1] then n=n+1 end end end;"
            "assert(g.numbers[i]==n) end end;assert(mines==7 and opened==g.revealed and opened<=33);"
            "local old=g.revealed;app.input('ok','press');assert(g.revealed==old)";
        CHECK(extra_lua(vm,test.c_str())); CHECK(draw_frame(vm,mock));
    }
    CHECK(extra_lua(vm,"g.mode='over';app.input('ok','press');for i=1,8 do g.cursor=i;app.input('back','press') end;"
        "assert(g.flagged==7 and not g.flags[8]);g.cursor=1;app.input('back','press');g.cursor=8;"
        "app.input('back','press');assert(g.flagged==7 and g.flags[8])"));
    CHECK(extra_lua(vm,"g.mode='over';app.input('ok','press');app.input('ok','press');"
        "for i=1,40 do if g.mines[i] then g.cursor=i;break end end;"
        "app.input('back','press');app.input('ok','press');assert(g.mode~='over');"
        "app.input('back','press');app.input('ok','press');assert(g.mode=='over')"));
    CHECK(utility_text(vm,mock,"BOOM"));
    CHECK(extra_lua(vm,"app.input('ok','press');assert(not g.placed and g.revealed==0 and g.flagged==0);"
        "app.input('ok','press');for i=1,40 do if not g.mines[i] and not g.open[i] then "
        "g.cursor=i;app.input('ok','press') end end;assert(g.mode=='won' and g.revealed==33)"));
    CHECK(utility_text(vm,mock,"YOU WIN")); CHECK(utility_press(vm,"ok"));
    CHECK(extra_lua(vm,"assert(g.mode=='play' and not g.placed and g.revealed==0);"
        "local c=g.cursor;app.input('down','hold');app.input('down','release');assert(g.cursor==c)"));
    CHECK(!mock.quit&&mock.starts==0&&mock.stops==0); CHECK(vm.peak()<=sizeof bank);
    printf("Lua game mines: peak %u/%u bytes\n",unsigned(vm.peak()),unsigned(sizeof bank));
    vm.close(); CHECK(vm.used()==0); return true;
}
#endif
