#include "lab_suite.h"
#include "lab_service.h"
#include "sub_file.h"
#include "raw_capture.h"
#include "test_decoder.h"
#include "app_install.h"
#include "lua_apps.h"
#include "chat_profile.h"
#include "rf_clock_probe.h"
#include "radio_calibration.h"
#include "trim_math.h"
#include "static_sub_encoder.h"
#include "file_page.h"
#include "file_entry.h"
#include "../interface/ui_text.h"
#include "../interface/keyboard_model.h"
#include "../../shared/flipper_chat_compat.h"
#include "../../shared/chat_session.h"
#include "../subghz.h"
#include <SD.h>
#include <esp_system.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

extern bool inMenu;
void returnToMainMenu();

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
bool openedFileValid;
FileEntry fileEntries[FilePage::kEntries];
bool fileHasNext, folderOpenError;
size_t pathCount, fileEntryCount;
uint64_t matchingFileCount;
uint32_t filePage, longPathCount;
char activePreset[64] = "AM270";
char currentFolder[kRadioPathCapacity] = "/subghz";
char selectedFilePath[kRadioPathCapacity];
char operationPath[kRadioPathCapacity];
char filter[32] = "";
char notice[80] = "";
char fileErrorRu[160] = "";
bool radioLabHomeOpen;
uint32_t lastFrame;
bool up, down, ok, back, holdOk, holdBack, holdUp, holdDown;
bool serialChatContext, serialChatListen, serialChatReported, serialChatCloseRequested;
bool serialChatOpenPending;
bool serialChatEditPending, serialChatEditing;

__attribute__((noinline)) void printChatStatus() {
    const LabSnapshot s=labSnapshot();
    Serial.printf("[Chat] active=%u connected=%u mode=%s diagnostic=%u freq=%lu TX=%s reason=%s\n",
        s.active,s.connected,s.chatMode==ChatMode::FlipperPlaintext?"plain":"ack",s.chatDiagnostic,
        (unsigned long)s.frequencyHz,s.txEnabled?"enabled":"OFF",s.txBlockReason);
    Serial.printf("[Chat] marc=%02X fifo=%02X pkt=%02X rssi=%d peak=%d sync=%lu rearm=%lu packets=%lu crc=%lu fifoerr=%lu spi=%lu miso=%lu RX=%lu TX=%lu reject=%lu trunc=%lu len=%u lqi=%u\n",
        s.marcState,s.rxBytes,s.pktStatus,s.rssiDbm,s.peakDbm,(unsigned long)s.syncEvents,(unsigned long)s.rxRearms,
        (unsigned long)s.packets,(unsigned long)s.crcErrors,(unsigned long)s.fifoErrors,(unsigned long)s.spiErrors,
        (unsigned long)s.misoTimeouts,(unsigned long)s.receivedMessages,(unsigned long)s.sentMessages,
        (unsigned long)s.plaintextRejected,(unsigned long)s.plaintextTruncated,s.lastPacketLength,s.lastLqi);
    if(s.packets || s.crcErrors)Serial.printf("[Chat] lastPacket rssi=%.1f freqestHz=%ld crc=%u statusCRC=%u statusLqi=%u peakRssi=%d\n",
        s.packetRssiHalfDbm / 2.0, (long)s.packetFrequencyErrorHz, s.lastCrcOk, s.lastStatusCrcOk, s.lastStatusLqi, s.peakDbm);
    Serial.printf("[Chat] configured IOCFG0=%02X PKTLEN=%02X PKTCTRL1=%02X PKTCTRL0=%02X SYNC=%02X%02X MDMCFG=%02X/%02X/%02X MCSM1=%02X\n",
        s.registers[2],s.registers[6],s.registers[7],s.registers[8],s.registers[4],s.registers[5],
        s.registers[0x10],s.registers[0x11],s.registers[0x12],s.registers[0x17]);
    Serial.printf("[Chat] configured FREQ=%02X%02X%02X FSCTRL=%02X/%02X MDMCFG1/0=%02X/%02X DEVIATN=%02X BSCFG=%02X FOCCFG=%02X AGC=%02X/%02X/%02X FREND=%02X/%02X\n",
        s.registers[0x0D],s.registers[0x0E],s.registers[0x0F],s.registers[0x0B],s.registers[0x0C],
        s.registers[0x13],s.registers[0x14],s.registers[0x15],s.registers[0x1A],s.registers[0x19],
        s.registers[0x1B],s.registers[0x1C],s.registers[0x1D],s.registers[0x21],s.registers[0x22]);
    Serial.printf("[Chat] cached FSCAL=%02X/%02X/%02X/%02X TEST=%02X/%02X/%02X\n",
        s.registers[0x23],s.registers[0x24],s.registers[0x25],s.registers[0x26],
        s.registers[0x2C],s.registers[0x2D],s.registers[0x2E]);
    if(s.message[0])Serial.printf("[Chat] last=%s\n",s.message);
}

void input() {
    if(serialChatContext)radioLabSerialPoll();
    buttonUp.tick(); buttonDown.tick(); buttonOK.tick(); buttonBack.tick();
    up = buttonUp.isClick(); down = buttonDown.isClick();
    ok = buttonOK.isClick(); back = buttonBack.isClick();
    holdOk = buttonOK.isHolded(); holdBack = buttonBack.isHolded();
    holdUp = buttonUp.isHolded(); holdDown = buttonDown.isHolded();
    // A serial close unwinds the same nested chat dialogs as a held BACK.
    // Suppress simultaneous selection so closing cannot queue another send.
    if(serialChatContext && serialChatCloseRequested) {
        up = down = ok = holdOk = holdUp = holdDown = false; back = holdBack = true;
    }
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
void line(uint8_t row, const char* text) { ui_text::draw(display, 0, row * 9 + 2, text); }
void title(const char* text) {
    display.fillRect(0, 0, 128, 11, SH110X_WHITE);
    display.setTextColor(SH110X_BLACK); ui_text::draw(display, 2, 2, text, SH110X_BLACK);
    display.setTextColor(SH110X_WHITE);
}
void footer(const char* text) { display.drawFastHLine(0, 53, 128, SH110X_WHITE); ui_text::draw(display, 0, 56, text); }
void showNotice(const char* text) { snprintf(notice, sizeof(notice), "%s", text); Serial.printf("[RadioLab] %s\n", notice); }
void message(const char* heading, const char* text) {
    char rows[32][43]={};size_t row=0,column=0,bytes=0,offset=0;
    const size_t length=strlen(text);
    for(size_t at=0;at<length&&row<32;) {
        if(text[at]=='\n'){++row;column=bytes=0;++at;continue;}
        if(column==21){++row;column=bytes=0;if(row==32)break;}
        uint16_t codepoint;const size_t width=ui_text::decodeGlyph(text+at,length-at,codepoint);
        if(width){memcpy(rows[row]+bytes,text+at,width);bytes+=width;at+=width;}
        else{rows[row][bytes++]='?';++at;}
        ++column;
    }
    const size_t total=row<32?row+1:32;
    resetInput();
    while(true) {
        input(); if(ok || back || holdBack) break;
        if(up&&offset)--offset;
        if(down&&offset+4<total)++offset;
        if(frame()) {
            title(heading);
            for(size_t i=0;i<4&&offset+i<total;++i)ui_text::draw(display,0,15+i*8,rows[offset+i]);
            footer("UP/DN лист OK/BACK"); display.display();
        }
    }
    resetInput();
}
bool textEntry(const char* heading, char* output, size_t capacity, const char* alphabet = nullptr) {
    // Keep the existing ASCII character set. Alphabet pages group lowercase,
    // uppercase and digits; caller-supplied numeric/filename alphabets stay exact.
    static const char keys[] = " abcdefghijklmnopqrstuvwxyz._ABCDEFGHIJKLMNOPQRSTUVWXYZ-!0123456789?/@";
    if(!alphabet) alphabet = keys;
    char draft[161];
    if(!output || !capacity || capacity>sizeof(draft))return false;
    const char* end=static_cast<const char*>(memchr(output,0,capacity));
    if(!end)return false;
    memcpy(draft,output,size_t(end-output)+1);
    keyboard::Model editor(draft, capacity, alphabet, strlen(alphabet));
    if(!editor.valid())return false;
    struct EditingGuard {
        bool previous;
        EditingGuard():previous(serialChatEditing){serialChatEditing=true;}
        ~EditingGuard(){serialChatEditing=previous;}
    } editingGuard;
    uint32_t fullNoticeAt=millis()-1500;
    resetInput();
    while(editor.outcome()==keyboard::Outcome::Editing) {
        input();
        if(holdBack)editor.cancel();
        else if(holdOk)editor.accept();
        else {
            if(holdUp)editor.changePage(-1);else if(up)editor.move(-1);
            if(holdDown)editor.changePage(1);else if(down)editor.move(1);
            if(back)editor.erase();
            if(ok && editor.activate()==keyboard::Result::Full)fullNoticeAt=millis();
        }
        if(editor.outcome()!=keyboard::Outcome::Editing)break;
        if(frame()) {
            display.clearDisplay();
            display.fillRect(0,0,128,11,SH110X_WHITE);
            ui_text::draw(display,2,2,heading,SH110X_BLACK,13);
            char counter[16];snprintf(counter,sizeof(counter),"%u/%u",unsigned(editor.length()),unsigned(capacity-1));
            ui_text::draw(display,86,2,counter,SH110X_BLACK,7);
            const char* preview=editor.tail(20);ui_text::draw(display,0,12,preview,SH110X_WHITE,20);
            if((millis()/500)%2)display.drawFastHLine(strlen(preview)*6,20,5,SH110X_WHITE);
            for(size_t i=0;i<32;++i) {
                const keyboard::Cell cell=editor.cell(i);
                if(cell.kind==keyboard::Kind::Empty)continue;
                const int16_t x=(i%8)*16,y=22+(i/8)*8;
                const bool selected=i==editor.selected();
                if(selected)display.fillRect(x,y,16,8,SH110X_WHITE);
                const char* label="";char glyph[2]={cell.character,0};
                switch(cell.kind) {
                case keyboard::Kind::Character:label=glyph;break;
                case keyboard::Kind::Space:label="SP";break;
                case keyboard::Kind::Delete:label="<-";break;
                case keyboard::Kind::Next:label=">>";break;
                case keyboard::Kind::Done:label="OK";break;
                default:break;
                }
                const uint16_t ink=selected?SH110X_BLACK:SH110X_WHITE;
                ui_text::draw(display,x+(strlen(label)==1?5:2),y,label,ink,2);
                if(!cell.enabled)display.drawFastHLine(x+2,y+3,12,ink);
            }
            const keyboard::Cell selected=editor.cell(editor.selected());
            char hint[64];
            if(uint32_t(millis()-fullNoticeAt)<1500)snprintf(hint,sizeof(hint),"Поле заполнено");
            else if(selected.kind==keyboard::Kind::Space)snprintf(hint,sizeof(hint),"%s",selected.enabled?"OK: пробел":"Пробел недоступен");
            else if(selected.kind==keyboard::Kind::Delete)snprintf(hint,sizeof(hint),"%s",selected.enabled?"OK: стереть символ":"Нечего стирать");
            else if(selected.kind==keyboard::Kind::Next) {
                if(selected.enabled)snprintf(hint,sizeof(hint),"OK: страница %u/%u",unsigned(editor.page()+1),unsigned(editor.pages()));
                else snprintf(hint,sizeof(hint),"Одна страница");
            }
            else if(selected.kind==keyboard::Kind::Done)snprintf(hint,sizeof(hint),"OK: готово");
            else if((millis()/2500)%3==0)snprintf(hint,sizeof(hint),"UP/DN: выбор OK: ввод");
            else if((millis()/2500)%3==1)snprintf(hint,sizeof(hint),"Держ.OK/BACK:да/нет");
            else snprintf(hint,sizeof(hint),"Держ.UP/DN:стр.%u/%u",unsigned(editor.page()+1),unsigned(editor.pages()));
            footer(hint);display.display();
        }
    }
    resetInput();
    const bool accepted=editor.outcome()==keyboard::Outcome::Accepted;
    if(accepted)memcpy(output,draft,editor.length()+1);
    return accepted;
}
bool validLeaf(const char* name) {
    if(!name || !*name || *name == '.' || strlen(name) > 48) return false;
    for(const char* p = name; *p; ++p)
        if(!isalnum(uint8_t(*p)) && *p != '_' && *p != '-' && *p != '.') return false;
    return !strstr(name, "..");
}
bool safePath(const char* path) {
    return radioFilePath(path);
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
    fileErrorRu[0]=0;openedFileValid=false;waveformCount=0;
    openedInfo.kind=SubKind::Unknown;openedInfo.protocol[0]=0;
    if(!labStop()) { showNotice("Radio stop timed out");snprintf(fileErrorRu,sizeof(fileErrorRu),"Радио не остановилось."); return false; }
    File file = SD.open(path, FILE_READ);
    if(!file) { showNotice("SD open failed");snprintf(fileErrorRu,sizeof(fileErrorRu),"Не удалось открыть\nфайл на SD."); return false; }
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
        snprintf(fileErrorRu,sizeof(fileErrorRu),"Строка %lu:\n%s",(unsigned long)parser.line(),parser.errorTextRu());
        showNotice(error); waveformCount = 0; return false;
    }
    openedInfo = parser.info(); waveformCount = sink.full ? 0 : sink.used;openedFileValid=true;
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
    if(settings.packetFormat != PacketFormat::Async) { message("Приём RAW", "Выберите сигнал AM/FM\nв настройках приёма."); return; }
    if(!startMode(LabMode::Raw)) return;
    resetInput();
    while(true) {
        input(); if(back || holdBack) break;
        if(ok) {
            saveCapture();const char* result=notice;
            if(!strcmp(notice,"No complete RSSI-triggered burst"))result="Нет целого сигнала.\nНажмите кнопку пульта\nи дождитесь приёма.";
            else if(!strcmp(notice,"Damaged/full capture: not saved"))result="Запись повреждена.\nПовторите приём.";
            else if(!strcmp(notice,"SD save failed"))result="Ошибка записи на SD.";
            else if(!strcmp(notice,"Radio still active"))result="Радио останавливается";
            message("Результат приёма",result);break;
        }
        if(frame()) {
            const LabSnapshot s = labSnapshot(); title(protocol ? "Протокол LAB" : "Принять сигнал");
            char b[48]; snprintf(b, sizeof(b), "%lu.%03lu %s %dдБм", (unsigned long)(settings.frequencyHz/1000000),
                    (unsigned long)((settings.frequencyHz/1000)%1000), activePreset, s.rssiDbm); line(2, b);
            snprintf(b, sizeof(b), "Имп.%lu потерь%lu", (unsigned long)s.pulseCount, (unsigned long)s.dropped); line(3, b);
            if(protocol) {
                const size_t n = labCopyPulses(waveform, kWaveformCapacity); DecodedSignal decoded;
                if(decodeLabOok(waveform, n, decoded)) { snprintf(b, sizeof(b), "LAB-OOK24: %04lX CRC OK", (unsigned long)decoded.payload); line(4, b); }
                else line(4, "Ожидание LAB-OOK24");
            } else line(4,s.captureComplete?"Принято: OK записать":s.captureTriggered?"Запись сигнала...":"Ждём кнопку пульта");
            footer("OK:запись BACK:выход"); display.display();
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
    // At most39 actual paths plus one explicit navigation row. A second scan
    // is needed only when a deletion made the requested last page disappear.
    for(unsigned pass=0;pass<2;++pass) {
        pathCount=fileEntryCount=0;longPathCount=0;folderOpenError=false;fileHasNext=false;matchingFileCount=0;
        File folder=SD.open(currentFolder);if(!folder){folderOpenError=true;return;}
        FilePage page(filePage);uint32_t ordinal=0;unsigned scanned=0;
        // Directory names avoid opening/stat'ing every payload while scanning.
        // Only one transient Arduino String exists, regardless of directory size.
        while(true) {
            bool directory=false;String name=folder.getNextFileName(&directory);
            if(!name.length())break;
            const char* full=name.c_str();const char* leaf=strrchr(full,'/');leaf=leaf?leaf+1:full;
            if(leaf&&*leaf&&*leaf!='.'&&(!*filter||strstr(leaf,filter))) {
                if(strlen(currentFolder)+1+strlen(leaf)>=kRadioPathCapacity) {
                    if(longPathCount!=UINT32_MAX)++longPathCount;
                } else if(page.consider()) {
                    if(setFileEntry(fileEntries[fileEntryCount],leaf,ordinal,directory))++fileEntryCount;
                }
            }
            if(ordinal!=UINT32_MAX)++ordinal;
            if((++scanned&15)==0)delay(1);
        }
        folder.close();
        if(filePage>page.lastPage()){filePage=page.lastPage();continue;}
        fileHasNext=page.more();matchingFileCount=page.total();pathCount=fileEntryCount+unsigned(fileHasNext);
        return;
    }
}
bool resolveFileEntry(size_t selected) {
    selectedFilePath[0]=0;
    if(selected>=fileEntryCount)return false;
    const FileEntry& expected=fileEntries[selected];
    File folder=SD.open(currentFolder);if(!folder)return false;
    uint32_t ordinal=0;bool found=false;
    while(true) {
        bool directory=false;String name=folder.getNextFileName(&directory);
        if(!name.length())break;
        if(ordinal==expected.ordinal) {
            const char* full=name.c_str();const char* leaf=strrchr(full,'/');leaf=leaf?leaf+1:full;
            found=sameFileEntry(expected,leaf,ordinal,directory)&&
                  completeFilePath(selectedFilePath,sizeof selectedFilePath,currentFolder,leaf)&&safePath(selectedFilePath);
            break;
        }
        if(ordinal==UINT32_MAX)break;
        ++ordinal;if(!(ordinal&15))delay(1);
    }
    folder.close();return found;
}
void metadataScreen() {
    char text[320];
    if(openedInfo.kind==SubKind::Raw)snprintf(text,sizeof(text),"%lu.%03lu МГц\n%.63s\nRAW\nИмпульсов: %lu\nДлина: %lu мс\n%llu байт RAW",
        (unsigned long)(openedInfo.frequencyHz/1000000),(unsigned long)((openedInfo.frequencyHz/1000)%1000),openedInfo.preset,
        (unsigned long)openedInfo.raw.pulses,(unsigned long)(openedInfo.raw.durationUs/1000),
        (unsigned long long)openedInfo.raw.bufferedBytes());
    else snprintf(text,sizeof(text),"%lu.%03lu МГц\n%.63s\n%.63s\nБит: %u\nTE: %lu мкс\nGuard: %u\n%s",
        (unsigned long)(openedInfo.frequencyHz/1000000),(unsigned long)((openedInfo.frequencyHz/1000)%1000),openedInfo.preset,openedInfo.protocol,
        openedInfo.bits,(unsigned long)openedInfo.teUs,openedInfo.guardTime,
        dynamicSubProtocol(openedInfo.protocol)?"Передача протокола\nне поддержана.":"");
    message("О файле",text);
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
                char name[64]="";char* destination=operationPath;
                if(selected==4) {
                    if(!textEntry("Folder under /subghz",name,sizeof(name),"abcdefghijklmnopqrstuvwxyz0123456789_-"))continue;
                    if(!validLeaf(name)){message("Path","Invalid folder");continue;}
                    char folder[80];snprintf(folder,sizeof(folder),"/subghz/%s",name);SD.mkdir(folder);
                    const char* leaf=strrchr(path,'/');
                    if(!completeFilePath(destination,kRadioPathCapacity,folder,leaf?leaf+1:path)){message("Путь","Путь длиннее 1023 байт.\nФайл не изменён.");continue;}
                } else {
                    if(!textEntry("File name (no .sub)",name,sizeof(name),"abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-"))continue;
                    if(!validLeaf(name)){message("Path","Invalid file name");continue;}
                    char leaf[sizeof(name)+4];snprintf(leaf,sizeof(leaf),"%s.sub",name);
                    if(!completeFilePath(destination,kRadioPathCapacity,presetMode?"/subghz/presets":currentFolder,leaf)){message("Путь","Путь длиннее 1023 байт.\nФайл не изменён.");continue;}
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
const char* prepareOpenedFileSend(StaticSubWaveform& generated) {
    if(!openedFileValid)return "Сначала откройте файл.";
    if(openedInfo.kind==SubKind::Key) {
        waveformCount=0;
        const StaticSubError result=encodeStaticSub(openedInfo,waveform,kWaveformCapacity,generated);
        if(result!=StaticSubError::None)return staticSubErrorRu(result);
        waveformCount=generated.count;
    } else if(openedInfo.kind==SubKind::Raw) {
        if(openedInfo.raw.pulses>kWaveformCapacity)return "Запись длиннее 2048\nимпульсов. Только\nпросмотр файла.";
        if(openedInfo.raw.durationUs>5000000)return "Запись длиннее 5 сек.\nТолько просмотр файла.";
        if(!openedInfo.rawCompatible||!waveformCount)return "Этот RAW нельзя\nпередать как OOK.";
        if(waveform[0]<=0)return "RAW начинается с LOW.\nПередача не поддержана.";
        generated.count=waveformCount;generated.durationUs=uint32_t(openedInfo.raw.durationUs);generated.repeats=1;generated.phaseRotated=false;
    } else return "Тип файла не подходит\nдля передачи.";
    RadioSettings description;
    if(!describeRadio(openedInfo.radio,openedInfo.frequencyHz,description) ||
       description.modulation!=Modulation::AskOok || description.packetFormat!=PacketFormat::Async ||
       description.crc || description.whitening || description.manchester || description.fec || description.syncMode)
        return "Нужен OOK RAW или\nподдерживаемый\nстатический протокол.";
    if(openedInfo.radio.pa[0])return "Многоуровневый ASK:\nпередача не поддержана.";
    for(size_t i=2;i<8;++i)if(openedInfo.radio.pa[i])return "Многоуровневый ASK:\nпередача не поддержана.";
    if(openedInfo.radio.present[0x22]&&(openedInfo.radio.value[0x22]&7)!=1)return "Не подходит PA-профиль\nфайла для OOK.";
    if(!permitLabTx(TxProfile::ShieldedLab,openedInfo.frequencyHz,(generated.durationUs+999)/1000,5000))
        return "Передача только на\n433.920 / 868.350 МГц.\nЧастота файла сохранена.";
    return nullptr;
}
void sendOpenedFile(const char* path) {
    StaticSubWaveform generated={0,0,0,false};const char* error=prepareOpenedFileSend(generated);
    if(error){char detail[256];const char* reason=dynamicSubProtocol(openedInfo.protocol)?"Передача протокола\nне поддержана.":error;
        snprintf(detail,sizeof detail,"Протокол: %.63s\n%s",openedInfo.protocol,reason);message("Отправка",detail);return;}
    const RadioSettings savedSettings=settings;const RegisterImage savedRadio=radio;
    const TxProfile savedProfile=profile;const uint8_t savedPower=powerIndex;
    RadioSettings fileSettings;
    if(!describeRadio(openedInfo.radio,openedInfo.frequencyHz,fileSettings)){message("Отправка","Ошибка параметров\nрадио в файле.");return;}
    settings=fileSettings;settings.txLimitMs=5000;radio=openedInfo.radio;profile=TxProfile::ShieldedLab;powerIndex=1;
    resetInput();
    while(true) {
        input();if(back||holdBack)break;
        if(holdOk) {
            uint32_t elapsed=0;int32_t timingError=0;
            const bool sent=labTransmitRaw(waveform,waveformCount,openedInfo.radio,openedInfo.frequencyHz,profile,powerIndex,5000,elapsed,timingError);
            char result[200];snprintf(result,sizeof(result),"%s\n%lu мс\nПриём устройством\nне подтверждён.",sent?"Передача завершена.":"Не передано: ошибка\nили остановка BACK.",(unsigned long)(elapsed/1000));
            message("Отправка",result);break;
        }
        if(frame()) {
            title("Отправить .sub");const char* leaf=strrchr(path,'/');line(2,leaf?leaf+1:path);
            char detail[64];snprintf(detail,sizeof(detail),"%lu.%03lu МГц 0дБм",(unsigned long)(openedInfo.frequencyHz/1000000),(unsigned long)((openedInfo.frequencyHz/1000)%1000));line(3,detail);
            snprintf(detail,sizeof(detail),"%s %lu мс",openedInfo.kind==SubKind::Raw?"RAW 1 проход":"6 повторов",(unsigned long)(generated.durationUs/1000));line(4,detail);
            footer("Держ.OK:слать BACK");display.display();
        }
    }
    settings=savedSettings;radio=savedRadio;profile=savedProfile;powerIndex=savedPower;resetInput();
}
void friendlyFileActions(const char* path) {
    static const char* actions[]={"Отправить","О файле","Ещё"};size_t selected=0;resetInput();
    while(true) {
        input();if(back||holdBack)break;if(up)selected=(selected+2)%3;if(down)selected=(selected+1)%3;
        if(ok) {
            if(selected==2){fileActions(path,false);break;}
            if(!inspectFile(path)){message("Ошибка файла",fileErrorRu[0]?fileErrorRu:"Не удалось прочитать.");resetInput();continue;}
            if(selected==0)sendOpenedFile(path);else metadataScreen();resetInput();
        }
        if(frame()) {
            title("Файл .sub");const char* leaf=strrchr(path,'/');ui_text::draw(display,0,14,leaf?leaf+1:path);
            for(size_t row=0;row<3;++row){display.setCursor(0,25+row*9);display.print(row==selected?"> ":"  ");ui_text::print(display,actions[row]);}
            footer("UP/DN OK:вход BACK");display.display();
        }
    }
    resetInput();
}
void browser(bool music=false,bool presets=false,bool friendly=false) {
    if(!labStop())return;
    snprintf(currentFolder,sizeof(currentFolder),"%s",presets?"/subghz/presets":"/subghz");filter[0]=0;filePage=0;
    listFiles();size_t selected=0;resetInput();
    while(true) {
        input();
        if(holdBack)break;
        if((holdDown&&fileHasNext)||(holdUp&&filePage)) {
            if(holdDown){if(filePage<UINT32_MAX)++filePage;}else --filePage;
            listFiles();selected=0;resetInput();continue;
        }
        if(back){if(filePage){--filePage;listFiles();selected=0;}else{char* slash=strrchr(currentFolder,'/');if(slash&&slash>currentFolder+1){*slash=0;filePage=0;listFiles();selected=0;}else break;}}
        if(up&&pathCount)selected=(selected+pathCount-1)%pathCount;
        if(down&&pathCount)selected=(selected+1)%pathCount;
        if(holdOk){if(textEntry("Поиск: пусто = все",filter,sizeof(filter))){filePage=0;listFiles();selected=0;}resetInput();}
        if(ok&&pathCount) {
            if(fileHasNext&&selected==fileEntryCount) {if(filePage<UINT32_MAX)++filePage;listFiles();selected=0;resetInput();continue;}
            if(!resolveFileEntry(selected)){message("Файлы","Папка изменилась.\nОткройте файл снова.");listFiles();selected=0;resetInput();continue;}
            if(fileEntries[selected].directory) {memcpy(currentFolder,selectedFilePath,strlen(selectedFilePath)+1);filePage=0;listFiles();selected=0;}
            else if(music) {
                if(inspectFile(selectedFilePath)) {
                    metadataScreen();
                    RadioSettings fileSettings;bool ook=describeRadio(openedInfo.radio,openedInfo.frequencyHz,fileSettings)&&fileSettings.modulation==Modulation::AskOok;
                    if(openedInfo.kind!=SubKind::Raw||!openedInfo.rawCompatible||!ook||!waveformCount)message("Music Lab","Requires RAW binary OOK, <=2048 pulses, <=5 seconds. FSK playback is unsupported.");
                    else {
                        bool done=false;resetInput();
                        while(!done){input();if(back||holdBack)break;if(holdOk){uint32_t elapsed;int32_t error;bool sent=labTransmitRaw(waveform,waveformCount,openedInfo.radio,openedInfo.frequencyHz,profile,powerIndex,settings.txLimitMs,elapsed,error);char b[150];snprintf(b,sizeof(b),"%s\nWall %lu us\nCompletion delta %ld us\nEdge jitter unmeasured",sent?"Waveform sent":"TX blocked/aborted",(unsigned long)elapsed,(long)error);message("Music timing",b);done=true;}if(frame()){title("Music Lab / RAW");line(2,"Hold OK: finite TX");line(3,profile==TxProfile::ShieldedLab?"Shielded lab enabled":"TX profile: receive only");line(4,"BACK stops transmission");footer("hold OK play BACK exit");display.display();}}
                    }
                } else message("Music file error",notice);
            } else {if(friendly)friendlyFileActions(selectedFilePath);else fileActions(selectedFilePath,presets);listFiles();if(selected>=pathCount)selected=0;}
        }
        if(frame()) {
            char heading[64];const uint64_t pages=matchingFileCount?(matchingFileCount-1)/FilePage::kEntries+1:1;
            snprintf(heading,sizeof heading,"%s %llu/%llu (%llu)",music?"Музыка":presets?"Пресеты":"Файлы",(unsigned long long)filePage+1,(unsigned long long)pages,(unsigned long long)matchingFileCount);title(heading);ui_text::draw(display,0,13,currentFolder);
            for(size_t row=0;row<3;++row){size_t i=selected+row;if(i>=pathCount)break;display.setCursor(0,25+row*9);display.print(row?"  ":"> ");if(fileHasNext&&i==fileEntryCount)ui_text::print(display,"След. страница");else{display.print(fileEntries[i].directory?"/":"");ui_text::print(display,fileEntries[i].preview);}}
            if(!pathCount)line(4,folderOpenError?"Папка не открылась":"Файлов пока нет");
            if(longPathCount){char hint[48];snprintf(hint,sizeof(hint),"Путь>1023Б:%lu",(unsigned long)longPathCount);footer(hint);}
            else footer(fileHasNext||filePage?"Держ.UP/DN:стр BACK":"UP/DN OK:вход BACK");display.display();
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
const char* chatBlockText(const LabSnapshot& s) {
    if(!strcmp(s.txBlockReason,"Carrier busy"))return "Эфир занят: ждём";
    if(!strcmp(s.txBlockReason,"60s airtime budget"))return "Лимит эфира: ждём";
    if(!strcmp(s.txBlockReason,"100ms packet gap"))return "Пауза между пакетами";
    if(!strcmp(s.txBlockReason,"Ready: manual send"))return "Можно отправлять";
    if(!strncmp(s.txBlockReason,"TX off",6))return "Передача выключена";
    return "Сбой отправки";
}
const char* chatStoppedText(const LabSnapshot& s) {
    if(!strcmp(s.status,"CC1101 not detected"))return "CC1101 не найден";
    if(!strcmp(s.status,"Register readback mismatch"))return "Сбой настроек радио";
    if(!strcmp(s.status,"Radio state timeout"))return "Радио не отвечает";
    if(!strcmp(s.status,"No RAM for worker"))return "Не хватает памяти";
    return "Радио остановлено";
}
void readHistory(){
    size_t selected=0,offset=0;resetInput();
    while(true){input();if(back||holdBack)break;if(up&&selected) {--selected;offset=0;}if(down&&selected+1<historySize){++selected;offset=0;}if(ok&&historySize){const History& h=history[(historyNext+8-1-selected)%8];offset=offset+60<strlen(h.text)?offset+60:0;}
        if(frame()){title("История чата");if(historySize){const History& h=history[(historyNext+8-1-selected)%8];char b[48];if(h.peer)snprintf(b,sizeof(b),"%s %04X (%u/%u)",h.sent?"Кому":"От",h.peer,unsigned(selected+1),unsigned(historySize));else snprintf(b,sizeof(b),"%s (%u/%u)",h.sent?"Отправлено":"Принято",unsigned(selected+1),unsigned(historySize));line(2,b);for(size_t row=0;row<3;++row){size_t pos=offset+row*20;if(pos<strlen(h.text)){snprintf(b,21,"%.20s",h.text+pos);display.setCursor(0,28+row*8);display.print(b);}}}else line(3,"Сообщений пока нет");footer("UP/DN OK:лист BACK");display.display();}}
    resetInput();
}
void chatAdvanced(uint16_t& recipient){
    size_t selected=0;static const char* items[]={"История сообщений","Написать текст","Поиск устройств","Получатель","Статус отправки","Режим: ESP ACK"};resetInput();
    while(true){
        input();uint16_t source;char received[161];while(labReceiveMessage(source,received,sizeof(received)))addHistory(source,false,received);
        const bool plaintext=chatFormat==ChatMode::FlipperPlaintext;
        if(back||holdBack)break;if(up)selected=(selected+5)%6;if(down)selected=(selected+1)%6;
        if(ok){
            if(selected==0)readHistory();
            if(selected==1){char text[161]="";if(textEntry("Текст (ASCII)",text,sizeof(text))){LabSnapshot s=labSnapshot();uint16_t to=plaintext?0:(recipient?recipient:s.peer);if(labQueueText(text,to)){addHistory(to,true,text);showNotice(plaintext?"Queued broadcast; no ACK":"Queued");}else showNotice("Queue full or TX off");}}
            if(selected==2){if(!labDiscover())showNotice("TX off or command queue full");else showNotice(plaintext?"Presence queued; no ACK":"Discovery queued");}
            if(selected==3){if(plaintext)message("Связь с Flipper","Flipper: без шифр.\nЧастота 433.920 МГц.\nСообщение всем.\nПодтверждения нет.");else{LabSnapshot s=labSnapshot();size_t index=0;for(size_t i=0;i<8;++i)if(s.peers[i]==recipient)index=i+1;recipient=s.peers[index%8];}}
            if(selected==4){LabSnapshot s=labSnapshot();char b[256];if(plaintext)snprintf(b,sizeof(b),"Статус %u\n0покой 1очередь\n2передача 3ушло\n4ошибка\nПриём не подтверждён.\nRX%lu TX%lu\nОтброшено %lu\nОбрезано %lu",s.delivery,(unsigned long)s.receivedMessages,(unsigned long)s.sentMessages,(unsigned long)s.plaintextRejected,(unsigned long)s.plaintextTruncated);else snprintf(b,sizeof(b),"Статус %u\n0покой 1очередь\n2передача 3дошло\n4ошибка\nRX%lu TX%lu попыток%lu",s.delivery,(unsigned long)s.receivedMessages,(unsigned long)s.sentMessages,(unsigned long)s.chatRetries);message("Отправка",b);}
            if(selected==5){if(!labStop()){showNotice("Radio still stopping");continue;}chatFormat=plaintext?ChatMode::EspAck:ChatMode::FlipperPlaintext;recipient=0;historySize=historyNext=0;if(!startMode(LabMode::Chat))break;showNotice(chatFormat==ChatMode::FlipperPlaintext?"Flipper plaintext; encryption off":"ESP ACK protocol");}
            resetInput();
        }
        if(frame()){LabSnapshot s=labSnapshot();const bool plain=chatFormat==ChatMode::FlipperPlaintext;title(plain?"Чат Flipper":"Чат / ESP ACK");char b[48];snprintf(b,sizeof(b),"%s %lu.%03lu %ddB",s.active&&s.connected?"RX":"Выкл",(unsigned long)(s.frequencyHz/1000000),(unsigned long)((s.frequencyHz/1000)%1000),s.rssiDbm);ui_text::draw(display,0,13,b);ui_text::draw(display,0,23,selected==5?(plain?"Режим: Flipper":"Режим: ESP ACK"):selected==2&&plain?"Отправить маяк":selected==3&&plain?"О совместимости":items[selected]);snprintf(b,sizeof(b),"P%lu C%lu F%lu M%lu",(unsigned long)s.packets,(unsigned long)s.crcErrors,(unsigned long)s.fifoErrors,(unsigned long)s.receivedMessages);ui_text::draw(display,0,33,b);ui_text::draw(display,0,43,s.txBlockReason);footer("UP/DN OK:вход BACK");display.display();}
    }
    resetInput();
}
__attribute__((noinline)) uint16_t currentChatPeer() { return labSnapshot().peer; }
bool simpleChatSend(uint16_t recipient,const char* text) {
    const uint16_t destination=chatFormat==ChatMode::FlipperPlaintext?0:(recipient?recipient:currentChatPeer());
    if(!labQueueText(text,destination))return false;
    addHistory(destination,true,text);return true;
}
__attribute__((noinline)) void runChatEditorPreview() {
    if(!serialChatEditPending)return;
    serialChatEditPending=false;
    if(!serialChatContext || serialChatCloseRequested)return;
    // Called only after the UART parser frame has returned. This diagnostic
    // draft is always discarded; accepting the editor cannot request RF TX.
    char draft[161]="";
    const bool accepted=textEntry("Текст (ASCII)",draft,sizeof(draft));
    Serial.printf("[Chat] editor CLOSED accepted=%u length=%u; no text sent\n",accepted,unsigned(strlen(draft)));
    resetInput();
}
int chatSendMenu(uint16_t& recipient) {
    static const char* choices[]={"Написать текст","Привет (Hello)","Тест TEST123","Ответ OK","Дополнительно"};
    static const char* wireText[]={"","Hello","TEST123","OK",""};
    size_t selected=2;resetInput();
    while(true) {
        input();if(back||holdBack)break;
        if(up)selected=(selected+4)%5;if(down)selected=(selected+1)%5;
        if(ok) {
            if(selected==4){chatAdvanced(recipient);break;}
            char text[161]="";
            if(selected==0){if(!textEntry("Текст (ASCII)",text,sizeof(text))){resetInput();continue;}}
            else snprintf(text,sizeof(text),"%s",wireText[selected]);
            if(text[0]) {
                const bool queued=simpleChatSend(recipient,text);
                showNotice(queued?"Message queued":"Message not queued: radio busy/off");
                resetInput();return queued?1:-1;
            }
        }
        if(frame()) {
            title("Отправить");
            for(size_t row=0;row<4;++row){display.setCursor(0,15+row*9);display.print(row?"  ":"> ");ui_text::print(display,choices[(selected+row)%5]);}
            footer("OK:послать BACK:назад");display.display();
        }
    }
    resetInput();return false;
}
void chatApp(){
    const RadioSettings savedSettings=settings;const RegisterImage savedRadio=radio;
    const TxProfile savedProfile=profile;const uint8_t savedPower=powerIndex;const ChatMode savedMode=chatFormat;
    RegisterImage automatic;
    RadioSettings automaticSettings;
    if(!makeFlipperChatProfile(automatic,automaticSettings)){message("Чат недоступен","Ошибка профиля радио.");return;}
    settings=automaticSettings;radio=automatic;profile=TxProfile::ShieldedLab;powerIndex=1;chatFormat=ChatMode::FlipperPlaintext;
    const bool started=startMode(LabMode::Chat);
    if(started) {
        serialChatContext=true;serialChatCloseRequested=false;serialChatEditPending=false;historySize=historyNext=0;
        uint16_t recipient=0;size_t selected=0,offset=0;bool queueError=false;resetInput();
        while(true) {
            input();uint16_t source;char received[161];
            while(labReceiveMessage(source,received,sizeof(received))){addHistory(source,false,received);selected=offset=0;queueError=false;}
            if(back||holdBack)break;
            if(serialChatEditPending) {
                runChatEditorPreview();continue;
            }
            if(up&&historySize){if(offset)offset=0;else if(selected+1<historySize){++selected;offset=0;}}
            if(down&&historySize) {
                const History& h=history[(historyNext+8-1-selected)%8];
                if(offset+100<strlen(h.text))offset+=100;
                else if(selected){--selected;offset=0;}
            }
            if(ok){const int result=chatSendMenu(recipient);if(result<0)queueError=true;else if(result>0){selected=offset=0;queueError=false;}if(selected>=historySize)selected=offset=0;resetInput();}
            if(frame()) {
                const LabSnapshot s=labSnapshot();
                const char* phase=!s.active?"Стоп":!s.connected?"РФсбой":queueError?"Занят":s.delivery==ChatQueued||s.delivery==ChatSending?"Отпр.":s.delivery==ChatFailed?"Ошибка":s.delivery==ChatDelivered?"Послано":"Готов";
                title("ЧАТ");ui_text::draw(display,82,2,phase,SH110X_BLACK,7);display.setTextColor(SH110X_WHITE);
                if(historySize) {
                    const History& h=history[(historyNext+8-1-selected)%8];
                    for(size_t row=0;row<5;++row){const size_t position=offset+row*20;if(position>=strlen(h.text))break;char text[21];snprintf(text,sizeof(text),"%.20s",h.text+position);display.setCursor(0,13+row*8);display.print(text);}
                    char hint[48];snprintf(hint,sizeof(hint),"%s %u/%u OK: текст",h.sent?"Вы":"Flipper",unsigned(selected+1),unsigned(historySize));ui_text::draw(display,0,56,hint);
                    display.drawFastHLine(0,53,128,SH110X_WHITE);
                } else {
                    ui_text::draw(display,0,15,"433.920 МГц");ui_text::draw(display,0,27,"Flipper: без шифр.");ui_text::draw(display,0,39,"OK: отправка TEST123");footer("UP/DN лист BACK выход");
                }
                if((s.delivery==ChatQueued||s.delivery==ChatSending)&&strcmp(s.txBlockReason,"Ready: manual send")&&strcmp(s.txBlockReason,"100ms packet gap")) {
                    display.fillRect(0,43,128,10,SH110X_BLACK);ui_text::draw(display,0,44,chatBlockText(s));
                } else if(queueError||!s.active||!s.connected||s.delivery==ChatFailed) {
                    display.fillRect(0,43,128,10,SH110X_BLACK);
                    ui_text::draw(display,0,44,queueError?"Не отправлено: занят":!s.active?chatStoppedText(s):!s.connected?"CC1101 не найден":strcmp(s.txBlockReason,"Ready: manual send")?chatBlockText(s):"Сбой отправки");
                }
                display.display();
            }
        }
        while(!labStop()){showNotice("Waiting for chat radio stop");delay(10);}
        serialChatContext=false;serialChatCloseRequested=false;serialChatEditPending=false;resetInput();
    }
    settings=savedSettings;radio=savedRadio;profile=savedProfile;powerIndex=savedPower;chatFormat=savedMode;
}

void simpleRadioSettings() {
    static const char* presets[]={"AM/OOK 270 кГц","AM/OOK 650 кГц","FM 2.38 кГц","FM 47.6 кГц","FM 12.7 кГц"};
    size_t selected=0,preset=0;
    for(size_t i=0;i<presetCount();++i)if(!strcmp(activePreset,presetAt(i)->name))preset=i;
    resetInput();
    while(true) {
        input();if(back||holdBack)break;if(up||down)selected=1-selected;
        if(ok) {
            if(selected==0)settings.frequencyHz=settings.frequencyHz==433920000?868350000:433920000;
            else {preset=(preset+1)%presetCount();if(!selectPreset(preset))message("Настройки","Ошибка профиля радио.");}
        }
        if(frame()) {
            title("Настройки приёма");
            ui_text::draw(display,0,14,selected==0?"> Частота":"  Частота");
            char frequencyText[40];snprintf(frequencyText,sizeof(frequencyText),"%lu.%03lu МГц",(unsigned long)(settings.frequencyHz/1000000),(unsigned long)((settings.frequencyHz/1000)%1000));
            ui_text::draw(display,12,23,frequencyText);
            ui_text::draw(display,0,34,selected==1?"> Сигнал":"  Сигнал");
            ui_text::draw(display,12,43,preset<5?presets[preset]:activePreset,SH110X_WHITE,19);
            footer("OK:смена BACK:назад");display.display();
        }
    }
    resetInput();
}

void radioLabAdvanced() {
    static const char* apps[]={"Приём протокола LAB","Запись RAW","Поиск частоты","Анализ спектра","Анализ RAW","Анализ протокола","Файлы сигналов","Генератор сигналов","Все настройки радио","Поиск модуляции","Чат с Flipper","Музыкальная лаб.","Диагностика CC1101","О программе","Прежнее меню"};
    const size_t count=sizeof(apps)/sizeof(apps[0]);size_t selected=0;resetInput();
    while(true) {
        input();if(back||holdBack)break;if(up)selected=(selected+count-1)%count;if(down)selected=(selected+1)%count;
        if(ok){switch(selected){case 0:captureApp(true);break;case 1:captureApp(false);break;case 2:monitorApp(false);break;case 3:monitorApp(true);break;case 4:analyzerApp(false);break;case 5:analyzerApp(true);break;case 6:browser();break;case 7:generatorApp();break;case 8:settingsApp();break;case 9:explorerApp();break;case 10:chatApp();break;case 11:browser(true);break;case 12:diagnosticsApp();break;case 13:message("О радиолаборатории","ESP32 + CC1101\nRAW: один сигнал.\nCC1101 не SDR.\nПоддержка протоколов\nограничена.");break;case 14:while(!labStop()){showNotice("Waiting for radio worker to stop");delay(10);}runSubGHz();suspendLegacyReceive();break;}resetInput();}
        if(frame()){title("Дополнительно");for(size_t row=0;row<4;++row){const size_t i=(selected+row)%count;display.setCursor(0,17+row*9);display.print(row?"  ":"> ");ui_text::print(display,apps[i]);}footer("UP/DN выбор OK:вход");display.display();}
    }
    resetInput();
}
}

void runSubGhzChat() {
    suspendLegacyReceive();
    while(!research::labStop()) { showNotice("Waiting for radio worker to stop"); delay(10); }
    resetInput();
    chatApp();
    // A completed worker must release SPI ownership before legacy RX resumes.
    while(!research::labStop()) { showNotice("Waiting for chat radio stop"); delay(10); }
    restoreReceiveMode();
    resetInput();
}

void runRadioLab() {
    using namespace research;
    if(radioLabHomeOpen){message("Радио","Для возврата нажмите\nBACK в прежнем меню.");return;}
    radioLabHomeOpen=true;
    suspendLegacyReceive();
    settings=RadioSettings();settings.rxTimeoutMs=30000;selectPreset(0);profile=TxProfile::ReceiveOnly;powerIndex=0;chatFormat=ChatMode::FlipperPlaintext;
    SD.mkdir("/subghz");SD.mkdir("/subghz/presets");
    static const char* apps[]={"Принять сигнал","Файлы .sub","Чат Flipper","Настройки","Дополнительно"};
    const size_t count=sizeof(apps)/sizeof(apps[0]);size_t selected=0;resetInput();
    while(true){
        input();if(back||holdBack)break;if(up)selected=(selected+count-1)%count;if(down)selected=(selected+1)%count;
        if(ok){switch(selected){case 0:if(settings.packetFormat!=PacketFormat::Async)selectPreset(0);captureApp(false);break;case 1:browser(false,false,true);break;case 2:chatApp();break;case 3:simpleRadioSettings();break;case 4:radioLabAdvanced();break;}resetInput();}
        if(frame()){char heading[40];snprintf(heading,sizeof(heading),"Радио %lu.%03lu",(unsigned long)(settings.frequencyHz/1000000),(unsigned long)((settings.frequencyHz/1000)%1000));title(heading);for(size_t row=0;row<4;++row){size_t i=(selected+row)%count;display.setCursor(0,17+row*9);display.print(row?"  ":"> ");ui_text::print(display,apps[i]);}footer("UP/DN выбор OK:вход");display.display();}
    }
    // Ownership cannot pass back to legacy code until the worker has stopped.
    while(!labStop()) { showNotice("Waiting for radio worker to stop"); delay(10); }
    radioLabHomeOpen=false;restoreReceiveMode();resetInput();
}

bool radioLabPermitTx(uint32_t hz,uint32_t durationMs) {
    return research::permitLabTx(profile,hz,durationMs,settings.txLimitMs);
}

void runRadioLabFile(const char* path) {
    using namespace research;
    suspendLegacyReceive();
    friendlyFileActions(path);
    while(!labStop()){showNotice("Waiting for radio worker to stop");delay(10);}
    restoreReceiveMode();resetInput();
}
void runRadioLabFiles() {
    suspendLegacyReceive();
    while(!research::labStop()){showNotice("Waiting for radio worker to stop");delay(10);}
    browser(false,false,true);
    while(!research::labStop()){showNotice("Waiting for radio worker to stop");delay(10);}
    restoreReceiveMode();resetInput();
}

extern bool setupCC1101();
void radioLabBootDiagnostics() {
    const bool calibrationLoaded=research::loadRadioCalibration();
    Serial.printf("[RFCal] loaded=%u ppm=%ld\n",calibrationLoaded,(long)research::radioCalibrationPpm());
    Serial.printf("[RadioLab] firmware=%s esp_heap=%u crystal_cc1101_assumed=26000000\n",FIRMWARE,ESP.getFreeHeap());
    const bool ready=setupCC1101();
    Serial.printf("[RadioLab] probe=%s part=%02X version=%02X marc=%02X IOCFG0=%02X PKTCTRL0=%02X MDMCFG4=%02X MDMCFG2=%02X readyTO=%lu\n",
        ready?"PASS":"FAIL",ELECHOUSE_cc1101.SpiReadStatus(0x30),ELECHOUSE_cc1101.SpiReadStatus(0x31),
        ELECHOUSE_cc1101.SpiReadStatus(0x35)&0x1F,ELECHOUSE_cc1101.SpiReadReg(2),ELECHOUSE_cc1101.SpiReadReg(8),
        ELECHOUSE_cc1101.SpiReadReg(0x10),ELECHOUSE_cc1101.SpiReadReg(0x12),(unsigned long)ELECHOUSE_cc1101.spiTimeouts());
    ELECHOUSE_cc1101.SpiStrobe(0x36);pinMode(CC1101_GDO0,INPUT);
    Serial.println("[RadioLab] receive-only boot probe complete; RF waveform validation pending");
}
namespace {
// Keep diagnostic frames out of the UART poll used inside the chat UI.
// In particular SubInfo/RegisterImage and full LabSnapshot copies must not be
// reserved by radioLabSerialPoll() while it calls the send/queue path.
__attribute__((noinline)) bool serialRadioWorkerActive() { return research::labSnapshot().active; }
__attribute__((noinline)) bool serialChatMainIdle() {
    return inMenu && !serialChatContext && !serialChatListen && !serialRadioWorkerActive();
}
__attribute__((noinline)) bool serialChatCanSend() {
    if(!serialChatContext || serialChatCloseRequested)return false;
    const research::LabSnapshot state=research::labSnapshot();
    return state.active && state.connected && state.mode==research::LabMode::Chat && !state.chatDiagnostic;
}
__attribute__((noinline)) void printSerialChatQueueFailure() {
    const research::LabSnapshot state=research::labSnapshot();
    Serial.printf("[Chat] ERROR not queued: %s\n",state.txBlockReason);
}
__attribute__((noinline)) bool pollSerialChatListen() {
    const research::LabSnapshot s=research::labSnapshot();
    if(s.chatDiagnostic&&s.active&&s.samples&&!serialChatReported){printChatStatus();serialChatReported=true;}
    if(s.chatDiagnostic) {
        uint16_t source;char text[161];
        for(unsigned i=0;i<8&&research::labReceiveMessage(source,text,sizeof(text));++i)Serial.printf("[Chat] RX text=%s\n",text);
    }
    if(!s.active||!s.chatDiagnostic) {
        if(s.chatDiagnostic&&!s.active) {
            if(!research::labStop())return false; // Keep ownership until the worker confirms completion.
            restoreReceiveMode();
        }
        Serial.println("[Chat] DONE receive-only diagnostic");printChatStatus();serialChatListen=false;
    }
    return true;
}
__attribute__((noinline)) void printSerialScreen() {
    const uint8_t* pixels=display.getBuffer();
    if(!pixels)Serial.println("[Screen] ERROR no framebuffer");
    else {
        static const char hex[]="0123456789ABCDEF";
        Serial.println("SCREEN12864");
        for(size_t offset=0;offset<1024;offset+=64) {
            char encoded[129];
            for(size_t i=0;i<64;++i){encoded[i*2]=hex[pixels[offset+i]>>4];encoded[i*2+1]=hex[pixels[offset+i]&15];}
            encoded[128]=0;Serial.println(encoded);
        }
        Serial.println("ENDSCREEN");
    }
}
__attribute__((noinline)) bool serialChatListenFrequency(const char* command,uint32_t& frequencyHz) {
    uint32_t frequency=0;
    if(!strcmp(command,"chatlisten plain433"))frequency=433920000;
    else if(!strcmp(command,"chatlisten plain868"))frequency=868350000;
    else if(!strncmp(command,"chatlisten hz ",14)) {
        const char* cursor=command+14;
        if(!*cursor)return false;
        for(;*cursor;++cursor) {
            if(*cursor<'0'||*cursor>'9')return false;
            const uint32_t digit=uint32_t(*cursor-'0');
            if(frequency>(UINT32_MAX-digit)/10)return false;
            frequency=frequency*10+digit;
        }
    } else return false;
    if(!research::supportedFrequency(frequency))return false;
    frequencyHz=frequency;return true;
}
__attribute__((noinline)) void startSerialChatListen(const char* command) {
    uint32_t frequencyHz=0;
    if(!serialChatMainIdle())Serial.println("[Chat] ERROR busy; chatlisten requires the idle main menu");
    else if(!serialChatListenFrequency(command,frequencyHz))Serial.println("[Chat] ERROR use chatlisten hz <supported decimal Hz> or plain433/plain868");
    else {
        research::RegisterImage image;research::RadioSettings listen;
        research::loadPreset("AM270",image);listen.frequencyHz=frequencyHz;
        listen.rxTimeoutMs=30000;suspendLegacyReceive();
        serialChatListen=research::labStart(research::LabMode::Chat,image,listen,research::TxProfile::ReceiveOnly,0,25000,research::ChatMode::FlipperPlaintext,true);
        serialChatReported=false;
        Serial.printf("[Chat] %s plain receive-only freq=%lu limit=30000ms manualCRC=1\n",serialChatListen?"START":"FAIL",(unsigned long)listen.frequencyHz);
        if(!serialChatListen)restoreReceiveMode();
    }
}
__attribute__((noinline)) bool handleIdleSerialCommand(const char* command) {
    if(!strcmp(command,"rfcal"))Serial.printf("[RFCal] ppm=%ld\n",(long)research::radioCalibrationPpm());
    else if(!strncmp(command,"rfcal ppm ",10)) {
        int32_t ppm=0;
        if(!serialChatMainIdle())Serial.println("[RFCal] ERROR requires the idle main menu");
        else if(!research::parseRadioTrimPpm(command+10,ppm))Serial.println("[RFCal] ERROR signed decimal ppm must be -250..250");
        else if(!research::saveRadioCalibration(ppm))Serial.println("[RFCal] ERROR calibration not saved");
        else Serial.printf("[RFCal] SAVED ppm=%ld; nominal channels unchanged\n",(long)research::radioCalibrationPpm());
    }
    else if(!strncmp(command,"sublist ",8)) {
        const char* path=command+8;
        if(!inMenu || (strcmp(path,"/subghz")&&!safePath(path)))Serial.println("[SDList] ERROR main menu and /subghz path required");
        else {
            File folder=SD.open(path,FILE_READ);
            if(!folder || !folder.isDirectory())Serial.println("[SDList] ERROR directory unavailable");
            else {
                unsigned count=0;File entry=folder.openNextFile();
                while(entry && count<512) {
                    Serial.printf("[SDList] %s %s\n",entry.isDirectory()?"DIR":"FILE",entry.name());
                    entry.close();++count;if(!(count%16))delay(1);entry=folder.openNextFile();
                }
                Serial.printf("[SDList] count=%u more=%u\n",count,bool(entry));entry.close();
            }
            folder.close();
        }
    }
    else if(!strncmp(command,"subinspect ",11)) {
        const char* path=command+11;
        if(!inMenu||!safePath(path))Serial.println("[SUB] ERROR main menu and /subghz/ path required");
        else if(inspectFile(path)) {
            research::StaticSubWaveform generated={};const char* unsupported=prepareOpenedFileSend(generated);
            Serial.printf("[SUB] path=%s kind=%u protocol=%s frequency=%lu bits=%u pulses=%lu durationUs=%llu eligible=%u\n",path,unsigned(openedInfo.kind),openedInfo.protocol,(unsigned long)openedInfo.frequencyHz,openedInfo.bits,(unsigned long)openedInfo.raw.pulses,(unsigned long long)openedInfo.raw.durationUs,unsupported==nullptr);
            if(unsupported)Serial.printf("[SUB] reason=%s\n",unsupported);
        } else Serial.printf("[SUB] ERROR %s\n",notice);
    }
    else if(!strcmp(command,"rflisten433")) {
        if(!inMenu)Serial.println("[RFListen] busy: return to the main menu");
        else {
            research::RegisterImage image;research::RadioSettings listening;
            const bool prepared=research::loadPreset("AM270",image)&&research::describeRadio(image,433920000,listening);
            listening.rxTimeoutMs=2500;suspendLegacyReceive();
            const bool started=prepared&&research::labStart(research::LabMode::Monitor,image,listening,research::TxProfile::ReceiveOnly,0);
            Serial.printf("[RFListen] %s OOK433 RX-only 2500ms\n",started?"START":"FAIL");
            const uint32_t begin=millis();
            while(started&&millis()-begin<2600&&research::labSnapshot().active)delay(10);
            const research::LabSnapshot observed=research::labSnapshot();
            while(!research::labStop())delay(10);
            restoreReceiveMode();
            Serial.printf("[RFListen] peak=%d rssi=%d samples=%lu spi=%lu miso=%lu\n",observed.peakDbm,observed.rssiDbm,(unsigned long)observed.samples,(unsigned long)observed.spiErrors,(unsigned long)observed.misoTimeouts);
        }
    }
    else if(!strcmp(command,"rftest princeton433")) {
        if(!inMenu)Serial.println("[RFTest] busy: return to the main menu");
        else {
            research::SubInfo known;known.kind=research::SubKind::Key;known.frequencyHz=433920000;
            known.bits=24;known.teUs=390;known.keyBytes=8;known.key[5]=0xA5;known.key[6]=0x3A;known.key[7]=0x53;
            snprintf(known.protocol,sizeof known.protocol,"Princeton");research::loadPreset("AM270",known.radio);
            research::StaticSubWaveform generated={};waveformCount=0;
            const auto encoded=research::encodeStaticSub(known,waveform,research::kWaveformCapacity,generated);
            uint32_t elapsed=0;int32_t timing=0;bool sent=false;suspendLegacyReceive();
            if(encoded==research::StaticSubError::None)sent=research::labTransmitRaw(waveform,generated.count,known.radio,known.frequencyHz,research::TxProfile::ShieldedLab,1,5000,elapsed,timing);
            restoreReceiveMode();
            Serial.printf("[RFTest] %s Princeton A53A53 freq=433920000 repeats=6 nominal=0dBm intervals=%u expectedUs=%lu elapsedUs=%lu deltaUs=%ld\n",sent?"PASS":"FAIL",unsigned(generated.count),(unsigned long)generated.durationUs,(unsigned long)elapsed,(long)timing);
        }
    }
    else if(!strcmp(command,"rfclock")) {
        if(!inMenu)Serial.println("[RFClock] busy: return to the main menu");
        else {
            suspendLegacyReceive();research::RfClockProbeResult measured;
            const bool good=research::radioLabClockProbe(measured);
            restoreReceiveMode();
            Serial.printf("[RFClock] %s medianHz=%lu assumedHz=26000000 restored=%u error=%s\n",good?"PASS":"FAIL",(unsigned long)measured.medianHz,measured.restored,research::rfClockProbeErrorText(measured.error));
            for(unsigned i=0;i<3;++i)Serial.printf("[RFClock] sample=%u edges=%lu elapsedUs=%lu hz=%lu\n",i,(unsigned long)measured.samples[i].edges,(unsigned long)measured.samples[i].elapsedUs,(unsigned long)measured.samples[i].oscillatorHz);
        }
    }
    else if(handleAppInstallCommand(command)){}
    else if(!strcmp(command,"rfdiag"))radioLabBootDiagnostics();
    else if(!strcmp(command,"appsdiag"))sdAppsSelfTest();
    else return false;
    return true;
}
}

void radioLabSerialPoll() {
    if(serialChatListen && !pollSerialChatListen())return;
    static char command[160];static uint8_t used;static bool overflow;
    for(unsigned drained=0;drained<256&&Serial.available();++drained) {
        const char c=Serial.read();
        if(c=='\r')continue;
        if(c=='\n') {
            command[used]=0;
            if(overflow)Serial.println("[Chat] ERROR command too long");
            else if(!strcmp(command,"chatopen")) {
                if(serialChatOpenPending || !serialChatMainIdle())
                    Serial.println("[Chat] ERROR busy; chatopen requires the idle main menu");
                else {
                    // Opening a UI from this frame keeps the UART stack alive
                    // when chat input polls UART again. Dispatch only after
                    // returning to loop(), with no old parser frame retained.
                    serialChatOpenPending=true;
                    used=0;overflow=false;command[0]=0;
                    Serial.println("[Chat] open request queued");
                    return;
                }
            }
            else if(!strcmp(command,"chatsend TEST123")) {
                if(!serialChatCanSend())Serial.println("[Chat] ERROR chatsend requires an active chat UI");
                else if(simpleChatSend(0,"TEST123"))Serial.println("[Chat] queued TEST123; RF completion pending");
                else printSerialChatQueueFailure();
            }
            else if(!strcmp(command,"chatedit")) {
                if(serialChatEditPending || serialChatEditing || !serialChatCanSend())
                    Serial.println("[Chat] ERROR chatedit requires an active chat UI without an editor");
                else {serialChatEditPending=true;Serial.println("[Chat] editor request queued; no text send");}
            }
            else if(!strcmp(command,"chatclose")) {
                if(!serialChatContext)Serial.println("[Chat] ERROR no chat UI is open");
                else {serialChatCloseRequested=true;research::labRequestStop();Serial.println("[Chat] close requested");}
            }
            else if(!strcmp(command,"chatstat"))printChatStatus();
            else if(!strcmp(command,"screen"))printSerialScreen();
            else if(!strcmp(command,"chatstop")) {research::labRequestStop();Serial.println("[Chat] stop requested");}
            else if(!strncmp(command,"chatlisten ",11))startSerialChatListen(command);
            else if((serialChatContext||serialRadioWorkerActive())&&used)Serial.println("[Chat] busy: use chatstat, screen, chatedit, chatsend TEST123, chatclose or chatstop");
            else if(handleIdleSerialCommand(command)){}
            else if(used)Serial.println("[RadioLab] commands: chatopen, chatedit, chatsend TEST123, chatclose, chatstat, chatlisten plain433/plain868 or hz <decimal>, chatstop, screen, rfdiag, rfcal, rfcal ppm <signed decimal>, rfclock, rflisten433, rftest princeton433, sublist /subghz, subinspect /subghz/file.sub, appsdiag, APP_PUT");
            used=0;overflow=false;
        } else if(!overflow&&c>=32&&c<=126&&size_t(used)+1<sizeof(command))command[used++]=c;
        else overflow=true;
    }
}
__attribute__((noinline)) void radioLabHandlePending() {
    if(!serialChatOpenPending)return;
    serialChatOpenPending=false; // Consume once, including a failed idle recheck.
    if(!serialChatMainIdle()) {
        Serial.println("[Chat] ERROR busy; deferred chatopen requires the idle main menu");return;
    }
    Serial.println("[Chat] OPEN simple chat; no automatic text");
    inMenu=false;runSubGhzChat();returnToMainMenu();
    Serial.println("[Chat] CLOSED; main menu restored");
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
