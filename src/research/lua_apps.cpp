#include "lua_apps.h"
#include "lua_sandbox.h"
#include "app_catalog.h"
#include "lab_service.h"
#include "../subghz.h"
#include "../interface/menu/pocket_menu.h"
#include <Arduino.h>
#include <SD.h>
#include <esp_heap_caps.h>
#include <ctype.h>

void returnToMainMenu();

namespace {
using research::LuaSandbox;
struct Runtime {
    bool quit, ownsRadio, changedRadio, diagnostic;
    uint32_t radioFrequency, lastRadioChange;
    unsigned drawCalls;
    char radioPreset[12];
};
Runtime* activeApp;
research::apps::Entry* appEntries;
research::apps::Preferences appPreferences={};
size_t appCount;
uint32_t clockUs(void*) { return micros(); }

int coordinate(lua_State* state, int index, int minimum, int maximum) {
    const lua_Integer value = luaL_checkinteger(state, index);
    if(value < minimum || value > maximum) luaL_error(state, "coordinate outside screen bounds");
    return int(value);
}
void drawing(lua_State* state) {
    if(!activeApp || ++activeApp->drawCalls > 256) luaL_error(state, "drawing operation quota exceeded");
}
int uiClear(lua_State* state) { drawing(state); display.setFont(nullptr); display.clearDisplay(); return 0; }
int uiText(lua_State* state) {
    drawing(state);
    const int x = coordinate(state, 1, -128, 255), y = coordinate(state, 2, -64, 127);
    size_t length = 0; const char* text = luaL_checklstring(state, 3, &length);
    if(length > 160) return luaL_error(state, "text exceeds 160 bytes");
    for(size_t i = 0; i < length; ++i) if(uint8_t(text[i]) < 32 || uint8_t(text[i]) > 126)
        return luaL_error(state, "text must be printable ASCII");
    const int scale = lua_isnoneornil(state, 4) ? 1 : coordinate(state, 4, 1, 2);
    display.setTextSize(scale); display.setTextWrap(false); display.setTextColor(SH110X_WHITE);
    display.setCursor(x, y); display.write(reinterpret_cast<const uint8_t*>(text), length);
    return 0;
}
int uiLine(lua_State* state) {
    drawing(state);
    const int x1 = coordinate(state, 1, -128, 255), y1 = coordinate(state, 2, -64, 127);
    const int x2 = coordinate(state, 3, -128, 255), y2 = coordinate(state, 4, -64, 127);
    display.drawLine(x1, y1, x2, y2, SH110X_WHITE); return 0;
}
int uiRect(lua_State* state) {
    drawing(state);
    const int x = coordinate(state, 1, -128, 255), y = coordinate(state, 2, -64, 127);
    const int width = coordinate(state, 3, 0, 128), height = coordinate(state, 4, 0, 64);
    if(!lua_isnoneornil(state, 5) && !lua_isboolean(state, 5)) return luaL_error(state, "filled must be boolean");
    if(lua_toboolean(state, 5)) display.fillRect(x, y, width, height, SH110X_WHITE);
    else display.drawRect(x, y, width, height, SH110X_WHITE);
    return 0;
}
int uiPixel(lua_State* state) {
    drawing(state);
    const int x = coordinate(state, 1, 0, 127), y = coordinate(state, 2, 0, 63);
    display.drawPixel(x, y, SH110X_WHITE); return 0;
}
int clockMillis(lua_State* state) { lua_pushinteger(state, millis()); return 1; }
int quit(lua_State*) { if(activeApp) activeApp->quit = true; return 0; }
int radioResult(lua_State* state, bool success, const char* message) {
    lua_pushboolean(state, success); if(message) lua_pushstring(state, message); else lua_pushnil(state); return 2;
}
int radioStart(lua_State* state) {
    if(!activeApp) return radioResult(state, false, "no active app");
    if(activeApp->diagnostic) return radioResult(state, false, "radio start disabled during self-test");
    const lua_Integer requested = luaL_checkinteger(state, 1);
    if(requested < 300000000 || requested > 928000000 || !research::supportedFrequency(uint32_t(requested)))
        return radioResult(state, false, "outside CC1101 hardware bands");
    size_t length = 0; const char* name = luaL_checklstring(state, 2, &length);
    if(length > 10 || strlen(name) != length || !research::findPreset(name)) return radioResult(state, false, "unknown named preset");
    const research::LabSnapshot current = research::labSnapshot();
    if(current.active) {
        if(activeApp->ownsRadio && activeApp->radioFrequency == uint32_t(requested) && !strcmp(activeApp->radioPreset, name))
            return radioResult(state, true, nullptr);
        return radioResult(state, false, "radio busy; stop and wait for inactive");
    }
    const uint32_t now = millis();
    if(activeApp->changedRadio && uint32_t(now - activeApp->lastRadioChange) < 250)
        return radioResult(state, false, "radio controls limited to four per second");
    research::RegisterImage image; research::RadioSettings settings;
    if(!research::loadPreset(name, image) || !research::describeRadio(image, uint32_t(requested), settings))
        return radioResult(state, false, "unsupported preset");
    settings.rxTimeoutMs = 3600000;
    const bool started = research::labStart(research::LabMode::Monitor, image, settings, research::TxProfile::ReceiveOnly, 0);
    if(started) {
        activeApp->ownsRadio = true; activeApp->changedRadio = true;
        activeApp->lastRadioChange = now; activeApp->radioFrequency = uint32_t(requested);
        snprintf(activeApp->radioPreset, sizeof(activeApp->radioPreset), "%s", name);
    }
    return radioResult(state, started, started ? nullptr : "radio worker could not start");
}
int radioStop(lua_State*) { if(activeApp && activeApp->ownsRadio) research::labRequestStop(); return 0; }
void field(lua_State* state, const char* name, lua_Integer value) { lua_pushinteger(state, value); lua_setfield(state, -2, name); }
int radioSnapshot(lua_State* state) {
    const research::LabSnapshot sample = research::labSnapshot();
    lua_createtable(state, 0, 8);
    lua_pushboolean(state, sample.active); lua_setfield(state, -2, "active");
    lua_pushboolean(state, sample.connected); lua_setfield(state, -2, "connected");
    lua_pushboolean(state, sample.carrier); lua_setfield(state, -2, "carrier");
    field(state, "rssi_dbm", sample.rssiDbm); field(state, "frequency_hz", sample.frequencyHz);
    field(state, "samples", sample.samples); field(state, "spi_errors", sample.spiErrors + sample.misoTimeouts);
    field(state, "fifo_errors", sample.fifoErrors); return 1;
}
int installBindings(lua_State* state) {
    const luaL_Reg ui[] = {{"clear",uiClear},{"text",uiText},{"line",uiLine},{"rect",uiRect},{"pixel",uiPixel},{nullptr,nullptr}};
    luaL_newlib(state, ui); field(state, "width", 128); field(state, "height", 64); lua_setglobal(state, "ui");
    const luaL_Reg clock[] = {{"millis",clockMillis},{nullptr,nullptr}};
    luaL_newlib(state, clock); lua_setglobal(state, "clock");
    const luaL_Reg sdk[] = {{"quit",quit},{nullptr,nullptr}};
    luaL_newlib(state, sdk); lua_setglobal(state, "sdk");
    const luaL_Reg radio[] = {{"start_rx",radioStart},{"stop",radioStop},{"snapshot",radioSnapshot},{nullptr,nullptr}};
    luaL_newlib(state, radio); lua_setglobal(state, "radio"); return 0;
}
void resetButtons() { buttonUp.resetStates(); buttonDown.resetStates(); buttonOK.resetStates(); buttonBack.resetStates(); }
void heading(const char* title) {
    display.clearDisplay(); display.setTextSize(1); display.setTextWrap(false); display.setTextColor(SH110X_WHITE);
    display.fillRect(0, 0, 128, 11, SH110X_WHITE); display.setTextColor(SH110X_BLACK);
    display.setCursor(2, 2); display.print(title); display.setTextColor(SH110X_WHITE);
}
void notice(const char* title, const char* text) {
    heading(title); display.setTextWrap(true); display.setCursor(0, 15); display.print(text);
    display.setTextWrap(false); display.setCursor(0, 56); display.print("BACK returns"); display.display();
    while(digitalRead(BUTTON_BACK) == LOW) delay(2);
    while(digitalRead(BUTTON_BACK) != LOW) delay(2);
    while(digitalRead(BUTTON_BACK) == LOW) delay(2);
    resetButtons();
}
bool validName(const char* name) {
    const size_t length = name ? strlen(name) : 0;
    if(length < 5 || length > 40 || strcmp(name + length - 4, ".lua")) return false;
    for(size_t i = 0; i < length - 4; ++i)
        if(!((name[i] >= 'a' && name[i] <= 'z') || (name[i] >= 'A' && name[i] <= 'Z') ||
             (name[i] >= '0' && name[i] <= '9') || name[i] == '_' || name[i] == '-')) return false;
    return true;
}
void listApps() {
    appCount = 0; File directory = SD.open("/apps");
    if(!appEntries || !directory || !directory.isDirectory()) return;
    while(appCount < 32) {
        File item = directory.openNextFile(); if(!item) break;
        const char* path = item.name(); const char* slash = strrchr(path, '/'); const char* name = slash ? slash + 1 : path;
        if(!item.isDirectory() && validName(name)) {
            research::apps::Entry& entry=appEntries[appCount++];
            research::apps::initialize(entry,name,item.size());
            char source[1024];const int n=item.read(reinterpret_cast<uint8_t*>(source),sizeof source);
            if(n>0&&size_t(n)<=sizeof source)research::apps::metadata(entry,source,size_t(n));
        }
        item.close();
    }
    directory.close();
    for(size_t i = 1; i < appCount; ++i) for(size_t j = i; j && strcmp(appEntries[j-1].title, appEntries[j].title) > 0; --j) {
        research::apps::Entry temporary=appEntries[j-1];appEntries[j-1]=appEntries[j];appEntries[j]=temporary;
    }
}
void readAppPreferences() {
    memset(&appPreferences,0,sizeof appPreferences);
    if(!SD.exists("/apps/.launcher.cfg")&&SD.exists("/apps/.launcher.bak"))SD.rename("/apps/.launcher.bak","/apps/.launcher.cfg");
    File file=SD.open("/apps/.launcher.cfg",FILE_READ);
    if(!file||file.size()>1024)return;
    char line[48];size_t used=0;bool header=false,overflow=false;
    while(file.available()) {
        const char c=file.read();if(c=='\r')continue;
        if(c=='\n') {
            line[used]=0;
            if(!header){if(strcmp(line,"APP-LAUNCHER1"))break;header=true;}
            else if(!overflow)research::apps::preferenceLine(appPreferences,line);
            used=0;overflow=false;
        } else if(used+1<sizeof line)line[used++]=c;else overflow=true;
    }
    file.close();
}
bool saveAppPreferences() {
    const char* path="/apps/.launcher.cfg";const char* temporary="/apps/.launcher.tmp";const char* previous="/apps/.launcher.bak";
    if(SD.exists(temporary)&&!SD.remove(temporary))return false;
    File file=SD.open(temporary,FILE_WRITE);if(!file)return false;
    bool good=file.print("APP-LAUNCHER1\n")==14;
    char line[45];
    for(size_t i=0;i<appPreferences.favoriteCount;++i) {
        const int n=snprintf(line,sizeof line,"F %s\n",appPreferences.favorites[i]);good=good&&file.write(reinterpret_cast<uint8_t*>(line),n)==size_t(n);
    }
    for(size_t i=0;i<appPreferences.recentCount;++i) {
        const int n=snprintf(line,sizeof line,"R %s\n",appPreferences.recent[i]);good=good&&file.write(reinterpret_cast<uint8_t*>(line),n)==size_t(n);
    }
    file.flush();file.close();
    if(!good){SD.remove(temporary);return false;}
    const bool hadPrevious=SD.exists(path);
    if(hadPrevious&&SD.exists(previous)&&!SD.remove(previous)){SD.remove(temporary);return false;}
    if(hadPrevious&&!SD.rename(path,previous)){SD.remove(temporary);return false;}
    if(!SD.rename(temporary,path)){if(hadPrevious)SD.rename(previous,path);SD.remove(temporary);return false;}
    if(hadPrevious)SD.remove(previous);
    return true;
}
void releaseAppKeys() {
    while(digitalRead(BUTTON_OK)==LOW||digitalRead(BUTTON_BACK)==LOW)delay(2);
    resetButtons();
}
void appInformation(const research::apps::Entry& entry) {
    releaseAppKeys();bool redraw=true;bool filePage=false;
    while(true) {
        buttonUp.tick();buttonDown.tick();buttonOK.tick();buttonBack.tick();
        if(buttonBack.isClick()||buttonBack.isHolded())break;
        if(buttonUp.isClick()||buttonDown.isClick()){filePage=!filePage;redraw=true;}
        if(buttonOK.isClick()) {
            if(!research::apps::toggleFavorite(appPreferences,entry.file))notice("Favorites full","Keep up to8 favorites. Remove one in its app info.");
            else if(!saveAppPreferences())notice("SD preferences","Favorite changed in RAM; SD save failed.");
            redraw=true;
        }
        if(redraw) {
            display.clearDisplay();pocket_draw::header(display,filePage?"App file":"App info",filePage?2:1,2,true);
            display.setCursor(0,13);display.print(entry.title);
            const char* text=filePage?entry.file:entry.description;
            for(size_t row=0;row<3&&row*21<strlen(text);++row){char part[22];snprintf(part,sizeof part,"%.21s",text+row*21);display.setCursor(0,23+row*8);display.print(part);}
            char detail[22];snprintf(detail,sizeof detail,"%lu B  %s",(unsigned long)entry.bytes,research::apps::favorite(appPreferences,entry.file)?"Starred":"Not starred");
            display.setCursor(0,47);display.print(detail);pocket_draw::footer(display,"OK star UP info BK");display.display();redraw=false;
        }
        delay(2);
    }
    releaseAppKeys();
}
struct ButtonState { uint8_t pin; const char* name; bool stable, observed, held; uint32_t changed, pressed; };
bool sendInput(LuaSandbox& vm, Runtime& runtime, const char* name, const char* event) {
    runtime.drawCalls = 0; return vm.input(name, event);
}
void stopOwnedRadio(Runtime& runtime) {
    if(!runtime.ownsRadio) return;
    research::labRequestStop();
    while(!research::labStop()) { Serial.println("[Lua] Waiting for RX worker stop"); delay(10); }
}
void sampleSelfTest(const char* name, void* bank) {
    char path[48]; snprintf(path, sizeof(path), "/apps/%s", name);
    File file = SD.open(path, FILE_READ);
    if(!file) { Serial.printf("[Lua] SAMPLE name=%s result=MISSING\n", name); return; }
    const size_t length = file.size();
    if(!length || length > LuaSandbox::kSourceBytes) {
        file.close(); Serial.printf("[Lua] SAMPLE name=%s result=FAIL reason=source-size\n", name); return;
    }
    char* source = static_cast<char*>(heap_caps_malloc(length + 1, MALLOC_CAP_8BIT));
    if(!source) { file.close(); Serial.printf("[Lua] SAMPLE name=%s result=FAIL reason=source-memory\n", name); return; }
    const size_t read = file.read(reinterpret_cast<uint8_t*>(source), length); file.close(); source[length] = 0;
    Runtime runtime = {}; runtime.diagnostic = true; activeApp = &runtime;
    LuaSandbox vm(bank, LuaSandbox::kHeapBytes, clockUs);
    bool good = read == length && vm.open(installBindings) && vm.load(source, length, path);
    free(source);
    unsigned frames=0;
    for(unsigned frame=0;frame<120&&good&&!runtime.quit;++frame) {
        if(frame%20==0) {
            const char* button=frame%60==0?"ok":frame%60==20?"down":"up";
            runtime.drawCalls=0;good=vm.input(button,"press");
            if(good){runtime.drawCalls=0;good=vm.input(button,"release");}
        }
        if(good){runtime.drawCalls=0;good=vm.tick(33);}
        if(good){runtime.drawCalls=0;good=vm.draw();}
        if(good)++frames;
        delay(1);
    }
    if(good) display.display();
    Serial.printf("[Lua] SAMPLE name=%s result=%s peak=%u/%u frames=%u%s%s\n", name, good ? "PASS" : "FAIL",
                  unsigned(vm.peak()), unsigned(LuaSandbox::kHeapBytes), frames, good ? "" : " reason=",
                  good ? "" : (read != length ? "incomplete-read" : vm.error()));
    vm.close(); activeApp = nullptr; stopOwnedRadio(runtime);
}
void launch(const char* name) {
    if(!validName(name)) return;
    char path[48]; snprintf(path, sizeof(path), "/apps/%s", name);
    File file = SD.open(path, FILE_READ);
    const size_t length = file ? file.size() : 0;
    if(!length || length > LuaSandbox::kSourceBytes) { if(file) file.close(); notice("App rejected", "Missing/empty file or source exceeds32KiB."); return; }
    void* bank = heap_caps_malloc(LuaSandbox::kHeapBytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    char* source = static_cast<char*>(heap_caps_malloc(length + 1, MALLOC_CAP_8BIT));
    if(!bank || !source) { file.close(); free(bank); free(source); notice("App memory", "Not enough free RAM for the bounded Lua VM."); return; }
    const size_t read = file.read(reinterpret_cast<uint8_t*>(source), length); file.close(); source[length] = 0;
    if(read != length) { free(source); free(bank); notice("App read failed", "SD read incomplete."); return; }
    Runtime runtime = {}; LuaSandbox vm(bank, LuaSandbox::kHeapBytes, clockUs); activeApp = &runtime;
    bool good = vm.open(installBindings) && vm.load(source, length, path);
    free(source);
    ButtonState buttons[] = {{BUTTON_UP,"up",false,false,false,0,0},{BUTTON_DOWN,"down",false,false,false,0,0},
                             {BUTTON_OK,"ok",false,false,false,0,0},{BUTTON_BACK,"back",false,false,false,0,0}};
    while(digitalRead(BUTTON_OK) == LOW && digitalRead(BUTTON_BACK) != LOW) delay(2);
    uint32_t frameAt = millis(), previous = frameAt;
    while(good && !runtime.quit) {
        const uint32_t now = millis();
        for(size_t i = 0; i < 4 && good && !runtime.quit; ++i) {
            ButtonState& button = buttons[i]; const bool down = digitalRead(button.pin) == LOW;
            if(down != button.observed) { button.observed = down; button.changed = now; }
            if(down != button.stable && uint32_t(now - button.changed) >= 20) {
                button.stable = down;
                if(down) { button.pressed = now; button.held = false; }
                good = sendInput(vm, runtime, button.name, down ? "press" : "release");
            }
            if(button.stable && !button.held && uint32_t(now - button.pressed) >= 700) {
                button.held = true;
                if(button.pin == BUTTON_BACK) { runtime.quit = true; break; }
                good = sendInput(vm, runtime, button.name, "hold");
            }
        }
        if(good && !runtime.quit && uint32_t(now - frameAt) >= 33) {
            uint32_t delta = now - previous; if(delta > 100) delta = 100;
            previous = now; frameAt = now; runtime.drawCalls = 0;
            good = vm.tick(delta);
            if(good && !runtime.quit) { runtime.drawCalls = 0; good = vm.draw(); }
            if(good && !runtime.quit) display.display(); // I2C flush is outside Lua execution quota.
        }
        delay(1);
    }
    char error[128]; snprintf(error, sizeof(error), "%s", good ? "" : vm.error());
    Serial.printf("[Lua] App=%s result=%s VMpeak=%u/%u\n", name, good ? "closed" : error,
                  unsigned(vm.peak()), unsigned(LuaSandbox::kHeapBytes));
    vm.close(); activeApp = nullptr; stopOwnedRadio(runtime); free(bank);
    while(digitalRead(BUTTON_BACK) == LOW || digitalRead(BUTTON_OK) == LOW) delay(2);
    resetButtons(); if(!good) notice("App stopped", error);
}
}

void runSdApps() {
    suspendLegacyReceive();
    while(!research::labStop()) delay(10);
    if(!appEntries)appEntries=static_cast<research::apps::Entry*>(heap_caps_calloc(32,sizeof(research::apps::Entry),MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT));
    if(!appEntries){restoreReceiveMode();notice("App catalog","Not enough free memory.");return;}
    SD.mkdir("/apps");readAppPreferences();listApps();size_t selected=0,sectionIndex=0;bool root=true;
    research::apps::Section section=research::apps::Section::All;
    uint8_t indices[32];size_t count=research::apps::visible(appEntries,appCount,appPreferences,section,indices);
    releaseAppKeys();bool redraw = true;
    while(true) {
        buttonUp.tick(); buttonDown.tick(); buttonOK.tick(); buttonBack.tick();
        if(buttonBack.isClick()||buttonBack.isHolded()) {
            if(root)break;
            root=true;selected=sectionIndex;releaseAppKeys();redraw=true;
        }
        const size_t rows=root?6:count;
        if(buttonUp.isClick()&&rows){selected=(selected+rows-1)%rows;redraw=true;}
        if(buttonDown.isClick()&&rows){selected=(selected+1)%rows;redraw=true;}
        if(!root&&count&&buttonOK.isHolded()) {
            appInformation(appEntries[indices[selected]]);
            count=research::apps::visible(appEntries,appCount,appPreferences,section,indices);
            if(selected>=count)selected=count?count-1:0;
            redraw=true;
        }
        if(buttonOK.isClick()) {
            if(root) {
                sectionIndex=selected;section=research::apps::Section(selected);root=false;selected=0;
                count=research::apps::visible(appEntries,appCount,appPreferences,section,indices);redraw=true;
            } else if(count) {
                char name[41];snprintf(name,sizeof name,"%s",appEntries[indices[selected]].file);
                research::apps::recordRecent(appPreferences,name);
                if(!saveAppPreferences())Serial.println("[Lua] Launcher preferences were not saved");
                launch(name);listApps();count=research::apps::visible(appEntries,appCount,appPreferences,section,indices);
                if(selected>=count)selected=count?count-1:0;
                redraw=true;
            }
        }
        if(redraw) {
            const size_t renderRows=root?6:count;
            display.clearDisplay();pocket_draw::header(display,root?"SD Apps":research::apps::sectionName(section),renderRows?selected+1:0,renderRows,SD.exists("/apps"));
            const size_t first=research::apps::firstRow(selected,renderRows,3);
            for(size_t row=0;row<3&&first+row<renderRows;++row) {
                const size_t index=first+row;char label[24];uint8_t icon=7;
                if(root) {
                    uint8_t temporary[32];const auto category=research::apps::Section(index);
                    const size_t amount=research::apps::visible(appEntries,appCount,appPreferences,category,temporary);
                    snprintf(label,sizeof label,"%s %u",research::apps::sectionName(category),unsigned(amount));
                    icon=index==2?5:index==3?2:index==1?4:index==4?6:7;
                } else {
                    const auto& entry=appEntries[indices[index]];
                    snprintf(label,sizeof label,"%s%s",research::apps::favorite(appPreferences,entry.file)?"*":"",entry.title);
                    icon=entry.category==research::apps::Category::Games?5:entry.category==research::apps::Category::Radio?2:4;
                }
                pocket_draw::listRow(display,12+row*14,label,index==selected,icon);
            }
            if(!renderRows){display.setCursor(3,20);display.print(section==research::apps::Section::Favorites?"Hold OK on an app":"No apps in this group");display.setCursor(3,33);display.print(section==research::apps::Section::Favorites?"then OK to star it":"Copy .lua to /apps");}
            pocket_draw::footer(display,root?"UP/DN OK open BK exit":"OK run HoldOK info BK");display.display();redraw=false;
        }
        delay(2);
    }
    while(!research::labStop()) delay(10);
    restoreReceiveMode(); resetButtons();
}

void sdAppsSelfTest() {
    if(activeApp || research::labSnapshot().active) { Serial.println("[Lua] SELFTEST busy"); return; }
    // Match the launcher: noisy legacy RX interrupts must not consume a VM
    // callback's wall-time budget while the diagnostic owns the device.
    suspendLegacyReceive();
    while(!research::labStop()) delay(10);
    void* bank = heap_caps_malloc(LuaSandbox::kHeapBytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if(!bank) { Serial.println("[Lua] SELFTEST FAIL heap allocation"); restoreReceiveMode(); return; }
    Runtime runtime = {}; runtime.diagnostic = true; activeApp = &runtime;
    LuaSandbox vm(bank, LuaSandbox::kHeapBytes, clockUs);
    const char smoke[] = "app={draw=function() ui.clear();ui.text(0,0,'Lua SDK self-test');ui.line(0,12,127,12) end,tick=function(dt) assert(dt==33) end}";
    const bool smokeGood = vm.open(installBindings) && vm.load(smoke, sizeof(smoke)-1, "selftest") && vm.tick(33) && vm.draw();
    if(smokeGood) display.display();
    const char runaway[] = "while true do end";
    const bool quotaGood = smokeGood && !vm.load(runaway, sizeof(runaway)-1, "quota-test") && strstr(vm.error(), "quota");
    Serial.printf("[Lua] SELFTEST %s version=%s peak=%u/%u instructionQuota=%lu wallUs=%lu\n",
                  smokeGood && quotaGood ? "PASS" : "FAIL", LUA_RELEASE, unsigned(vm.peak()),
                  unsigned(LuaSandbox::kHeapBytes), static_cast<unsigned long>(LuaSandbox::kInstructions), static_cast<unsigned long>(LuaSandbox::kWallUs));
    vm.close(); activeApp = nullptr; stopOwnedRadio(runtime);
    const char* samples[] = {"hello.lua", "stopwatch.lua", "rf_dashboard.lua", "snake.lua", "game2048.lua",
        "countdown.lua","calculator.lua","unit_converter.lua","resistor_codes.lua","blockfall.lua","mines.lua"};
    for(size_t i = 0; i < sizeof(samples) / sizeof(samples[0]); ++i) sampleSelfTest(samples[i], bank);
    free(bank);
    restoreReceiveMode();
    returnToMainMenu();
}
