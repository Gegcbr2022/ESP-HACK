// The runner injects actual production controller function bodies at the marker.
// Arduino UART/UI/worker are doubles; no serial port or radio is accessed.
#include "lab_service.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdarg>
#include <string>
using namespace research;
static unsigned checks;
#define CHECK(x) do{++checks;if(!(x)){std::fprintf(stderr,"FAIL line%d: %s\n",__LINE__,#x);std::exit(1);}}while(0)
bool inMenu=true,serialChatContext=false,serialChatListen=false,serialChatReported=false;
bool serialChatCloseRequested=false,serialChatOpenPending=false;
ChatMode chatFormat=ChatMode::FlipperPlaintext;
static LabSnapshot observed={};
static unsigned pollDepth,maxDepth,uiCalls,sends,stops,restores,starts,history,diagnostics,stats,screens;
static uint32_t startFrequency,startTimeout;static uint16_t sentDestination;
static bool failStart;
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
bool labStart(LabMode mode,const RegisterImage&,const RadioSettings& settings,TxProfile profile,uint8_t power,uint32_t step,ChatMode format,bool diagnostic){
    CHECK(mode==LabMode::Chat&&profile==TxProfile::ReceiveOnly&&power==0&&step==25000);
    CHECK(format==ChatMode::FlipperPlaintext&&diagnostic);CHECK(settings.rxTimeoutMs==30000);
    ++starts;startFrequency=settings.frequencyHz;startTimeout=settings.rxTimeoutMs;
    observed.active=!failStart;observed.chatDiagnostic=!failStart;return !failStart;
}
}
void suspendLegacyReceive(){}
void restoreReceiveMode(){++restores;}
void addHistory(uint16_t,bool sent,const char*){CHECK(sent);++history;}
void printChatStatus(){++stats;}
void printSerialScreen(){++screens;}
void printSerialChatQueueFailure(){}
bool pollSerialChatListen(){return true;}
bool handleIdleSerialCommand(const char*){++diagnostics;return true;}
void radioLabSerialPoll();void radioLabHandlePending();
static void runPoll(){++pollDepth;if(pollDepth>maxDepth)maxDepth=pollDepth;radioLabSerialPoll();--pollDepth;}
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
    std::printf("Radio serial controller: %u checks passed (actual extracted functions, UART/UI/worker doubles)\n",checks);
}
