#ifndef ESP_HACK_WALL_BUDGET_TEST_INC
#define ESP_HACK_WALL_BUDGET_TEST_INC
// Simulate time spent in a native framebuffer call, independently of Lua's
// instruction count. No production binding or real sleep is involved.
static FakeClock* budgetClock;
static int advance_budget_clock(lua_State* state) {
    const lua_Integer elapsed=luaL_checkinteger(state,1);
    if(elapsed<0||lua_Unsigned(elapsed)>UINT32_MAX) return luaL_error(state,"invalid fake duration");
    budgetClock->now+=uint32_t(elapsed);
    return 0;
}
static int budget_bindings(lua_State* state) {
    lua_pushcfunction(state,advance_budget_clock); lua_setglobal(state,"advance_budget_clock");
    return 0;
}
static bool budget_callback(LuaSandbox& vm,unsigned callback) {
    return callback==0?vm.draw():callback==1?vm.tick(33):vm.input("ok","press");
}
static bool wall_budget_tests() {
    alignas(16) uint8_t bank[LuaSandbox::kHeapBytes]; FakeClock clock={0,0}; budgetClock=&clock;
    LuaSandbox vm(bank,sizeof bank,clock_us,&clock); CHECK(vm.open(budget_bindings));
    CHECK(LuaSandbox::kWallUs==5000&&LuaSandbox::kDrawWallUs==10000);
    const uint32_t durations[]={4999,5000,6000,9999,10000,10001};
    for(size_t i=0;i<sizeof durations/sizeof *durations;++i) {
        char source[384]; const unsigned duration=unsigned(durations[i]);
        snprintf(source,sizeof source,
            "app={draw=function() advance_budget_clock(%u) end,"
            "tick=function() advance_budget_clock(%u) end,"
            "input=function() advance_budget_clock(%u) end}",duration,duration,duration);
        CHECK(load(vm,source));
        for(unsigned callback=0;callback<3;++callback) {
            // Every measurement crosses the 32-bit microsecond boundary.
            clock.now=UINT32_MAX-1000;
            const bool expected=duration<(callback==0?10000u:5000u);
            CHECK(budget_callback(vm,callback)==expected);
            CHECK(vm.instructions()==0); // Short callbacks exercise the return-time check.
            if(!expected) CHECK(strstr(vm.error(),"wall time quota"));
        }
        snprintf(source,sizeof source,"advance_budget_clock(%u)",duration);
        clock.now=UINT32_MAX-1000;
        CHECK(load(vm,source)==(duration<5000)); CHECK(vm.instructions()==0);
        if(duration>=5000) CHECK(strstr(vm.error(),"wall time quota"));
    }
    // A finite callback invokes multiple Lua hooks after native work has already
    // crossed 5 ms. Draw must still be allowed to complete below 10 ms.
    CHECK(load(vm,"app={draw=function() advance_budget_clock(6000);local n=0;"
        "for i=1,2000 do n=n+i end;assert(n==2001000) end}"));
    clock.now=UINT32_MAX-1000; CHECK(vm.draw()); CHECK(vm.instructions()>=500);
    CHECK(vm.instructions()<LuaSandbox::kInstructions);
    CHECK(load(vm,"app={draw=function() advance_budget_clock(10000);while true do end end}"));
    CHECK(!vm.draw()); CHECK(strstr(vm.error(),"execution quota"));
    CHECK(vm.instructions()>=500&&vm.instructions()<LuaSandbox::kInstructions);
    CHECK(load(vm,"app={tick=function() advance_budget_clock(5000);while true do end end,"
        "input=function() advance_budget_clock(5000);while true do end end}"));
    CHECK(!vm.tick(33)); CHECK(strstr(vm.error(),"execution quota"));
    CHECK(vm.instructions()>=500&&vm.instructions()<LuaSandbox::kInstructions);
    CHECK(!vm.input("ok","press")); CHECK(strstr(vm.error(),"execution quota"));
    CHECK(vm.instructions()>=500&&vm.instructions()<LuaSandbox::kInstructions);
    CHECK(!load(vm,"advance_budget_clock(5000);while true do end"));
    CHECK(strstr(vm.error(),"execution quota"));
    CHECK(vm.instructions()>=500&&vm.instructions()<LuaSandbox::kInstructions);
    // Frozen wall time isolates the unchanged instruction limit for runaway draw.
    CHECK(load(vm,"app={draw=function() while true do end end}"));
    CHECK(!vm.draw()); CHECK(strstr(vm.error(),"execution quota"));
    CHECK(vm.instructions()==100000); CHECK(vm.peak()<=sizeof bank);
    vm.close(); CHECK(vm.used()==0); budgetClock=nullptr;
    return true;
}
#endif
