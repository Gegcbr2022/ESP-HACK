#include "radio_config.h"
#include "lab_limits.h"
#include "capture_history.h"
#include "../interface/utf8_text.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdarg>
#include <string>
#include <vector>
using namespace research;
static unsigned checks;
#define CHECK(x) do{++checks;if(!(x)){std::fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);std::exit(1);}}while(0)
RadioSettings settings;RegisterImage radio;int32_t waveform[kWaveformCapacity];size_t waveformCount;
char captureSavedPath[112],notice[80];bool serialCaptureContext,serialCaptureSavePending,serialCapturePlayPending;
bool back,holdBack,holdOk,up,down,ok;
static capture_history::History history;static int32_t bank[8192];
static RegisterImage slotImages[4];static RadioSettings slotSettings[4];
static bool active,seedEntries=true,copyFailure,writeFailure,uniqueFailure;
static bool oversizedCopy;
static unsigned starts,stops,copies,writes,eventAt,stopFailures,startFailures,seedCount,clears,plays,clearFailures;
static std::vector<unsigned> events;static std::vector<uint32_t> savedFrequencies;
static std::vector<std::string> headings,serialOutput;
enum { Up=1,Down=2,Ok=4,Back=8,HoldOk=16,HoldBack=32,SerialSave=64 };
static const uint16_t SH110X_WHITE=1,SH110X_BLACK=0;
void input(){CHECK(eventAt<events.size());const unsigned e=events[eventAt++];up=e&Up;down=e&Down;ok=e&Ok;back=e&Back;holdOk=e&HoldOk;holdBack=e&HoldBack;if(e&SerialSave){serialCaptureContext=true;serialCaptureSavePending=true;}}
void resetInput(){}
bool frame(){return true;}
void delay(unsigned){}
struct Display {void display(){}void fillRect(int x,int y,int w,int h,uint16_t){CHECK(x>=0&&y>=0&&x+w<=128&&y+h<=64);}} display;
namespace ui_text {void draw(Display&,int x,int y,const char* text,uint16_t=1,size_t limit=21){size_t glyphs=0;CHECK(validate(text,std::strlen(text),&glyphs));CHECK(x>=0&&x<128&&y>=0&&y+8<=64);CHECK(x+int((glyphs<limit?glyphs:limit)*6)<=128);}}
void title(const char* text){headings.push_back(text);size_t glyphs=0;CHECK(ui_text::validate(text,std::strlen(text),&glyphs)&&glyphs<=21);}
void line(unsigned row,const char* text){CHECK(row>=1&&row<=5);size_t glyphs=0;CHECK(ui_text::validate(text,std::strlen(text),&glyphs)&&glyphs<=21);}
void footer(const char* value){size_t glyphs=0;CHECK(ui_text::validate(value,std::strlen(value),&glyphs)&&glyphs<=21);}
void message(const char*,const char*){}
void showNotice(const char* value){std::snprintf(notice,sizeof(notice),"%s",value);}
struct MockSerial {void printf(const char* format,...){char buffer[256];va_list args;va_start(args,format);std::vsnprintf(buffer,sizeof buffer,format,args);va_end(args);serialOutput.push_back(buffer);}} Serial;
bool labStop(){++stops;if(stopFailures){--stopFailures;return false;}active=false;return true;}
bool labClearCaptureHistory(){CHECK(!active);++clears;if(clearFailures){--clearFailures;return false;}history.clear();return true;}
static void seed(){
    history.clear();
    const int32_t first[]={100,-300,100,-310},second[]={500,-1500,500,-1510};
    capture_history::CandidateStatus valid{true,false,0,0};
    CHECK(history.add(first,4,433920000,-40,valid)==capture_history::AddResult::Added);
    CHECK(history.add(second,4,906400000,-45,valid)==capture_history::AddResult::Added);
    CHECK(loadPreset("AM270",slotImages[0])&&describeRadio(slotImages[0],433920000,slotSettings[0]));
    CHECK(loadPreset("FM476",slotImages[1])&&describeRadio(slotImages[1],906400000,slotSettings[1]));
    if(seedCount==4){
        CHECK(history.add(first,4,300000000,-45,valid)==capture_history::AddResult::Added);
        CHECK(history.add(second,4,928000000,-45,valid)==capture_history::AddResult::Added);
        CHECK(loadPreset("AM270",slotImages[2])&&describeRadio(slotImages[2],300000000,slotSettings[2]));
        CHECK(loadPreset("FM476",slotImages[3])&&describeRadio(slotImages[3],928000000,slotSettings[3]));
    }
}
bool startMode(LabMode mode,bool automatic,bool multiple){CHECK(mode==LabMode::Raw&&automatic&&multiple);CHECK(settings.rxTimeoutMs==0);++starts;if(startFailures){--startFailures;return false;}active=true;if(seedEntries&&starts==1)seed();else history.clear();return true;}
size_t labCopyCaptureHistory(size_t index,int32_t* out,size_t capacity,RegisterImage& image,RadioSettings& context){
    CHECK(!active);++copies;const auto* entry=history.entry(index);if(oversizedCopy)return capacity+1;if(copyFailure||!entry||capacity<entry->count)return 0;
    std::memcpy(out,history.pulses(index),entry->count*sizeof(int32_t));image=slotImages[index];context=slotSettings[index];return entry->count;
}
bool uniquePath(char* output,size_t capacity,const char*,const char*){std::snprintf(output,capacity,"/subghz/LabRAW_%04u.sub",writes+1);return !uniqueFailure;}
bool saveWave(const char*){CHECK(!active);++writes;savedFrequencies.push_back(settings.frequencyHz);CHECK(waveformCount==4);const bool fsk=settings.frequencyHz==906400000||settings.frequencyHz==928000000;CHECK(waveform[0]==(fsk?500:100));CHECK(radio.value[0x10]==slotImages[fsk?1:0].value[0x10]);return !writeFailure;}
#include "capture_history_ui.inc"
void playSavedCaptureHistory(){++plays;}
HistoryUiSnapshot historyUiSnapshot(){HistoryUiSnapshot snapshot;snapshot.active=active;snapshot.thresholdReady=true;snapshot.count=history.size();snapshot.full=snapshot.count==4;for(size_t i=0;i<history.size();++i)snapshot.entries[i]=*history.entry(i);return snapshot;}
static void reset(std::initializer_list<unsigned> buttons){history.clear();CHECK(history.bind(bank,8192));events=buttons;eventAt=starts=stops=copies=writes=stopFailures=startFailures=clears=plays=clearFailures=0;active=false;seedEntries=true;seedCount=2;copyFailure=writeFailure=uniqueFailure=oversizedCopy=false;serialCaptureContext=serialCaptureSavePending=serialCapturePlayPending=false;captureHistorySavedMask=0;headings.clear();serialOutput.clear();savedFrequencies.clear();CHECK(loadPreset("AM270",radio)&&describeRadio(radio,433920000,settings));}
static void runApp(){const uint32_t prior=settings.rxTimeoutMs;captureHistoryApp();CHECK(settings.rxTimeoutMs==prior);}
int main(){
    reset({0,Down,Ok,Up,Ok,Back});runApp();CHECK(writes==2&&copies==2);CHECK(savedFrequencies[0]==906400000&&savedFrequencies[1]==433920000);CHECK(settings.frequencyHz==433920000&&radio.value[0x10]==slotImages[0].value[0x10]);CHECK(history.size()==0&&!active&&clears==1);
    reset({Ok,Back});seedEntries=false;runApp();CHECK(!writes&&!copies&&stops==1);
    reset({HoldOk,Back,HoldBack});runApp();CHECK(starts==1&&history.size()==0&&clears==1);
    reset({HoldOk,HoldOk,Back});runApp();CHECK(starts==2&&history.size()==0);
    reset({Down,Ok,HoldBack});copyFailure=true;runApp();CHECK(!writes&&copies==1&&settings.frequencyHz==433920000&&!captureHistorySavedMask);
    reset({Down,Ok,HoldBack});writeFailure=true;runApp();CHECK(writes==1&&settings.frequencyHz==433920000&&radio.value[0x10]==slotImages[0].value[0x10]&&!captureHistorySavedMask);
    reset({Down,Ok,HoldBack});uniqueFailure=true;runApp();CHECK(!writes&&settings.frequencyHz==433920000&&!captureHistorySavedMask);
    reset({HoldBack});runApp();const unsigned before=stops;CHECK(!saveSelectedCaptureHistory(4)&&stops==before);
    reset({Back,0,Ok,HoldBack});runApp();CHECK(eventAt==4&&stops==1&&!writes&&headings[0]=="Не всё сохранено");
    reset({Ok,Back,0,Ok,Down,Ok,Back});runApp();CHECK(writes==2&&captureHistorySavedMask==3&&eventAt==7);
    reset({Ok,HoldOk,HoldOk,Back});runApp();CHECK(starts==2&&history.size()==0&&!captureHistorySavedMask);
    reset({Down,Ok,HoldBack});stopFailures=1;runApp();CHECK(!copies&&!writes&&!captureHistorySavedMask&&!active);
    reset({Down,Ok,HoldBack});oversizedCopy=true;runApp();CHECK(copies==1&&!writes&&!captureHistorySavedMask);
    reset({SerialSave,HoldBack});runApp();CHECK(writes==1&&captureHistorySavedMask==1&&serialOutput.size()==1);
    CHECK(serialOutput[0].find("SAVED")!=std::string::npos&&serialOutput[0].find("slot=1")!=std::string::npos);
    reset({0,Up,Ok,HoldBack});seedCount=4;runApp();CHECK(history.size()==0&&writes==1&&savedFrequencies[0]==928000000&&captureHistorySavedMask==8&&headings[0]=="Приём: 4/4");
    reset({0,Down,Ok,HoldBack});seed();runApp();CHECK(!starts&&writes==1&&savedFrequencies[0]==906400000&&captureHistorySavedPathSlot==1);
    reset({0,HoldBack});serialCapturePlayPending=true;runApp();CHECK(plays==1&&!writes&&!serialCapturePlayPending);
    reset({HoldOk,HoldOk,HoldBack});clearFailures=1;runApp();CHECK(starts==1&&clears==2&&!active&&history.size()==0);
    reset({});startFailures=1;runApp();CHECK(!active&&!writes&&!stops&&starts==1);
    reset({});settings.packetFormat=PacketFormat::Fifo;runApp();CHECK(!starts&&!writes&&!stops);
    std::printf("capture history UI: %u checks passed (production UI include, real core model, selection/save/restart/unsaved guards, UTF8 display bounds)\n",checks);
}
