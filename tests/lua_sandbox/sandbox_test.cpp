#include "lua_sandbox.h"
#include "../../src/interface/utf8_text.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <string>
#include <vector>
#include <stdlib.h>

using research::LuaArena;
using research::LuaSandbox;
static unsigned checks;
#define CHECK(test) do { ++checks; if(!(test)) { fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#test); return false; } } while(0)

struct FakeClock { uint32_t now, step; };
static uint32_t clock_us(void* opaque) {
    FakeClock& clock=*static_cast<FakeClock*>(opaque);
    const uint32_t now=clock.now; clock.now+=clock.step; return now;
}
struct TextDraw { int x, y; std::string value; };
struct RectDraw { int x, y, w, h; };
struct MockSdk {
    uint32_t millis, samples, starts, stops, drawCalls;
    bool quit, active, connected;
    std::string lastText;
    std::vector<TextDraw> texts;
    std::vector<RectDraw> rectangles;
    MockSdk():millis(0),samples(0),starts(0),stops(0),drawCalls(0),quit(false),active(false),connected(false) {}
};
static MockSdk* current;
static void drawing(lua_State* state) {
    if(++current->drawCalls>256) luaL_error(state,"drawing operation quota exceeded");
}
static int clear(lua_State* state) {
    drawing(state); current->texts.clear(); current->rectangles.clear(); return 0;
}
static int text(lua_State* state) {
    drawing(state);
    const int x=int(luaL_checkinteger(state,1)),y=int(luaL_checkinteger(state,2));
    size_t length; const char* value=luaL_checklstring(state,3,&length);
    const int scale=int(luaL_optinteger(state,4,1));
    if(length>160) return luaL_error(state,"text exceeds 160 bytes");
    size_t glyphs=0;
    if(!ui_text::validate(value,length,&glyphs)) return luaL_error(state,"unsupported or malformed UTF-8");
    if(scale<1||scale>2||x<0||y<0||x+int(glyphs)*6*scale>128||y+8*scale>64)
        return luaL_error(state,"sample text outside visible screen");
    current->lastText.assign(value,length);
    current->texts.push_back(TextDraw{x,y,current->lastText}); return 0;
}
static int line(lua_State* state) {
    drawing(state);
    for(int i=1;i<=4;++i) {
        const lua_Integer value=luaL_checkinteger(state,i);
        if(value<0||value>(i%2?127:63)) return luaL_error(state,"sample line outside screen");
    }
    return 0;
}
static int rect(lua_State* state) {
    drawing(state);
    const int x=int(luaL_checkinteger(state,1)),y=int(luaL_checkinteger(state,2));
    const int w=int(luaL_checkinteger(state,3)),h=int(luaL_checkinteger(state,4));
    if(x<0||y<0||w<0||h<0||x+w>128||y+h>64) return luaL_error(state,"sample rectangle outside screen");
    if(!lua_isnoneornil(state,5)&&!lua_isboolean(state,5)) return luaL_error(state,"filled must be boolean");
    current->rectangles.push_back(RectDraw{x,y,w,h}); return 0;
}
static int pixel(lua_State* state) {
    drawing(state); const int x=int(luaL_checkinteger(state,1)),y=int(luaL_checkinteger(state,2));
    if(x<0||x>127||y<0||y>63) return luaL_error(state,"pixel outside screen");
    return 0;
}
static int millis(lua_State* state) { lua_pushinteger(state,current->millis); return 1; }
static int quit(lua_State*) { current->quit=true; return 0; }
static int start_rx(lua_State* state) {
    const lua_Integer hz=luaL_checkinteger(state,1); const char* preset=luaL_checkstring(state,2);
    if(hz!=433920000||strcmp(preset,"AM270")) return luaL_error(state,"sample used an unexpected receive profile");
    ++current->starts; current->active=current->connected=true;
    lua_pushboolean(state,true); lua_pushnil(state); return 2;
}
static int stop(lua_State*) { ++current->stops; current->active=false; return 0; }
static void integer(lua_State* state,const char* name,lua_Integer value) { lua_pushinteger(state,value); lua_setfield(state,-2,name); }
static void boolean(lua_State* state,const char* name,bool value) { lua_pushboolean(state,value); lua_setfield(state,-2,name); }
static int snapshot(lua_State* state) {
    lua_createtable(state,0,8); boolean(state,"active",current->active); boolean(state,"connected",current->connected);
    boolean(state,"carrier",current->active); integer(state,"rssi_dbm",-90); integer(state,"frequency_hz",433920000);
    integer(state,"samples",++current->samples); integer(state,"spi_errors",0); integer(state,"fifo_errors",0); return 1;
}
static int bindings(lua_State* state) {
    const luaL_Reg ui[]={{"clear",clear},{"text",text},{"line",line},{"rect",rect},{"pixel",pixel},{nullptr,nullptr}};
    luaL_newlib(state,ui); integer(state,"width",128); integer(state,"height",64); lua_setglobal(state,"ui");
    const luaL_Reg clock[]={{"millis",millis},{nullptr,nullptr}}; luaL_newlib(state,clock); lua_setglobal(state,"clock");
    const luaL_Reg sdk[]={{"quit",quit},{nullptr,nullptr}}; luaL_newlib(state,sdk); lua_setglobal(state,"sdk");
    const luaL_Reg radio[]={{"start_rx",start_rx},{"stop",stop},{"snapshot",snapshot},{nullptr,nullptr}};
    luaL_newlib(state,radio); lua_setglobal(state,"radio"); return 0;
}
static bool load(LuaSandbox& vm,const char* source) { return vm.load(source,strlen(source),"test"); }
#include "wall_budget_test.inc"
#include "utf8_display_test.inc"
static bool allocator_tests() {
    alignas(16) uint8_t bank[4096]; LuaArena arena(bank,sizeof bank);
    void* a=arena.resize(nullptr,256); void* b=arena.resize(nullptr,512); void* c=arena.resize(nullptr,128);
    CHECK(a&&b&&c); memset(b,0xA5,512);
    CHECK(!arena.resize(b,SIZE_MAX)); CHECK(static_cast<uint8_t*>(b)[511]==0xA5);
    b=arena.resize(b,1024); CHECK(b); CHECK(static_cast<uint8_t*>(b)[511]==0xA5);
    a=arena.resize(a,64); CHECK(a);
    arena.resize(c,0); arena.resize(a,0); arena.resize(b,0); CHECK(arena.used()==0);
    void* large=arena.resize(nullptr,4000); CHECK(large); arena.resize(large,0);
    CHECK(arena.used()==0); CHECK(arena.peak()<=arena.capacity()); CHECK(arena.capacity()<=sizeof bank);
    return true;
}
static bool vm_tests() {
    struct Bank { uint8_t before[16]; alignas(16) uint8_t bytes[LuaSandbox::kHeapBytes]; uint8_t after[16]; } bank;
    memset(&bank,0xD3,sizeof bank); FakeClock clock={0,0};
    LuaSandbox vm(bank.bytes,sizeof bank.bytes,clock_us,&clock); CHECK(vm.open());
    CHECK(load(vm,"assert(io==nil and os==nil and package==nil and debug==nil and coroutine==nil);"
        "assert(load==nil and loadfile==nil and dofile==nil and pcall==nil and xpcall==nil);"
        "assert(print==nil and warn==nil and collectgarbage==nil and getmetatable==nil and setmetatable==nil);"
        "assert(string.dump==nil and string.rep==nil and string.find==nil and string.match==nil);"
        "assert(string.gmatch==nil and string.gsub==nil); app={} "));
    CHECK(vm.draw()); CHECK(vm.tick(33)); CHECK(vm.input("up","press"));
    CHECK(load(vm,"a={1,2,3};b={};table.move(a,1,3,2,b);assert(b[2]==1 and b[3]==2 and b[4]==3);"
        "table.move(a,1,2,2);assert(a[1]==1 and a[2]==1 and a[3]==2)"));
    CHECK(!load(vm,"table.move({},1,2147483647,1,{})")); CHECK(strstr(vm.error(),"1024 elements"));
    // A few hash keys force Lua's length search above 32 bits without exhausting the 48 KiB heap.
    CHECK(!load(vm,"local t={1,1,1,1,1,1,1,1};t[9]=1;t[16]=1;t[17]=1;for i=5,35 do t[2^i]=1 end;"
        "assert(#t>4294967295);table.insert(t,1,0)")); CHECK(strstr(vm.error(),"1024 elements"));
    CHECK(!load(vm,"local t={1,1,1,1,1,1,1,1};t[9]=1;t[16]=1;t[17]=1;for i=5,35 do t[2^i]=1 end;"
        "assert(#t>4294967295);table.remove(t,1)")); CHECK(strstr(vm.error(),"1024 elements"));
    CHECK(!load(vm,"table.move({},math.mininteger,math.maxinteger,1,{})")); CHECK(strstr(vm.error(),"1024 elements"));
    CHECK(load(vm,"app={tick=function(dt) assert(dt==33) end,input=function(b,e) assert(b=='up' and e=='press') end}"));
    CHECK(vm.tick(33)); CHECK(vm.input("up","press"));
    CHECK(load(vm,"app={draw=function() error('draw-failure') end}")); CHECK(!vm.draw()); CHECK(strstr(vm.error(),"draw-failure"));
    CHECK(load(vm,"app={tick=function() error('tick-failure') end}")); CHECK(!vm.tick(33)); CHECK(strstr(vm.error(),"tick-failure"));
    CHECK(load(vm,"app={input=function() error('input-failure') end}")); CHECK(!vm.input("ok","press")); CHECK(strstr(vm.error(),"input-failure"));
    CHECK(load(vm,"app={draw=function() error(123,0) end}")); CHECK(!vm.draw()); CHECK(strstr(vm.error(),"without an error string"));
    CHECK(load(vm,"app={draw=function() error({reason='table'},0) end}")); CHECK(!vm.draw()); CHECK(strstr(vm.error(),"without an error string"));
    CHECK(load(vm,"app=12")); CHECK(!vm.draw()); CHECK(strstr(vm.error(),"app must be a table"));
    CHECK(load(vm,"app={draw=12}")); CHECK(!vm.draw()); CHECK(strstr(vm.error(),"callback must be a function"));
    CHECK(load(vm,"app={draw=function() while true do end end}")); CHECK(!vm.draw());
    CHECK(strstr(vm.error(),"quota")); CHECK(vm.instructions()<=LuaSandbox::kInstructions);
    CHECK(!load(vm,"while true do end")); CHECK(strstr(vm.error(),"quota"));
    const std::string oversized(LuaSandbox::kSourceBytes+1,' ');
    CHECK(!vm.load(oversized.data(),oversized.size(),"large")); CHECK(strstr(vm.error(),"32 KiB"));
    CHECK(!vm.load("\x1bLua\x54\x00",6,"binary")); CHECK(strstr(vm.error(),"binary"));
    CHECK(load(vm,"app={tick=function() local t={} while true do t[#t+1]=string.format('%040d',#t) end end}"));
    CHECK(!vm.tick(33)); CHECK(strstr(vm.error(),"memory")); CHECK(vm.peak()<=sizeof bank.bytes);
    vm.close(); CHECK(vm.used()==0);
    CHECK(vm.open());
    CHECK(load(vm,"reserve={};for i=1,210 do reserve[i]=string.format('%050d',i) end;app={draw=function() error(123,0) end}"));
    CHECK(vm.used()>LuaSandbox::kHeapBytes*3/4); CHECK(!vm.draw()); CHECK(strstr(vm.error(),"without an error string"));
    CHECK(load(vm,"app={draw=function() error({},0) end}")); CHECK(!vm.draw()); CHECK(strstr(vm.error(),"without an error string"));
    vm.close(); CHECK(vm.used()==0);
    for(unsigned i=0;i<16;++i) { CHECK(bank.before[i]==0xD3); CHECK(bank.after[i]==0xD3); }
    for(unsigned i=0;i<25;++i) { CHECK(vm.open()); CHECK(load(vm,"app={draw=function() return end}")); CHECK(vm.draw()); vm.close(); CHECK(vm.used()==0); }
    FakeClock moving={UINT32_MAX-2000,1500}; LuaSandbox wall(bank.bytes,sizeof bank.bytes,clock_us,&moving);
    CHECK(wall.open()); CHECK(!load(wall,"while true do end")); CHECK(strstr(wall.error(),"quota")); CHECK(wall.instructions()<LuaSandbox::kInstructions);
    wall.close(); CHECK(wall.used()==0);
    return true;
}
static bool read_file(const char* path,std::string& output) {
    FILE* file=fopen(path,"rb"); if(!file) return false;
    char block[4096]; size_t size; while((size=fread(block,1,sizeof block,file))) output.append(block,size);
    const bool ok=!ferror(file); fclose(file); return ok;
}
static bool has_text(const MockSdk& mock,const char* value) {
    for(size_t i=0;i<mock.texts.size();++i) {
        if(mock.texts[i].value.find(value)!=std::string::npos) return true;
    }
    return false;
}
static bool has_rect(const MockSdk& mock,int x,int y,int w,int h) {
    for(size_t i=0;i<mock.rectangles.size();++i) {
        const RectDraw& r=mock.rectangles[i];
        if(r.x==x&&r.y==y&&r.w==w&&r.h==h) return true;
    }
    return false;
}
static unsigned snake_cells(const MockSdk& mock) {
    unsigned count=0;
    for(size_t i=0;i<mock.rectangles.size();++i) {
        if(mock.rectangles[i].w==4&&mock.rectangles[i].h==4) ++count;
    }
    return count;
}
static bool draw_frame(LuaSandbox& vm,MockSdk& mock) {
    mock.drawCalls=0;
    CHECK(vm.draw()); CHECK(vm.instructions()<=LuaSandbox::kInstructions);
    return true;
}
static bool snake_game_test(const char* path) {
    std::string source; CHECK(read_file(path,source));
    alignas(16) uint8_t bank[LuaSandbox::kHeapBytes]; FakeClock clock={0,0}; MockSdk mock; current=&mock;
    LuaSandbox vm(bank,sizeof bank,clock_us,&clock); CHECK(vm.open(bindings));
    CHECK(vm.load(source.data(),source.size(),path)); CHECK(draw_frame(vm,mock));
    CHECK(has_text(mock,"UP поворот влево")); CHECK(has_text(mock,"DN поворот вправо"));
    CHECK(vm.input("ok","release")); CHECK(draw_frame(vm,mock)); CHECK(has_text(mock,"OK пуск"));
    CHECK(vm.input("ok","press")); CHECK(draw_frame(vm,mock)); CHECK(snake_cells(mock)==3);
    CHECK(has_rect(mock,41,32,4,4));
    CHECK(vm.input("ok","press")); mock.millis+=500; CHECK(vm.tick(33));
    CHECK(draw_frame(vm,mock)); CHECK(has_text(mock,"ПАУЗА")); CHECK(has_rect(mock,31,32,4,4));
    CHECK(vm.input("ok","press")); mock.millis+=179; CHECK(vm.tick(33));
    CHECK(draw_frame(vm,mock)); CHECK(!has_rect(mock,46,32,4,4));
    ++mock.millis; CHECK(vm.tick(33)); CHECK(draw_frame(vm,mock));
    CHECK(has_rect(mock,46,32,4,4)); CHECK(!has_rect(mock,31,32,4,4));
    CHECK(vm.input("up","press")); CHECK(vm.input("down","press"));
    mock.millis+=180; CHECK(vm.tick(33)); CHECK(draw_frame(vm,mock));
    CHECK(has_rect(mock,46,27,4,4)); // Only one queued turn is accepted per movement.

    vm.close(); mock.millis=0; CHECK(vm.open(bindings));
    CHECK(vm.load(source.data(),source.size(),path)); CHECK(vm.input("ok","press"));
    // Deterministic seed zero puts food at (23,0), reachable from the initial head.
    for(unsigned i=0;i<15;++i) { mock.millis+=180; CHECK(vm.tick(33)); }
    CHECK(vm.input("up","press"));
    for(unsigned i=0;i<4;++i) { mock.millis+=180; CHECK(vm.tick(33)); }
    CHECK(draw_frame(vm,mock)); CHECK(has_text(mock,"ЗМЕЙКА 1")); CHECK(snake_cells(mock)==4);
    mock.millis+=180; CHECK(vm.tick(33)); CHECK(draw_frame(vm,mock)); CHECK(has_text(mock,"ПРОИГРЫШ"));
    CHECK(vm.input("ok","press")); CHECK(draw_frame(vm,mock));
    CHECK(snake_cells(mock)==3); CHECK(has_text(mock,"ЗМЕЙКА 0")); CHECK(!has_text(mock,"ПРОИГРЫШ"));

    vm.close(); mock.millis=UINT32_MAX-100; CHECK(vm.open(bindings));
    CHECK(vm.load(source.data(),source.size(),path)); CHECK(vm.input("ok","press"));
    mock.millis+=80; CHECK(vm.tick(33)); CHECK(draw_frame(vm,mock)); CHECK(!has_rect(mock,46,32,4,4));
    mock.millis+=100; CHECK(vm.tick(33)); CHECK(draw_frame(vm,mock)); CHECK(has_rect(mock,46,32,4,4));
    CHECK(vm.input("back","press")); CHECK(mock.quit); CHECK(mock.starts==0&&mock.stops==0);
    CHECK(vm.peak()<=sizeof bank); vm.close(); CHECK(vm.used()==0);
    return true;
}
static void read_2048(const MockSdk& mock,unsigned board[16],unsigned& score,unsigned& moves) {
    memset(board,0,16*sizeof *board); score=moves=0;
    for(size_t i=0;i<mock.texts.size();++i) {
        const TextDraw& text=mock.texts[i];
        const size_t s=text.value.find("С:");
        if(s!=std::string::npos) score=unsigned(strtoul(text.value.c_str()+s+strlen("С:"),nullptr,10));
        if(text.x==86&&text.y==0) moves=unsigned(strtoul(text.value.c_str()+strlen("Х:"),nullptr,10));
        if(text.y>=13&&text.y<=43&&(text.y-13)%10==0) {
            unsigned value=unsigned(strtoul(text.value.c_str(),nullptr,10));
            if(text.value.find('K')!=std::string::npos) value*=1024;
            if(text.value.find('M')!=std::string::npos) value*=1048576;
            board[((text.y-13)/10)*4+text.x/32]=value;
        }
    }
}
static bool game2048_test(const char* path) {
    std::string source; CHECK(read_file(path,source));
    alignas(16) uint8_t bank[LuaSandbox::kHeapBytes]; FakeClock clock={0,0}; MockSdk mock; current=&mock;
    LuaSandbox vm(bank,sizeof bank,clock_us,&clock); CHECK(vm.open(bindings));
    CHECK(vm.load(source.data(),source.size(),path)); CHECK(draw_frame(vm,mock));
    CHECK(has_text(mock,"Выход: держите BK")); CHECK(has_text(mock,"OK вправо BK влево"));
    CHECK(vm.input("ok","press")); CHECK(draw_frame(vm,mock));
    unsigned before[16],scoreBefore,movesBefore; read_2048(mock,before,scoreBefore,movesBefore);
    unsigned initialTiles=0; for(unsigned i=0;i<16;++i) if(before[i]) ++initialTiles;
    CHECK(initialTiles==2); CHECK(scoreBefore==0&&movesBefore==0);
    unsigned merges=0,ends=0,successful=0,seed=1;
    const char* buttons[]={"up","down","ok","back"};
    for(unsigned turn=0;turn<1200;++turn) {
        const bool over=has_text(mock,"ПРОИГРЫШ");
        if(over) {
            ++ends; CHECK(vm.input("ok","press")); CHECK(draw_frame(vm,mock));
            read_2048(mock,before,scoreBefore,movesBefore); CHECK(scoreBefore==0&&movesBefore==0);
        }
        seed=seed*1664525U+1013904223U;
        const char* button=buttons[(seed>>28)%4];
        CHECK(vm.input(button,"release")); CHECK(vm.input(button,"hold")); CHECK(draw_frame(vm,mock));
        unsigned unchanged[16],s,m; read_2048(mock,unchanged,s,m);
        CHECK(!memcmp(before,unchanged,sizeof before)); CHECK(s==scoreBefore&&m==movesBefore);
        CHECK(vm.input(button,"press")); CHECK(draw_frame(vm,mock));
        unsigned after[16],scoreAfter,movesAfter; read_2048(mock,after,scoreAfter,movesAfter);
        unsigned massBefore=0,massAfter=0;
        for(unsigned i=0;i<16;++i) {
            massBefore+=before[i]; massAfter+=after[i];
            CHECK(after[i]==0||(after[i]>=2&&(after[i]&(after[i]-1))==0));
        }
        if(movesAfter==movesBefore) {
            CHECK(!memcmp(before,after,sizeof before)); CHECK(scoreAfter==scoreBefore);
        } else {
            ++successful; CHECK(movesAfter==movesBefore+1);
            CHECK(massAfter==massBefore+2||massAfter==massBefore+4);
            CHECK(scoreAfter>=scoreBefore); if(scoreAfter>scoreBefore) ++merges;
        }
        if(has_text(mock,"ПРОИГРЫШ")) {
            for(unsigned i=0;i<16;++i) {
                CHECK(after[i]!=0);
                if(i%4!=3) CHECK(after[i]!=after[i+1]);
                if(i<12) CHECK(after[i]!=after[i+4]);
            }
        }
        memcpy(before,after,sizeof before); scoreBefore=scoreAfter; movesBefore=movesAfter;
    }
    CHECK(successful>500); CHECK(merges>100); CHECK(ends>0); CHECK(!mock.quit);
    CHECK(mock.starts==0&&mock.stops==0); CHECK(vm.peak()<=sizeof bank);
    vm.close(); CHECK(vm.used()==0);
    return true;
}
static bool example_test(const char* path,unsigned kind) {
    std::string source; CHECK(read_file(path,source)); CHECK(source.size()<=LuaSandbox::kSourceBytes);
    alignas(16) uint8_t bank[LuaSandbox::kHeapBytes]; FakeClock clock={0,0}; MockSdk mock; current=&mock;
    LuaSandbox vm(bank,sizeof bank,clock_us,&clock); CHECK(vm.open(bindings));
    CHECK(vm.load(source.data(),source.size(),path));
    for(unsigned frame=0;frame<1500;++frame) {
        mock.millis+=33;
        if(frame%90==0) { mock.drawCalls=0; CHECK(vm.input("ok","press")); CHECK(vm.input("ok","release")); }
        if(frame%150==0) { mock.drawCalls=0; CHECK(vm.input("up","press")); CHECK(vm.input("up","hold")); CHECK(vm.input("up","release")); }
        if(frame%210==0) { mock.drawCalls=0; CHECK(vm.input("down","press")); CHECK(vm.input("down","release")); }
        if(kind==3&&frame%270==0) { CHECK(vm.input("back","press")); CHECK(vm.input("back","release")); }
        mock.drawCalls=0; CHECK(vm.tick(33)); mock.drawCalls=0;
        if(!vm.draw()) { fprintf(stderr,"Sample %s: %s\n",path,vm.error()); return false; }
        ++checks; CHECK(vm.peak()<=sizeof bank);
    }
    // Exercise millis wrap while callbacks continue with a bounded displayed value.
    mock.millis=UINT32_MAX-20; CHECK(vm.tick(33)); mock.millis=12; CHECK(vm.tick(33));
    mock.drawCalls=0; CHECK(vm.draw());
    CHECK(vm.input("back","press")); CHECK(kind==3 ? !mock.quit : mock.quit);
    CHECK(kind==1 ? (mock.starts>0&&mock.stops>0) : (mock.starts==0&&mock.stops==0));
    printf("Lua sample %s: peak %u/%u bytes\n",path,unsigned(vm.peak()),unsigned(sizeof bank));
    vm.close(); CHECK(vm.used()==0); return true;
}
#include "utilities_test.inc"
#include "games_extra_test.inc"
int main(int argc,char** argv) {
    if(argc!=12) { fprintf(stderr,"Pass paths for all eleven shipped Lua apps\n"); return 2; }
    if(!allocator_tests()||!vm_tests()||!wall_budget_tests()||!utf8_display_tests()) return 1;
    for(int i=1;i<argc;++i) {
        const unsigned kind=i==3?1:(i==1||i==2||i==4?0:3);
        if(!example_test(argv[i],kind)) return 1;
    }
    if(!snake_game_test(argv[4])||!game2048_test(argv[5])||
       !utility_tests(argv[6],argv[7],argv[8],argv[9])||
       !blockfall_game_test(argv[10])||!mines_game_test(argv[11])) return 1;
    printf("Lua sandbox: %u checks passed (real Lua VM/allocator, quotas, APIs, callbacks, %u sample frames)\n",checks,unsigned(argc-1)*1500);
    return 0;
}
