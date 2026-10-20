#include "lab_suite.h"
#include "lab_service.h"
#include "sub_file.h"
#include "raw_capture.h"
#include "test_decoder.h"
#include "app_install.h"
#include "lua_apps.h"
#include "../subghz.h"
#include <SD.h>
#include <esp_system.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

namespace {
using namespace research;
RadioSettings settings;
RegisterImage radio;
TxProfile profile = TxProfile::ReceiveOnly;
uint8_t powerIndex = 0;
ChatMode chatFormat = ChatMode::FlipperPlaintext;
uint32_t sweepStep = 25000;
int32_t waveform[kWaveformCapacity];
size_t waveformCount;
SubInfo openedInfo;
char paths[40][112];
bool pathIsDir[40];
size_t pathCount;
char activePreset[64] = "AM270";
char currentFolder[112] = "/subghz";
char filter[32] = "";
char notice[80] = "";
uint32_t lastFrame;
bool up, down, ok, back, holdOk, holdBack;

void input() {
    buttonUp.tick(); buttonDown.tick(); buttonOK.tick(); buttonBack.tick();
    up = buttonUp.isClick(); down = buttonDown.isClick();
    ok = buttonOK.isClick(); back = buttonBack.isClick();
    holdOk = buttonOK.isHolded(); holdBack = buttonBack.isHolded();
    delay(2);
}
void resetInput() {
    // A hold used to finish text entry must be released before a TX confirmation.
    // This UI wait yields; the radio worker and edge ISR continue independently.
    while(digitalRead(BUTTON_UP)==LOW||digitalRead(BUTTON_DOWN)==LOW||
          digitalRead(BUTTON_OK)==LOW||digitalRead(BUTTON_BACK)==LOW)delay(2);
    buttonUp.resetStates();buttonDown.resetStates();buttonOK.resetStates();buttonBack.resetStates();
}
bool frame(bool force = false) {
    uint32_t now = millis();
    if(!force && now - lastFrame < 100) return false;
    lastFrame = now; display.clearDisplay(); display.setTextSize(1);
    display.setTextColor(SH110X_WHITE); display.setTextWrap(false); return true;
}
void line(uint8_t row, const char* text) { display.setCursor(0, row * 9 + 2); display.print(text); }
void title(const char* text) {
    display.fillRect(0, 0, 128, 11, SH110X_WHITE);
    display.setTextColor(SH110X_BLACK); display.setCursor(2, 2); display.print(text);
    display.setTextColor(SH110X_WHITE);
}
void footer(const char* text) { display.drawFastHLine(0, 53, 128, SH110X_WHITE); display.setCursor(0, 56); display.print(text); }
void showNotice(const char* text) { snprintf(notice, sizeof(notice), "%s", text); Serial.printf("[RadioLab] %s\n", notice); }
void message(const char* heading, const char* text) {
    char rows[32][22]={};size_t row=0,column=0,offset=0;
    for(const char* cursor=text;*cursor&&row<32;++cursor) {
        if(*cursor=='\n'){++row;column=0;continue;}
        if(column==21){++row;column=0;if(row==32)break;}
        rows[row][column++]=*cursor;
    }
    const size_t total=row<32?row+1:32;
    resetInput();
    while(true) {
        input(); if(ok || back || holdBack) break;
        if(up&&offset)--offset;
        if(down&&offset+4<total)++offset;
        if(frame()) {
            title(heading);
            for(size_t i=0;i<4&&offset+i<total;++i){display.setCursor(0,15+i*8);display.print(rows[offset+i]);}
            footer("UP/DN scroll OK/BACK"); display.display();
        }
    }
    resetInput();
}
bool textEntry(const char* heading, char* output, size_t capacity, const char* alphabet = nullptr) {
    static const char keys[] = " abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789._-!?/@";
    if(!alphabet) alphabet = keys;
    size_t key = 0, n = strlen(output), total = strlen(alphabet);
    resetInput();
    while(true) {
        input();
        if(up) key = (key + total - 1) % total;
        if(down) key = (key + 1) % total;
        if(ok && n + 1 < capacity) { output[n++] = alphabet[key]; output[n] = 0; }
        if(back && n) output[--n] = 0;
        if(holdOk) { resetInput(); return true; }
        if(holdBack) { resetInput(); return false; }
        if(frame()) {
            title(heading); line(2, output + (n > 20 ? n - 20 : 0));
            char buf[40]; snprintf(buf, sizeof(buf), "UP/DOWN: [%c]  %u/%u", alphabet[key], unsigned(n), unsigned(capacity - 1)); line(4, buf);
            if((millis()/500)%2)display.drawFastHLine(60,46,6,SH110X_WHITE);
            footer("hold OK done/BK cancel"); display.display();
        }
    }
}
bool validLeaf(const char* name) {
    if(!name || !*name || *name == '.' || strlen(name) > 48) return false;
    for(const char* p = name; *p; ++p)
        if(!isalnum(uint8_t(*p)) && *p != '_' && *p != '-' && *p != '.') return false;
    return !strstr(name, "..");
}
bool safePath(const char* path) {
    return path && !strncmp(path, "/subghz/", 8) && !strstr(path, "..") && !strchr(path, '\\') && strlen(path) < 112;
}
bool textToFile(void* context, const char* text, size_t length) {
    return static_cast<File*>(context)->write(reinterpret_cast<const uint8_t*>(text), length) == length;
}
struct WaveSink { size_t used; bool full; };
bool stagePulse(void* context, int32_t pulse) {
    WaveSink* sink = static_cast<WaveSink*>(context);
    if(sink->used < kWaveformCapacity) waveform[sink->used++] = pulse;
    else sink->full = true;
    return true; // Inspection continues streaming even when playback staging is full.
}
bool inspectFile(const char* path) {
    if(!labStop()) { showNotice("Radio stop timed out"); return false; }
    File file = SD.open(path, FILE_READ);
    if(!file) { showNotice("SD open failed"); return false; }
    WaveSink sink = {};
    SubParser parser(stagePulse, &sink);
    uint8_t block[256]; bool valid = true;
    while(file.available() && valid) {
        size_t n = file.read(block, sizeof(block));
        if(!n) { valid = false; break; }
        valid = parser.feed(reinterpret_cast<char*>(block), n);
        delay(1);
    }
    file.close(); valid = valid && parser.finish();
    if(!valid) {
        char error[80]; snprintf(error, sizeof(error), "line %lu: %s", (unsigned long)parser.line(), parser.errorText());
        showNotice(error); waveformCount = 0; return false;
    }
    openedInfo = parser.info(); waveformCount = sink.full ? 0 : sink.used;
    if(sink.full) showNotice("Inspected; exceeds 2048 pulse playback limit");
    else showNotice("File validated");
    return true;
}
bool saveWave(const char* path) {
    if(!safePath(path) || SD.exists(path)) return false;
    File file = SD.open(path, FILE_WRITE); if(!file) return false;
    RawWriter writer(textToFile, &file);
    RegisterImage exported = radio;
    // Advanced RX settings deliberately contain a zero PA table. Give exported
    // binary OOK recordings an explicit nominal -10dBm table for stock Flipper.
    if(settings.modulation == Modulation::AskOok && exported.paPresent) {
        bool nonzero = false; for(size_t i=0;i<8;++i)nonzero |= exported.pa[i] != 0;
        if(!nonzero){exported.pa[1]=0x34;exported.set(0x22,(exported.value[0x22]&0xF8)|1);}
    }
    bool valid = writer.beginCustom(settings.frequencyHz, exported);
    for(size_t i = 0; valid && i < waveformCount; ++i) valid = writer.pulse(waveform[i]);
    valid = valid && writer.finish(); file.flush(); file.close();
    if(!valid) SD.remove(path);
    return valid;
}
bool uniquePath(char* output, size_t capacity, const char* folder, const char* prefix) {
    for(unsigned n = 1; n < 10000; ++n) {
        snprintf(output, capacity, "%s/%s_%04u.sub", folder, prefix, n);
        if(!SD.exists(output)) return true;
    }
    return false;
}
bool applySettings() {
    RegisterImage next; RadioSettings actual; const char* error = nullptr;
    if(!encodeRadioSettings(settings, next, &actual, &error)) { showNotice(error); return false; }
    radio = next; settings = actual; snprintf(activePreset, sizeof(activePreset), "Custom"); return true;
}
bool selectPreset(size_t index) {
    const RadioPreset* p = presetAt(index); if(!p) return false;
    RegisterImage next;
    RadioSettings previous = settings;
    if(!loadPreset(p->name, next) || !describeRadio(next, settings.frequencyHz, settings)) return false;
    settings.rssiThresholdDbm = previous.rssiThresholdDbm;
    settings.rxTimeoutMs = previous.rxTimeoutMs; settings.txLimitMs = previous.txLimitMs;
    radio = next; snprintf(activePreset, sizeof(activePreset), "%s", p->name); return true;
}
bool startMode(LabMode mode) {
    if(!labStart(mode, radio, settings, profile, powerIndex, sweepStep, chatFormat)) { showNotice("Worker start failed"); return false; }
    return true;
}
void saveCapture() {
    if(!labStop()) { showNotice("Radio still active"); return; }
    LabSnapshot s = labSnapshot();
    if(!s.captureTriggered||!s.captureComplete){showNotice("No complete RSSI-triggered burst");return;}
    if(s.dropped || s.captureFull || s.rejected) { showNotice("Damaged/full capture: not saved"); return; }
    waveformCount = labCopyPulses(waveform, kWaveformCapacity);
    char path[112];
    if(!waveformCount) showNotice("No pulses captured");
    else if(uniquePath(path, sizeof(path), "/subghz", "LabRAW") && saveWave(path)) showNotice(path);
    else showNotice("SD save failed");
}
void captureApp(bool protocol) {
    if(settings.packetFormat != PacketFormat::Async) { message("Read RAW", "Select an asynchronous preset first."); return; }
    if(!startMode(LabMode::Raw)) return;
    resetInput();
    while(true) {
        input(); if(back || holdBack) break;
        if(ok) { saveCapture(); message("Capture result", notice); break; }
        if(frame()) {
            const LabSnapshot s = labSnapshot(); title(protocol ? "Protocol Read" : "Read RAW");
            char b[48]; snprintf(b, sizeof(b), "%lu.%03lu %s %ddBm", (unsigned long)(settings.frequencyHz/1000000),
                    (unsigned long)((settings.frequencyHz/1000)%1000), activePreset, s.rssiDbm); line(2, b);
            snprintf(b, sizeof(b), "%lu pulses drop%lu", (unsigned long)s.pulseCount, (unsigned long)s.dropped); line(3, b);
            if(protocol) {
                const size_t n = labCopyPulses(waveform, kWaveformCapacity); DecodedSignal decoded;
                if(decodeLabOok(waveform, n, decoded)) { snprintf(b, sizeof(b), "LAB-OOK24: %04lX CRC OK", (unsigned long)decoded.payload); line(4, b); }
                else line(4, "Listening LAB-OOK24");
            } else line(4,s.captureComplete?"Burst complete: OK save":s.captureTriggered?"Capturing burst":"Waiting RSSI threshold");
            footer("OK stop+save  BACK exit"); display.display();
        }
    }
    labStop(); resetInput();
}
void monitorApp(bool sweep) {
    if(!startMode(sweep ? LabMode::Sweep : LabMode::Monitor)) return;
    resetInput();
    while(true) {
        input(); if(back || holdBack) break;
        if(ok) {
            LabSnapshot s = labSnapshot();
            char path[112];
            snprintf(path, sizeof(path), "/subghz/scan_%08lX.csv", (unsigned long)esp_random());
            File f = SD.open(path, FILE_WRITE);
            if(f) {
                f.println("frequency_hz,rssi_dbm,peak_dbm");
                for(size_t i = 0; i < (sweep ? kSweepBins : 1); ++i)
                    f.printf("%lu,%d,%d\n", (unsigned long)(settings.frequencyHz+i*sweepStep), sweep?s.sweep[i]:s.rssiDbm, sweep?s.peakHold[i]:s.peakDbm);
                f.close(); showNotice(path);
            } else showNotice("Scan export failed");
        }
        if(frame()) {
            LabSnapshot s = labSnapshot(); title(sweep ? "Spectrum / peak hold" : "Frequency Analyzer");
            char b[40]; snprintf(b, sizeof(b), "%lu kHz %ddBm pk%d", (unsigned long)(s.frequencyHz/1000), s.rssiDbm, s.peakDbm); line(2, b);
            if(sweep) {
                for(size_t i=0; i<kSweepBins; ++i) {
                    int y = constrain((s.sweep[i]+110)/3, 0, 25);
                    int p = constrain((s.peakHold[i]+110)/3, 0, 25);
                    display.drawFastVLine(i*2, 51-y, y, SH110X_WHITE); display.drawPixel(i*2, 51-p, SH110X_WHITE);
                }
            } else {
                snprintf(b, sizeof(b), "Carrier %s / %lu samples", s.carrier?"ON":"off", (unsigned long)s.samples); line(3,b);
                snprintf(b, sizeof(b), "Threshold %d dBm", settings.rssiThresholdDbm); line(4,b);
                if(s.rssiDbm >= settings.rssiThresholdDbm) display.fillRect(113, 28, 12, 12, SH110X_WHITE);
            }
            footer("OK SD export BACK exit"); display.display();
        }
    }
    labStop(); resetInput();
}
void analyzerApp(bool protocol) {
    if(!waveformCount) { message("RAW Analyzer", "Open a RAW file in Signal Manager, or capture a waveform first."); return; }
    RawStatistics stats; for(size_t i=0;i<waveformCount;++i) stats.add(waveform[i]);
    size_t offset = 0; resetInput();
    while(true) {
        input(); if(back || holdBack) break;
        if(up && offset >= 20) offset -= 20;
        if(down && offset+20 < waveformCount) offset += 20;
        if(frame()) {
            title(protocol?"Protocol Analyzer":"RAW Signal Analyzer"); char b[48];
            snprintf(b,sizeof(b),"%u pulses / %lu ms",unsigned(waveformCount),(unsigned long)(stats.durationUs/1000)); line(2,b);
            if(protocol) {
                DecodedSignal decoded;
                if(decodeLabOok(waveform,waveformCount,decoded)) { snprintf(b,sizeof(b),"LAB-OOK24 %04lX CRC OK",(unsigned long)decoded.payload); line(3,b); line(4,"24 bits / unit 300us"); }
                else { line(3,"No registered decoder"); line(4,"No modulation inference"); }
            } else {
                snprintf(b,sizeof(b),"min%lu max%lu gaps%lu",(unsigned long)stats.minimumUs,(unsigned long)stats.maximumUs,(unsigned long)stats.gaps); line(3,b);
                for(size_t i=offset;i<waveformCount && i<offset+40;++i) {
                    int x=(i-offset)*3; int high=waveform[i]>0?1:0;
                    display.drawFastHLine(x,high?39:48,3,SH110X_WHITE);
                    if(i>offset) display.drawFastVLine(x,39,10,SH110X_WHITE);
                }
            }
            footer("UP/DOWN pan BACK exit"); display.display();
        }
    }
    resetInput();
}
void listFiles() {
    pathCount=0; File folder=SD.open(currentFolder); if(!folder) return;
    File item;
    while((item=folder.openNextFile()) && pathCount<40) {
        const char* name=item.name(); const char* leaf=strrchr(name,'/'); leaf=leaf?leaf+1:name;
        if(*leaf!='.' && (!*filter || strstr(leaf,filter))) {
            snprintf(paths[pathCount],sizeof(paths[0]),"%s/%s",currentFolder,leaf);
            pathIsDir[pathCount]=item.isDirectory(); ++pathCount;
        }
        item.close();
    }
    folder.close();
}
void metadataScreen() {
    char text[200];
    snprintf(text,sizeof(text),"%lu Hz\n%.25s\n%.25s\n%lu pulses %lu ms\n%llu bytes RAW",
        (unsigned long)openedInfo.frequencyHz,openedInfo.preset,openedInfo.protocol,
        (unsigned long)openedInfo.raw.pulses,(unsigned long)(openedInfo.raw.durationUs/1000),
        (unsigned long long)openedInfo.raw.bufferedBytes());
    message("Signal metadata",text);
}
bool duplicateFile(const char* from,const char* to) {
    if(!safePath(to)||SD.exists(to)) return false;
    File src=SD.open(from,FILE_READ); if(!src) return false;
    File dst=SD.open(to,FILE_WRITE); if(!dst){src.close();return false;}
    uint8_t b[256]; bool valid=true;
    while(src.available()) { size_t n=src.read(b,sizeof(b)); if(!n||dst.write(b,n)!=n){valid=false;break;} delay(1); }
    dst.flush();src.close();dst.close();if(!valid)SD.remove(to);return valid;
}
void fileActions(const char* path,bool presetMode) {
    static const char* actions[]={"Inspect/validate","Load radio preset","RAW analyzer","Rename","Move","Duplicate/export","Delete"};
    size_t selected=0; resetInput();
    while(true) {
        input();if(back||holdBack)break;
        if(up)selected=(selected+6)%7;if(down)selected=(selected+1)%7;
        if(ok) {
            if(selected<=2) {
                if(!inspectFile(path)){message("File error",notice);continue;}
                if(selected==0)metadataScreen();
                if(selected==1) {
                    RadioSettings next;
                    if(settingsFromSub(openedInfo,next)) {
                        settings=next;radio=openedInfo.radio;snprintf(activePreset,sizeof(activePreset),"Imported");showNotice("Radio preset imported");
                    } else showNotice("Unsupported configuration");
                    message("Import",notice);
                }
                if(selected==2)analyzerApp(false);
            } else if(selected==6) {
                message("Delete", "Hold OK on the next screen to delete this file.");
                resetInput(); bool remove=false;
                while(true){input();if(back||holdBack)break;if(holdOk){remove=true;break;}if(frame()){title("Delete file?");line(2,path+8);footer("hold OK delete/BK exit");display.display();}}
                if(remove)showNotice(SD.remove(path)?"Deleted":"Delete failed");break;
            } else {
                char name[64]="";char destination[112];
                if(selected==4) {
                    if(!textEntry("Folder under /subghz",name,sizeof(name),"abcdefghijklmnopqrstuvwxyz0123456789_-"))continue;
                    if(!validLeaf(name)){message("Path","Invalid folder");continue;}
                    char folder[80];snprintf(folder,sizeof(folder),"/subghz/%s",name);SD.mkdir(folder);
                    const char* leaf=strrchr(path,'/');snprintf(destination,sizeof(destination),"%s/%s",folder,leaf?leaf+1:path);
                } else {
                    if(!textEntry("File name (no .sub)",name,sizeof(name),"abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-"))continue;
                    if(!validLeaf(name)){message("Path","Invalid file name");continue;}
                    snprintf(destination,sizeof(destination),"%s/%s.sub",presetMode?"/subghz/presets":currentFolder,name);
                }
                bool valid=safePath(destination)&&!SD.exists(destination);
                if(valid)valid=selected==5?duplicateFile(path,destination):SD.rename(path,destination);
                showNotice(valid?"File updated":"Operation failed/existing file");message("Signal Manager",notice);break;
            }
        }
        if(frame()) {
            title(presetMode?"Preset operations":"Signal operations");line(2,path+8);
            for(size_t row=0;row<3;++row){size_t item=(selected+row)%7;display.setCursor(0,29+row*8);display.print(row?"  ":"> ");display.print(actions[item]);}
            display.display();
        }
    }
    resetInput();
}
void browser(bool music=false,bool presets=false) {
    if(!labStop())return;
    snprintf(currentFolder,sizeof(currentFolder),"%s",presets?"/subghz/presets":"/subghz");filter[0]=0;
    listFiles();size_t selected=0;resetInput();
    while(true) {
        input();
        if(holdBack)break;
        if(back){char* slash=strrchr(currentFolder,'/');if(slash&&slash>currentFolder+1){*slash=0;listFiles();selected=0;}else break;}
        if(up&&pathCount)selected=(selected+pathCount-1)%pathCount;
        if(down&&pathCount)selected=(selected+1)%pathCount;
        if(holdOk){if(textEntry("Search (empty=all)",filter,sizeof(filter))){listFiles();selected=0;}resetInput();}
        if(ok&&pathCount) {
            if(pathIsDir[selected]) {snprintf(currentFolder,sizeof(currentFolder),"%s",paths[selected]);listFiles();selected=0;}
            else if(music) {
                if(inspectFile(paths[selected])) {
                    metadataScreen();
                    RadioSettings fileSettings;bool ook=describeRadio(openedInfo.radio,openedInfo.frequencyHz,fileSettings)&&fileSettings.modulation==Modulation::AskOok;
                    if(openedInfo.kind!=SubKind::Raw||!openedInfo.rawCompatible||!ook||!waveformCount)message("Music Lab","Requires RAW binary OOK, <=2048 pulses, <=5 seconds. FSK playback is unsupported.");
                    else {
                        bool done=false;resetInput();
                        while(!done){input();if(back||holdBack)break;if(holdOk){uint32_t elapsed;int32_t error;bool sent=labTransmitRaw(waveform,waveformCount,openedInfo.radio,openedInfo.frequencyHz,profile,powerIndex,settings.txLimitMs,elapsed,error);char b[150];snprintf(b,sizeof(b),"%s\nWall %lu us\nCompletion delta %ld us\nEdge jitter unmeasured",sent?"Waveform sent":"TX blocked/aborted",(unsigned long)elapsed,(long)error);message("Music timing",b);done=true;}if(frame()){title("Music Lab / RAW");line(2,"Hold OK: finite TX");line(3,profile==TxProfile::ShieldedLab?"Shielded lab enabled":"TX profile: receive only");line(4,"BACK stops transmission");footer("hold OK play BACK exit");display.display();}}
                    }
                } else message("Music file error",notice);
            } else {char path[112];snprintf(path,sizeof(path),"%s",paths[selected]);fileActions(path,presets);listFiles();if(selected>=pathCount)selected=0;}
        }
        if(frame()) {
            title(music?"Sub-GHz Music Lab":presets?"Saved Presets":"Signal Manager");line(2,currentFolder);
            for(size_t row=0;row<4;++row){size_t i=selected+row;if(i>=pathCount)break;const char* leaf=strrchr(paths[i],'/');display.setCursor(0,27+row*8);display.print(row?"  ":"> ");display.print(pathIsDir[i]?"/":"");display.print(leaf?leaf+1:paths[i]);}
            if(!pathCount)line(4,"No matching files");display.display();
        }
    }
    resetInput();
}
void savePreset() {
    char name[52]="";
    if(!textEntry("Save preset name",name,sizeof(name),"abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-"))return;
    if(!validLeaf(name)){message("Preset","Invalid name");return;}
    char path[112];snprintf(path,sizeof(path),"/subghz/presets/%s.sub",name);
    if(SD.exists(path)){message("Preset","Name exists; choose another name or delete the old preset.");return;}
    File f=SD.open(path,FILE_WRITE);if(!f){message("Preset","SD open failed");return;}
    bool valid=writePresetMetadata(textToFile,&f,radio,settings);
    f.flush();f.close();if(!valid)SD.remove(path);message("Save preset",valid?path:"Invalid preset / SD write failed");
}
void settingsApp() {
    static const char* labels[]={"Preset","Frequency Hz","Modulation","Packet format","RX bandwidth Hz","Symbol rate baud","Deviation Hz","AGCCTRL2","AGCCTRL1","AGCCTRL0","Sync word","Sync mode","CRC","RSSI threshold","RX timeout ms","TX limit ms","TX profile","Power nominal","Sweep step Hz","Whitening","Manchester","FEC","Packet length","Save preset","Load/edit/manage","Reset defaults"};
    const size_t count=sizeof(labels)/sizeof(labels[0]);size_t selected=0,preset=0;resetInput();
    while(true) {
        input();if(back||holdBack)break;if(up)selected=(selected+count-1)%count;if(down)selected=(selected+1)%count;
        if(ok) {
            RadioSettings previous=settings;bool encode=true;
            switch(selected) {
            case 0:preset=(preset+1)%presetCount();selectPreset(preset);encode=false;break;
            case 1:{char value[16];snprintf(value,sizeof(value),"%lu",(unsigned long)settings.frequencyHz);if(textEntry("Frequency Hz",value,sizeof(value),"0123456789"))settings.frequencyHz=strtoul(value,nullptr,10);break;}
            case 2:{static const Modulation modes[]={Modulation::AskOok,Modulation::Fsk2,Modulation::Gfsk,Modulation::Fsk4,Modulation::Msk};size_t i=0;while(i<5&&modes[i]!=settings.modulation)++i;settings.modulation=modes[(i+1)%5];if(settings.modulation!=Modulation::AskOok&&settings.modulation!=Modulation::Msk&&settings.deviationHz<1586.9)settings.deviationHz=19042.96875;break;}
            case 3:settings.packetFormat=settings.packetFormat==PacketFormat::Async?PacketFormat::Fifo:PacketFormat::Async;if(settings.packetFormat==PacketFormat::Fifo){settings.syncMode=3;settings.crc=true;}else{settings.syncMode=0;settings.crc=false;settings.whitening=false;settings.fec=false;}break;
            case 4:{const double values[]={58035.715,101562.5,135416.667,203125,270833.333,325000,406250,650000,812500};size_t i=0;while(i<9&&values[i]<=settings.bandwidthHz+1)++i;settings.bandwidthHz=values[i%9];break;}
            case 5:{const double values[]={600,1200,4800,10000,38400,100000};size_t i=0;while(i<6&&values[i]<=settings.dataRateBaud*1.01)++i;settings.dataRateBaud=values[i%6];break;}
            case 6:{const double values[]={1586.915,2380.372,4760.743,19042.97,47607.423};size_t i=0;while(i<5&&values[i]<=settings.deviationHz+1)++i;settings.deviationHz=values[i%5];break;}
            case 7:++settings.agc2;break;case 8:settings.agc1=(settings.agc1+1)&0x7F;break;case 9:++settings.agc0;break;
            case 10:{char value[5];snprintf(value,sizeof(value),"%04X",settings.syncWord);if(textEntry("Sync word HEX",value,sizeof(value),"0123456789ABCDEF"))settings.syncWord=strtoul(value,nullptr,16);break;}
            case 11:settings.syncMode=(settings.syncMode+1)%8;break;
            case 12:settings.crc=!settings.crc;break;
            case 13:settings.rssiThresholdDbm+=5;if(settings.rssiThresholdDbm>-30)settings.rssiThresholdDbm=-110;break;
            case 14:settings.rxTimeoutMs=settings.rxTimeoutMs==5000?30000:settings.rxTimeoutMs==30000?60000:5000;break;
            case 15:settings.txLimitMs=settings.txLimitMs==1000?3000:settings.txLimitMs==3000?5000:1000;break;
            case 16:profile=profile==TxProfile::ReceiveOnly?TxProfile::ShieldedLab:TxProfile::ReceiveOnly;encode=false;break;
            case 17:powerIndex=(powerIndex+1)%3;encode=false;break;
            case 18:sweepStep=sweepStep==25000?50000:sweepStep==50000?100000:25000;encode=false;break;
            case 19:settings.whitening=!settings.whitening;break;case 20:settings.manchester=!settings.manchester;break;case 21:settings.fec=!settings.fec;break;
            case 22:settings.packetLength=settings.packetLength>=60?16:settings.packetLength+4;break;
            case 23:savePreset();encode=false;break;
            case 24:browser(false,true);encode=false;break;
            case 25:settings=RadioSettings();selectPreset(0);profile=TxProfile::ReceiveOnly;powerIndex=0;encode=false;break;
            }
            if(encode&&!applySettings()){settings=previous;message("Unsupported settings",notice);}
            resetInput();
        }
        if(frame()) {
            title("Radio Settings");line(2,labels[selected]);char value[70];
            switch(selected){
            case 0:snprintf(value,sizeof(value),"%s",activePreset);break;case 1:snprintf(value,sizeof(value),"%lu",(unsigned long)settings.frequencyHz);break;
            case 2:snprintf(value,sizeof(value),"%s",modulationName(settings.modulation));break;case 3:snprintf(value,sizeof(value),"%s",settings.packetFormat==PacketFormat::Async?"Async GPIO":"Variable FIFO");break;
            case 4:snprintf(value,sizeof(value),"%.1f",settings.bandwidthHz);break;case 5:snprintf(value,sizeof(value),"%.1f",settings.dataRateBaud);break;case 6:snprintf(value,sizeof(value),"%.2f",settings.deviationHz);break;
            case 7:snprintf(value,sizeof(value),"0x%02X",settings.agc2);break;case 8:snprintf(value,sizeof(value),"0x%02X",settings.agc1);break;case 9:snprintf(value,sizeof(value),"0x%02X",settings.agc0);break;
            case 10:snprintf(value,sizeof(value),"0x%04X",settings.syncWord);break;case 11:snprintf(value,sizeof(value),"%u",settings.syncMode);break;case 12:snprintf(value,sizeof(value),"%s",settings.crc?"ON":"off");break;
            case 13:snprintf(value,sizeof(value),"%d dBm",settings.rssiThresholdDbm);break;case 14:snprintf(value,sizeof(value),"%lu",(unsigned long)settings.rxTimeoutMs);break;case 15:snprintf(value,sizeof(value),"%lu",(unsigned long)settings.txLimitMs);break;
            case 16:snprintf(value,sizeof(value),"%s",profile==TxProfile::ShieldedLab?"Shielded laboratory":"Receive only");break;case 17:snprintf(value,sizeof(value),"%d dBm uncalibrated",powerIndex==0?-10:powerIndex==1?0:10);break;case 18:snprintf(value,sizeof(value),"%lu",(unsigned long)sweepStep);break;
            case 19:snprintf(value,sizeof(value),"%s",settings.whitening?"ON":"off");break;case 20:snprintf(value,sizeof(value),"%s",settings.manchester?"ON":"off");break;case 21:snprintf(value,sizeof(value),"%s",settings.fec?"ON":"off");break;case 22:snprintf(value,sizeof(value),"%u bytes",settings.packetLength);break;
            default:snprintf(value,sizeof(value),"OK to open");break;
            }
            line(3,value);line(4,"Validated before apply");footer("UP/DN field OK edit BK");display.display();
        }
    }
    resetInput();
}
void generatorApp() {
    char value[8]="1234";if(!textEntry("LAB-OOK payload HEX",value,sizeof(value),"0123456789ABCDEF"))return;
    waveformCount=encodeLabOok(strtoul(value,nullptr,16),waveform,kWaveformCapacity);
    RegisterImage testRadio;loadPreset("AM270",testRadio);resetInput();
    while(true){
        input();if(back||holdBack)break;
        if(holdOk){uint32_t elapsed;int32_t error;bool sent=labTransmitRaw(waveform,waveformCount,testRadio,settings.frequencyHz,profile,powerIndex,settings.txLimitMs,elapsed,error);message("Signal Generator",sent?"One LAB-OOK24 frame sent":"TX blocked, radio error or manual stop");break;}
        if(frame()){title("Laboratory Generator");line(2,"LAB-OOK24 + CRC8");line(3,value);line(4,"One finite test frame");footer("hold OK send BACK exit");display.display();}
    }
    resetInput();
}
void explorerApp() {
    if(!startMode(LabMode::Explorer))return;resetInput();
    while(true){input();if(back||holdBack)break;if(frame()){LabSnapshot s=labSnapshot();title("Modulation Explorer");
        for(size_t i=0;i<4;++i){char b[40];snprintf(b,sizeof(b),"%c%s %ddB %lu edges",i==s.explorerPreset?'>':' ',presetAt(i)->name,s.explorerRssi[i],(unsigned long)s.explorerEdges[i]);line(i+2,b);}footer("Activity is a heuristic");display.display();}}
    labStop();resetInput();
}
void diagnosticsApp() {
    if(!startMode(LabMode::Monitor))return;size_t page=0;resetInput();
    while(true){
        input();if(back||holdBack)break;if(up)page=(page+3)%4;if(down)page=(page+1)%4;
        if(ok){labStop();startMode(LabMode::Monitor);}
        if(frame()){LabSnapshot s=labSnapshot();title("CC1101 Diagnostics");char b[64];
            if(page==0){snprintf(b,sizeof(b),"PART%02X VER%02X MARC%02X",s.part,s.version,s.marcState);line(2,b);snprintf(b,sizeof(b),"RSSI %d FIFO %02X CS%d",s.rssiDbm,s.rxBytes,s.carrier);line(3,b);snprintf(b,sizeof(b),"SPI%lu FIFO%lu T/O%lu",(unsigned long)s.spiErrors,(unsigned long)s.fifoErrors,(unsigned long)s.timeouts);line(4,b);}
            else if(page==1){snprintf(b,sizeof(b),"IRQ%lu drop%lu bad%lu",(unsigned long)s.edgeCount,(unsigned long)s.dropped,(unsigned long)s.rejected);line(2,b);snprintf(b,sizeof(b),"MISO%lu tail%lu",(unsigned long)s.misoTimeouts,(unsigned long)s.captureTailDiscarded);line(3,b);line(4,s.status);}
            else{size_t start=page==2?0:24;for(size_t row=0;row<4;++row){size_t r=start+row*6;size_t n=snprintf(b,sizeof(b),"%02X:",unsigned(r));for(size_t j=0;j<6&&r+j<kRegisterCount;++j)n+=snprintf(b+n,sizeof(b)-n,"%02X ",s.registers[r+j]);line(row+2,b);}}
            footer("UP/DN page OK recover");display.display();
            Serial.printf("[RadioLab] part=%02X ver=%02X marc=%02X freq=%lu rssi=%d fifo=%02X spi=%lu fifoerr=%lu timeout=%lu raw=%lu drop=%lu\n",s.part,s.version,s.marcState,(unsigned long)s.frequencyHz,s.rssiDbm,s.rxBytes,(unsigned long)s.spiErrors,(unsigned long)s.fifoErrors,(unsigned long)s.timeouts,(unsigned long)s.pulseCount,(unsigned long)s.dropped);
        }
    }
    labStop();resetInput();
}
struct History {uint16_t peer;bool sent;char text[161];};
History history[8];size_t historySize,historyNext;
void addHistory(uint16_t peer,bool sent,const char* text){History& h=history[historyNext];h.peer=peer;h.sent=sent;snprintf(h.text,sizeof(h.text),"%s",text);historyNext=(historyNext+1)%8;if(historySize<8)++historySize;}
void readHistory(){
    size_t selected=0,offset=0;resetInput();
    while(true){input();if(back||holdBack)break;if(up&&selected) {--selected;offset=0;}if(down&&selected+1<historySize){++selected;offset=0;}if(ok&&historySize){const History& h=history[(historyNext+8-1-selected)%8];offset=offset+60<strlen(h.text)?offset+60:0;}
        if(frame()){title("Chat history");if(historySize){const History& h=history[(historyNext+8-1-selected)%8];char b[32];if(h.peer)snprintf(b,sizeof(b),"%s %04X (%u/%u)",h.sent?"To":"From",h.peer,unsigned(selected+1),unsigned(historySize));else snprintf(b,sizeof(b),"Broadcast %s (%u/%u)",h.sent?"TX":"RX",unsigned(selected+1),unsigned(historySize));line(2,b);for(size_t row=0;row<3;++row){size_t pos=offset+row*20;if(pos<strlen(h.text)){snprintf(b,21,"%.20s",h.text+pos);display.setCursor(0,28+row*8);display.print(b);}}}else line(3,"No messages yet");footer("UP/DN item OK page BK");display.display();}}
    resetInput();
}
void chatApp(){
    if(!laboratoryChannel(settings.frequencyHz)){message("Sub-GHz Chat","Select 433920000 or 868350000 Hz in Radio Settings. Both devices must match.");return;}
    if(!startMode(LabMode::Chat))return;uint16_t recipient=0;size_t selected=0;static const char* items[]={"Inbox / sent history","Compose message","Discover devices","Select recipient","Delivery status","Mode: ESP ACK"};resetInput();
    while(true){
        input();uint16_t source;char received[161];while(labReceiveMessage(source,received,sizeof(received)))addHistory(source,false,received);
        const bool plaintext=chatFormat==ChatMode::FlipperPlaintext;
        if(back||holdBack)break;if(up)selected=(selected+5)%6;if(down)selected=(selected+1)%6;
        if(ok){
            if(selected==0)readHistory();
            if(selected==1){char text[161]="";if(textEntry("Compose ASCII",text,sizeof(text))){LabSnapshot s=labSnapshot();uint16_t to=plaintext?0:(recipient?recipient:s.peer);if(labQueueText(text,to)){addHistory(to,true,text);showNotice(plaintext?"Queued broadcast; no ACK":"Queued");}else showNotice("Queue full or TX off");}}
            if(selected==2){if(!labDiscover())showNotice("TX off or command queue full");else showNotice(plaintext?"Presence queued; no ACK":"Discovery queued");}
            if(selected==3){if(plaintext)message("Flipper plaintext","Broadcast to all listeners. Select No encryption in Enhanced Sub-GHz Chat and use the same frequency. No peer IDs or acknowledgements.");else{LabSnapshot s=labSnapshot();size_t index=0;for(size_t i=0;i<8;++i)if(s.peers[i]==recipient)index=i+1;recipient=s.peers[index%8];}}
            if(selected==4){LabSnapshot s=labSnapshot();char b[190];if(plaintext)snprintf(b,sizeof(b),"State %u: 0idle 1queued\n2sending 3sent 4failed\nSent means local RF only.\nNo acknowledgement.\nRX%lu TX%lu\nRejected%lu truncated%lu",s.delivery,(unsigned long)s.receivedMessages,(unsigned long)s.sentMessages,(unsigned long)s.plaintextRejected,(unsigned long)s.plaintextTruncated);else snprintf(b,sizeof(b),"Delivery %u\n0idle 1queued 2sending\n3delivered 4failed\nRX%lu TX%lu attempt%lu",s.delivery,(unsigned long)s.receivedMessages,(unsigned long)s.sentMessages,(unsigned long)s.chatRetries);message("Delivery",b);}
            if(selected==5){if(!labStop()){showNotice("Radio still stopping");continue;}chatFormat=plaintext?ChatMode::EspAck:ChatMode::FlipperPlaintext;recipient=0;historySize=historyNext=0;if(!startMode(LabMode::Chat))break;showNotice(chatFormat==ChatMode::FlipperPlaintext?"Flipper plaintext; encryption off":"ESP ACK protocol");}
            resetInput();
        }
        if(frame()){LabSnapshot s=labSnapshot();const bool plain=chatFormat==ChatMode::FlipperPlaintext;title(plain?"Flipper plaintext":"Sub-GHz Chat / ACK");char b[40];if(plain)snprintf(b,sizeof(b),"%lu Hz %ddBm",(unsigned long)s.frequencyHz,s.rssiDbm);else snprintf(b,sizeof(b),"Me%04X To%04X %ddBm",s.self,recipient?recipient:s.peer,s.rssiDbm);line(2,b);line(3,selected==5?(plain?"Mode: Flipper plain":"Mode: ESP ACK"):selected==2&&plain?"Send presence":selected==3&&plain?"Compatibility info":items[selected]);line(4,profile==TxProfile::ShieldedLab?"TX: shielded laboratory":"TX off: Radio Settings");footer("UP/DN item OK open BK");display.display();}
    }
    labStop();resetInput();
}
}

void runRadioLab() {
    using namespace research;
    suspendLegacyReceive();
    settings=RadioSettings();settings.rxTimeoutMs=30000;selectPreset(0);profile=TxProfile::ReceiveOnly;powerIndex=0;chatFormat=ChatMode::FlipperPlaintext;
    SD.mkdir("/subghz");SD.mkdir("/subghz/presets");
    static const char* apps[]={"Read laboratory","Read RAW","Frequency Analyzer","Spectrum Analyzer","RAW Signal Analyzer","Protocol Analyzer","Signal Manager","Signal Generator","Radio Settings","Modulation Explorer","Sub-GHz Chat","Music Lab","CC1101 Diagnostics","About"};
    const size_t count=sizeof(apps)/sizeof(apps[0]);size_t selected=0;resetInput();
    while(true){
        input();if(back||holdBack)break;if(up)selected=(selected+count-1)%count;if(down)selected=(selected+1)%count;
        if(ok){switch(selected){case 0:captureApp(true);break;case 1:captureApp(false);break;case 2:monitorApp(false);break;case 3:monitorApp(true);break;case 4:analyzerApp(false);break;case 5:analyzerApp(true);break;case 6:browser();break;case 7:generatorApp();break;case 8:settingsApp();break;case 9:explorerApp();break;case 10:chatApp();break;case 11:browser(true);break;case 12:diagnosticsApp();break;case 13:message("ESP-HACK Radio Lab","ESP32 + CC1101\nSH1106 / 4 buttons\n26MHz crystal assumed\nHardware validation pending\nCC1101 is not an SDR");break;}resetInput();}
        if(frame()){title("Sub-GHz / Radio Lab");for(size_t row=0;row<4;++row){size_t i=(selected+row)%count;display.setCursor(0,17+row*9);display.print(row?"  ":"> ");display.print(apps[i]);}footer("UP/DN select OK open");display.display();}
    }
    // Ownership cannot pass back to legacy code until the worker has stopped.
    while(!labStop()) { showNotice("Waiting for radio worker to stop"); delay(10); }
    restoreReceiveMode();resetInput();
}

bool radioLabPermitTx(uint32_t hz,uint32_t durationMs) {
    return research::permitLabTx(profile,hz,durationMs,settings.txLimitMs);
}

void runRadioLabFile(const char* path) {
    using namespace research;
    suspendLegacyReceive();
    if(!inspectFile(path)) { message("File validation",notice); return; }
    metadataScreen();
    RadioSettings fileSettings;
    if(!waveformCount || openedInfo.kind!=SubKind::Raw ||
       !settingsFromSub(openedInfo,fileSettings) || fileSettings.modulation!=Modulation::AskOok) {
        message("RAW transmission","Requires binary OOK RAW, <=2048 pulses. Unsupported files are not substituted.");return;
    }
    resetInput();
    while(true) {
        input();if(back||holdBack)break;
        if(holdOk) {
            uint32_t elapsed;int32_t error;
            bool sent=labTransmitRaw(waveform,waveformCount,openedInfo.radio,openedInfo.frequencyHz,profile,powerIndex,settings.txLimitMs,elapsed,error);
            char result[128];snprintf(result,sizeof(result),"%s\nWall duration %lu us\nCompletion delta %ld us",sent?"Finite waveform sent":"TX blocked/aborted/error",(unsigned long)elapsed,(long)error);
            message("RAW transmission",result);break;
        }
        if(frame()){title("RAW / Music Lab");line(2,profile==TxProfile::ShieldedLab?"Shielded lab enabled":"RX only: enable in Lab");line(3,"Hold OK finite playback");line(4,"BACK manual stop");footer("hold OK send BACK exit");display.display();}
    }
    resetInput();
}

extern bool setupCC1101();
void radioLabBootDiagnostics() {
    Serial.printf("[RadioLab] firmware=%s esp_heap=%u crystal_cc1101_assumed=26000000\n",FIRMWARE,ESP.getFreeHeap());
    const bool ready=setupCC1101();
    Serial.printf("[RadioLab] probe=%s part=%02X version=%02X marc=%02X IOCFG0=%02X PKTCTRL0=%02X MDMCFG4=%02X MDMCFG2=%02X readyTO=%lu\n",
        ready?"PASS":"FAIL",ELECHOUSE_cc1101.SpiReadStatus(0x30),ELECHOUSE_cc1101.SpiReadStatus(0x31),
        ELECHOUSE_cc1101.SpiReadStatus(0x35)&0x1F,ELECHOUSE_cc1101.SpiReadReg(2),ELECHOUSE_cc1101.SpiReadReg(8),
        ELECHOUSE_cc1101.SpiReadReg(0x10),ELECHOUSE_cc1101.SpiReadReg(0x12),(unsigned long)ELECHOUSE_cc1101.spiTimeouts());
    ELECHOUSE_cc1101.SpiStrobe(0x36);pinMode(CC1101_GDO0,INPUT);
    Serial.println("[RadioLab] receive-only boot probe complete; RF waveform validation pending");
}
void radioLabSerialPoll() {
    static char command[160];static uint8_t used;
    while(Serial.available()) {
        const char c=Serial.read();
        if(c=='\r')continue;
        if(c=='\n') {
            command[used]=0;
            if(handleAppInstallCommand(command)){}
            else if(!strcmp(command,"rfdiag"))radioLabBootDiagnostics();
            else if(!strcmp(command,"appsdiag"))sdAppsSelfTest();
            else if(used)Serial.println("[RadioLab] commands: rfdiag, appsdiag, APP_PUT");
            used=0;
        } else if(c>=32&&c<=126&&used+1<sizeof(command))command[used++]=c;
        else used=0;
    }
}
void ensureRadioLabFixture() {
    const char* path="/subghz/lab/LAB_OOK24_1234.sub";
    SD.mkdir("/subghz/presets");SD.mkdir("/subghz/lab");
    if(SD.exists(path))return;
    File file=SD.open(path,FILE_WRITE);if(!file)return;
    int32_t pulses[52];const size_t count=research::encodeLabOok(0x1234,pulses,52);
    research::RawWriter writer(textToFile,&file);
    bool valid=writer.begin(433920000,"AM270");
    for(size_t i=0;valid&&i<count;++i)valid=writer.pulse(pulses[i]);
    valid=valid&&writer.finish();file.flush();file.close();
    if(!valid)SD.remove(path);
    Serial.printf("[RadioLab] laboratory fixture %s\n",valid?"created":"write failed");
}
