// The runner injects actual production controller function bodies at the marker.
// Arduino UART/UI/worker are doubles; no serial port or radio is accessed.
#include "lab_service.h"
#include "keyboard_model.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdarg>
#include <string>
#include <vector>
using namespace research;
static unsigned checks;
#define CHECK(x) do{++checks;if(!(x)){std::fprintf(stderr,"FAIL line%d: %s\n",__LINE__,#x);std::exit(1);}}while(0)
bool inMenu=true,serialChatContext=false,serialChatListen=false,serialChatReported=false;
bool serialChatCloseRequested=false,serialChatOpenPending=false;
bool serialChatEditPending=false,serialChatEditing=false;
bool serialCaptureContext=false,serialCaptureOpenPending=false,serialCaptureSavePending=false,serialCaptureCloseRequested=false;
char captureSavedPath[112]="";
ChatMode chatFormat=ChatMode::FlipperPlaintext;
static LabSnapshot observed={};
static unsigned pollDepth,maxDepth,uiCalls,sends,stops,restores,starts,history,diagnostics,stats,screens;
static uint32_t startFrequency,startTimeout;static uint16_t sentDestination;
static bool failStart;
static unsigned editorCalls;static bool editorAccept=true;
static unsigned captureCalls,captureStats;
bool up,down,ok,back,holdOk,holdBack,holdUp,holdDown;
enum { EUp=1,EDown=2,EOk=4,EBack=8,EHoldOk=16,EHoldBack=32,EHoldUp=64,EHoldDown=128 };
static std::vector<unsigned> editorEvents;
static std::vector<std::string> drawnText;
static size_t eventAt;static unsigned uiFrames,uiClears;static uint32_t uiTime;
static const uint16_t SH110X_WHITE=1,SH110X_BLACK=0;
uint32_t millis(){return uiTime;}
void input(){
    CHECK(serialChatEditing);CHECK(eventAt<editorEvents.size());const unsigned e=editorEvents[eventAt++];uiTime+=7;
    up=e&EUp;down=e&EDown;ok=e&EOk;back=e&EBack;
    holdOk=e&EHoldOk;holdBack=e&EHoldBack;holdUp=e&EHoldUp;holdDown=e&EHoldDown;
}
bool frame(){++uiFrames;return true;}
struct KeyboardDisplay {
    void clearDisplay(){++uiClears;}
    void fillRect(int x,int y,int w,int h,uint16_t){CHECK(x>=0&&y>=0&&w>0&&h>0&&x+w<=128&&y+h<=64);}
    void drawFastHLine(int x,int y,int w,uint16_t){CHECK(x>=0&&y>=0&&w>0&&x+w<=128&&y<64);}
    void display(){}
} display;
namespace ui_text {
void draw(KeyboardDisplay&,int x,int y,const char* text,uint16_t=1,size_t=21,uint8_t=1){
    CHECK(x>=0&&x<128&&y>=0&&y<64);drawnText.push_back(text);
}
}
void footer(const char* text){
    size_t glyphs=0;for(const unsigned char* p=reinterpret_cast<const unsigned char*>(text);*p;++p)if((*p&0xC0)!=0x80)++glyphs;
    CHECK(glyphs<=21);drawnText.push_back(text);
}
static bool drawn(const char* text){for(const std::string& s:drawnText)if(s==text)return true;return false;}
static void editorScript(std::initializer_list<unsigned> events){editorEvents=events;eventAt=0;drawnText.clear();uiFrames=uiClears=0;}
struct MockSerial {
    std::string input,output;size_t offset=0;
    size_t available()const{return input.size()-offset;}
    int read(){return uint8_t(input[offset++]);}
    void println(const char* value){output+=value;output+='\n';}
    void printf(const char* format,...){char text[512];va_list args;va_start(args,format);vsnprintf(text,sizeof text,format,args);va_end(args);output+=text;}
    void feed(const std::string& value){CHECK(!available());input=value;offset=0;}
} Serial;
namespace research {
LabSnapshot labSnapshot(){return observed;}
void labRequestStop(){++stops;observed.active=false;}
bool labQueueText(const char* text,uint16_t destination){CHECK(std::strcmp(text,"TEST123")==0);++sends;sentDestination=destination;return true;}
bool labStart(LabMode mode,const RegisterImage&,const RadioSettings& settings,TxProfile profile,uint8_t power,uint32_t step,ChatMode format,bool diagnostic,bool autoThreshold){
    CHECK(mode==LabMode::Chat&&profile==TxProfile::ReceiveOnly&&power==0&&step==25000);
    CHECK(format==ChatMode::FlipperPlaintext&&diagnostic);CHECK(settings.rxTimeoutMs==30000);
    CHECK(!autoThreshold);
    ++starts;startFrequency=settings.frequencyHz;startTimeout=settings.rxTimeoutMs;
    observed.active=!failStart;observed.chatDiagnostic=!failStart;return !failStart;
}
}
void suspendLegacyReceive(){}
void restoreReceiveMode(){++restores;}
void addHistory(uint16_t,bool sent,const char*){CHECK(sent);++history;}
void printChatStatus(){++stats;}
void printCaptureStatus(){++captureStats;}
void printSerialScreen(){++screens;}
void printSerialChatQueueFailure(){}
bool pollSerialChatListen(){return true;}
bool handleIdleSerialCommand(const char*){++diagnostics;return true;}
void radioLabSerialPoll();void radioLabHandlePending();
static void runPoll(){++pollDepth;if(pollDepth>maxDepth)maxDepth=pollDepth;radioLabSerialPoll();--pollDepth;}
void resetInput(){}
void runSerialCaptureUi(){
    CHECK(pollDepth==0&&!inMenu);++captureCalls;
    serialCaptureContext=true;serialCaptureCloseRequested=serialCaptureSavePending=false;
    observed.active=true;observed.mode=LabMode::Raw;
    runPoll();CHECK(serialCaptureCloseRequested&&!observed.active&&!serialCaptureSavePending);
    serialCaptureContext=false;serialCaptureCloseRequested=false;
}
bool textEntry(const char*,char* draft,size_t capacity){
    CHECK(pollDepth==0);CHECK(!serialChatEditing);CHECK(capacity==161&&draft[0]==0);++editorCalls;
    serialChatEditing=true;
    if(Serial.available())runPoll();
    serialChatEditing=false;std::strcpy(draft,"preview");
    return editorAccept&&!serialChatCloseRequested;
}
void runSubGhzChat(){
    CHECK(pollDepth==0);CHECK(!inMenu);++uiCalls;
    serialChatContext=true;serialChatCloseRequested=false;
    observed.active=observed.connected=true;observed.mode=LabMode::Chat;observed.chatDiagnostic=false;
    runPoll();CHECK(serialChatCloseRequested);CHECK(!observed.active);
    serialChatContext=false;
}
void returnToMainMenu(){++restores;inMenu=true;}
// PRODUCTION_FUNCTIONS
int main(){
    uint32_t hz=99;
    CHECK(serialChatListenFrequency("chatlisten plain433",hz)&&hz==433920000);
    CHECK(serialChatListenFrequency("chatlisten plain868",hz)&&hz==868350000);
    const uint32_t valid[]={300000000,348000000,387000000,433770000,433920000,434070000,464000000,779000000,928000000};
    for(size_t i=0;i<sizeof valid/sizeof *valid;++i){const std::string command="chatlisten hz "+std::to_string(valid[i]);CHECK(serialChatListenFrequency(command.c_str(),hz)&&hz==valid[i]);}
    const char* invalid[]={"chatlisten hz ","chatlisten hz 0","chatlisten hz -1","chatlisten hz +433920000","chatlisten hz 433920000 ","chatlisten hz 433920000x","chatlisten hz 4294967296","chatlisten hz 99999999999999999999","chatlisten hz 299999999","chatlisten hz 348000001","chatlisten hz 464000001","chatlisten hz 928000001","chatlisten hz 500000000","chatlisten unknown"};
    for(size_t i=0;i<sizeof invalid/sizeof *invalid;++i){hz=99;CHECK(!serialChatListenFrequency(invalid[i],hz));CHECK(hz==99);}
    for(unsigned cycle=0;cycle<100;++cycle){
        Serial.feed("chatopen\nchatsend TEST123\nchatstat\nchatclose\n");const unsigned before=uiCalls;
        runPoll();CHECK(uiCalls==before&&serialChatOpenPending);CHECK(Serial.available()>0);
        radioLabHandlePending();CHECK(uiCalls==before+1);CHECK(!serialChatOpenPending&&inMenu);CHECK(!Serial.available());
        radioLabHandlePending();CHECK(uiCalls==before+1);CHECK(maxDepth==1);
    }
    CHECK(sends==100&&history==100&&stats==100);CHECK(sentDestination==0);
    Serial.feed("chatopen\n");runPoll();CHECK(serialChatOpenPending);observed.active=true;
    const unsigned before=uiCalls;radioLabHandlePending();CHECK(uiCalls==before&&!serialChatOpenPending);observed.active=false;radioLabHandlePending();CHECK(uiCalls==before);
    inMenu=false;Serial.feed("chatopen\n");runPoll();CHECK(!serialChatOpenPending);inMenu=true;
    Serial.feed(std::string(300,'x')+"\nchatopen\nchatsend TEST123\nchatclose\n");runPoll();CHECK(!serialChatOpenPending);runPoll();CHECK(serialChatOpenPending);radioLabHandlePending();CHECK(uiCalls==before+1);
    observed={};Serial.feed("chatlisten hz 433770000\n");runPoll();CHECK(starts==1&&serialChatListen&&startFrequency==433770000&&startTimeout==30000);CHECK(sends==101);
    Serial.feed("chatlisten hz 434070000\n");runPoll();CHECK(starts==1);observed={};serialChatListen=false;
    inMenu=false;Serial.feed("chatlisten hz 434070000\n");runPoll();CHECK(starts==1);inMenu=true;
    Serial.feed("chatlisten hz 4294967296\n");runPoll();CHECK(starts==1);
    Serial.feed("chatlisten plain868\n");runPoll();CHECK(starts==2&&startFrequency==868350000);observed={};serialChatListen=false;
    failStart=true;const unsigned restored=restores;Serial.feed("chatlisten plain433\n");runPoll();CHECK(starts==3&&!serialChatListen&&restores==restored+1);failStart=false;
    const unsigned diagnosticsBefore=diagnostics;serialChatContext=true;observed.active=true;
    Serial.feed("rfclock\nscreen\nchatsend TEST123\n");runPoll();CHECK(diagnostics==diagnosticsBefore&&screens==1&&sends==101);
    serialChatContext=false;observed={};chatFormat=ChatMode::EspAck;observed.peer=0x1234;
    CHECK(simpleChatSend(0,"TEST123"));CHECK(sentDestination==0x1234);CHECK(simpleChatSend(0x4321,"TEST123"));CHECK(sentDestination==0x4321);
    const unsigned sendsBeforeEditor=sends,editorsBefore=editorCalls;
    Serial.feed("chatedit\n");runPoll();CHECK(!serialChatEditPending&&editorCalls==editorsBefore);
    serialChatContext=true;observed.active=observed.connected=true;observed.mode=LabMode::Chat;
    serialChatCloseRequested=false;
    Serial.feed("chatedit\nchatedit\n");runPoll();CHECK(serialChatEditPending);CHECK(editorCalls==editorsBefore);
    runChatEditorPreview();CHECK(!serialChatEditPending&&editorCalls==editorsBefore+1);CHECK(sends==sendsBeforeEditor);
    CHECK(Serial.output.find("editor CLOSED accepted=1 length=7; no text sent")!=std::string::npos);
    runChatEditorPreview();CHECK(editorCalls==editorsBefore+1);
    serialChatEditing=true;Serial.feed("chatedit\n");runPoll();CHECK(!serialChatEditPending);serialChatEditing=false;
    observed.connected=false;Serial.feed("chatedit\n");runPoll();CHECK(!serialChatEditPending);observed.connected=true;
    observed.mode=LabMode::Monitor;Serial.feed("chatedit\n");runPoll();CHECK(!serialChatEditPending);observed.mode=LabMode::Chat;
    observed.chatDiagnostic=true;Serial.feed("chatedit\n");runPoll();CHECK(!serialChatEditPending);observed.chatDiagnostic=false;
    Serial.feed("chatedit\n");runPoll();CHECK(serialChatEditPending);editorAccept=false;
    Serial.feed("chatedit\nscreen\nchatclose\n");runChatEditorPreview();
    CHECK(editorCalls==editorsBefore+2&&!serialChatEditing&&!serialChatEditPending);
    CHECK(serialChatCloseRequested&&!observed.active&&sends==sendsBeforeEditor&&maxDepth==1);
    CHECK(Serial.output.find("editor CLOSED accepted=0 length=7; no text sent")!=std::string::npos);
    serialChatEditPending=true;runChatEditorPreview();CHECK(!serialChatEditPending&&editorCalls==editorsBefore+2);
    // Execute the actual production editor with deterministic button/display
    // doubles, independently of the UART preview stub above.
    serialChatContext=false;serialChatEditing=false;
    struct Guarded {char before;char text[12];char after;} draft={'L',"seed",'R'};
    editorScript({EDown,EOk,EHoldBack});
    CHECK(!testedTextEntry("test",draft.text,sizeof draft.text));
    CHECK(!std::strcmp(draft.text,"seed")&&draft.before=='L'&&draft.after=='R');
    CHECK(!serialChatEditing&&uiFrames==uiClears);
    CHECK(drawn("a")&&drawn("b")&&drawn("SP")&&drawn("<-")&&drawn(">>")&&drawn("OK"));
    draft.text[0]=0;editorScript({EOk,EDown,EOk,EHoldOk});
    CHECK(testedTextEntry("test",draft.text,sizeof draft.text));CHECK(!std::strcmp(draft.text,"ab"));
    CHECK(eventAt==editorEvents.size()&&!serialChatEditing);
    editorScript({EBack,EBack,EHoldOk});CHECK(testedTextEntry("test",draft.text,sizeof draft.text));CHECK(!draft.text[0]);
    editorScript({EUp,EOk});CHECK(testedTextEntry("test",draft.text,sizeof draft.text));CHECK(!draft.text[0]);
    editorScript({EUp,EUp,EUp,EUp,EOk,EHoldOk});
    CHECK(testedTextEntry("test",draft.text,sizeof draft.text));CHECK(!std::strcmp(draft.text," "));
    draft.text[0]=0;editorScript({EUp,EUp,EUp,EUp,EOk,EHoldOk});
    CHECK(testedTextEntry("test",draft.text,sizeof draft.text,"0123456789"));CHECK(!draft.text[0]);CHECK(drawn("Пробел недоступен"));
    editorScript({EHoldDown,EOk,EHoldOk});CHECK(testedTextEntry("test",draft.text,sizeof draft.text));CHECK(!std::strcmp(draft.text,"A"));
    draft.text[0]=0;editorScript({EHoldUp,EOk,EHoldOk});CHECK(testedTextEntry("test",draft.text,sizeof draft.text));CHECK(!std::strcmp(draft.text,"0"));
    char tiny[2]="";uiTime=UINT32_MAX-15;editorScript({EOk,EOk,EHoldOk});
    CHECK(testedTextEntry("test",tiny,sizeof tiny));CHECK(!std::strcmp(tiny,"a"));CHECK(drawn("Поле заполнено"));
    char large[162]="unchanged";editorScript({});CHECK(!testedTextEntry("test",large,sizeof large));CHECK(!std::strcmp(large,"unchanged"));
    char unterminated[2]={'a','a'};CHECK(!testedTextEntry("test",unterminated,sizeof unterminated));CHECK(unterminated[0]=='a'&&unterminated[1]=='a');
    std::strcpy(draft.text,"a");CHECK(!testedTextEntry("test",draft.text,sizeof draft.text,"0123456789"));CHECK(!std::strcmp(draft.text,"a"));
    serialChatContext=false;serialChatCloseRequested=false;observed={};inMenu=true;
    const unsigned beforeCaptureSends=sends;
    Serial.feed("captureopen\ncapturestat\ncapturesave\ncaptureclose\n");runPoll();
    CHECK(serialCaptureOpenPending&&captureCalls==0);CHECK(Serial.available()>0);
    radioLabHandlePending();CHECK(!serialCaptureOpenPending&&captureCalls==1&&captureStats==1&&inMenu);
    CHECK(sends==beforeCaptureSends&&maxDepth==1);radioLabHandlePending();CHECK(captureCalls==1);
    Serial.feed("capturesave\ncaptureclose\n");runPoll();CHECK(!serialCaptureSavePending&&!serialCaptureCloseRequested);
    serialCaptureContext=true;Serial.feed("chatopen\ncaptureopen\n");runPoll();CHECK(!serialChatOpenPending&&!serialCaptureOpenPending);serialCaptureContext=false;
    Serial.feed("captureopen\n");runPoll();CHECK(serialCaptureOpenPending);observed.active=true;
    radioLabHandlePending();CHECK(!serialCaptureOpenPending&&captureCalls==1);observed.active=false;
    observed.captureQualifiedPulses=37;observed.captureRssiQualified=true;Serial.output.clear();testedPrintCaptureStatus();
    CHECK(Serial.output.find("qualifiedPulses=37 rssiQualified=1")!=std::string::npos);
    observed.captureQualifiedPulses=0;observed.captureRssiQualified=false;Serial.output.clear();testedPrintCaptureStatus();
    CHECK(Serial.output.find("qualifiedPulses=0 rssiQualified=0")!=std::string::npos);
    std::printf("Radio serial controller: %u checks passed (actual extracted functions, UART/UI/worker doubles)\n",checks);
}
