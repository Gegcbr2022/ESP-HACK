#include "Arduino.h"
#include "SD.h"
#include "app_install.h"
#include <cstdlib>
#include <iostream>
MockState mock;MockSerial Serial;MockSD SD;
static unsigned scenarios=0;
static const char* app="/apps/demo.lua";
static const char* temporary="/apps/demo.lua.upload";
static const char* backup="/apps/demo.lua.bak";
#define CHECK(condition) do {if(!(condition)){std::cerr<<"FAIL line "<<__LINE__<<": " #condition "\n"<<mock.output;std::exit(1);}}while(0)

static std::vector<uint8_t> bytes(const std::string& text){return std::vector<uint8_t>(text.begin(),text.end());}
static std::string digest(const std::string& source) {
    uint8_t value[32];mockDigest(bytes(source),value);char text[65];
    for(unsigned i=0;i<32;++i)snprintf(text+2*i,3,"%02x",value[i]);return text;
}
static void reset(const std::string& source="abc",bool existing=true) {
    ++scenarios;mock=MockState();mock.input=source;mock.files["/apps"].directory=true;
    if(existing)mock.files[app].bytes=bytes("old working app");
}
static bool upload(const std::string& source="abc") {
    const std::string request="APP_PUT demo.lua "+std::to_string(source.size())+" "+digest(source);
    return handleAppInstallCommand(request.c_str());
}
static bool has(const char* text){return mock.output.find(text)!=std::string::npos;}
static void preserved(){CHECK(mock.files.at(app).bytes==bytes("old working app"));CHECK(!has("APP_INSTALLED"));}
int main() {
    reset();CHECK(!handleAppInstallCommand("rfdiag"));CHECK(mock.output.empty());
    reset();CHECK(handleAppInstallCommand("APP_PUT ../demo.lua 3 abc"));CHECK(!has("APP_READY"));preserved();
    reset();CHECK(upload());CHECK(has("APP_READY 3"));CHECK(has("APP_INSTALLED demo.lua 3"));CHECK(mock.readBytes==3);CHECK(mock.files.at(app).bytes==bytes("abc"));CHECK(!SD.exists(temporary));CHECK(!SD.exists(backup));
    reset("abc",false);CHECK(upload());CHECK(has("APP_INSTALLED"));CHECK(mock.files.at(app).bytes==bytes("abc"));
    const std::string large(32768,'X');reset(large);CHECK(upload(large));CHECK(has("APP_INSTALLED"));CHECK(mock.readBytes==large.size());CHECK(mock.files.at(app).bytes==bytes(large));
    reset();mock.corruptFlush=true;CHECK(upload());CHECK(has("APP_ERROR SD readback failed"));preserved();CHECK(!SD.exists(temporary));
    reset();mock.truncateFlush=true;CHECK(upload());CHECK(has("APP_ERROR SD readback failed"));preserved();
    reset();mock.shortRead=true;CHECK(upload());CHECK(has("APP_ERROR SD readback failed"));preserved();
    reset(large);mock.readDelay=1000;CHECK(upload(large));CHECK(has("APP_ERROR SD readback failed"));preserved();CHECK(mock.now<15100);
    reset();mock.shortWrite=true;CHECK(upload());CHECK(has("APP_ERROR transfer"));preserved();
    reset("a");CHECK(upload("abc"));CHECK(has("APP_ERROR transfer"));preserved();CHECK(mock.now>=15000);
    reset("xyz");CHECK(upload("abc"));CHECK(has("APP_ERROR transfer"));preserved();CHECK(mock.readBytes==0);
    reset();mock.failStart=1;CHECK(upload());CHECK(has("APP_ERROR digest initialization"));CHECK(!has("APP_READY"));preserved();CHECK(mock.inputAt==0);
    reset();mock.failStart=2;CHECK(upload());CHECK(has("APP_ERROR SD readback failed"));preserved();
    reset();mock.failRenameFrom.insert(app);CHECK(upload());CHECK(has("APP_ERROR preserve previous"));preserved();CHECK(!SD.exists(temporary));
    reset();mock.failRenameFrom.insert(temporary);CHECK(upload());CHECK(has("APP_ERROR promote failed"));preserved();CHECK(!SD.exists(backup));
    reset();mock.failRenameFrom.insert(temporary);mock.failRenameFrom.insert(backup);CHECK(upload());CHECK(has("previous at .bak"));CHECK(!SD.exists(app));CHECK(mock.files.at(backup).bytes==bytes("old working app"));CHECK(!has("APP_INSTALLED"));
    reset();mock.files[backup]=mock.files[app];mock.files.erase(app);CHECK(upload());CHECK(has("APP_INSTALLED"));CHECK(mock.files.at(app).bytes==bytes("abc"));
    reset();mock.files[backup]=mock.files[app];mock.files.erase(app);mock.failRenameFrom.insert(backup);CHECK(upload());CHECK(has("APP_ERROR previous recovery failed"));CHECK(!has("APP_READY"));CHECK(mock.files.at(backup).bytes==bytes("old working app"));
    reset();mock.files[temporary].bytes=bytes("stale");mock.failRemove.insert(temporary);CHECK(upload());CHECK(has("APP_ERROR stale upload cleanup"));CHECK(!has("APP_READY"));preserved();
    reset();mock.files[backup].bytes=bytes("older backup");mock.failRemove.insert(backup);CHECK(upload());CHECK(has("APP_ERROR stale backup cleanup"));preserved();
    reset();mock.files[app].directory=true;CHECK(upload());CHECK(has("APP_ERROR invalid destination"));CHECK(!has("APP_READY"));
    std::cout<<"PASS: "<<scenarios<<" installer integrity/recovery scenarios (mock SD and digest backend)\n";
}
