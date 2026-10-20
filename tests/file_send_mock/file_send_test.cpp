#include "lab_service.h"
#include "sub_file.h"
#include "static_sub_encoder.h"
#include "trim_math.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
using namespace research;
static unsigned checks;
#define CHECK(x) do{++checks;if(!(x)){std::fprintf(stderr,"FAIL line%d: %s\n",__LINE__,#x);std::exit(1);}}while(0)
RadioSettings settings;RegisterImage radio;TxProfile profile=TxProfile::ReceiveOnly;uint8_t powerIndex;
SubInfo openedInfo;bool openedFileValid;int32_t waveform[kWaveformCapacity];size_t waveformCount;
bool back,holdBack,holdOk;
static int32_t trim;
static unsigned transmissions,resets,messages;
static uint32_t expectedHz;static TxProfile expectedProfile;
static bool transmitSuccess=true;
static std::vector<unsigned> events;static size_t eventAt;
static std::vector<std::string> rendered;
enum { Cancel=1,HoldCancel=2,Transmit=4 };
void input(){CHECK(eventAt<events.size());unsigned e=events[eventAt++];back=e&Cancel;holdBack=e&HoldCancel;holdOk=e&Transmit;}
void resetInput(){++resets;}
bool frame(){return true;}
void title(const char*){}
void line(unsigned,const char* text){rendered.push_back(text);}
void footer(const char*){}
struct Display {void display(){}} display;
void message(const char*,const char* text){++messages;rendered.push_back(text);}
namespace research {
int32_t radioCalibrationPpm(){return trim;}
bool labTransmitRaw(const int32_t* pulses,size_t count,const RegisterImage& image,uint32_t hz,TxProfile tx,uint8_t power,uint32_t limit,uint32_t& elapsed,int32_t& timing){
    ++transmissions;CHECK(hz==expectedHz);CHECK(tx==TxProfile::FilePlayback);CHECK(power==1&&limit==5000);
    CHECK(count>0&&count<=kWaveformCapacity&&pulses==waveform);CHECK(image.paPresent);
    CHECK(settings.frequencyHz==hz&&settings.txLimitMs==5000);CHECK(profile==expectedProfile);CHECK(powerIndex==1);
    // Stress the UI scope guard on both backend outcomes. The actual backend's
    // hardware register restoration is verified in its separate SPI mocks.
    settings.frequencyHz=123;settings.agc0=0xFF;settings.txLimitMs=7;radio.set(0x13,0x99);profile=TxProfile::FilePlayback;powerIndex=0;
    elapsed=600;timing=0;return transmitSuccess;
}
}
// PRODUCTION_FUNCTIONS
static bool has(const char* text){for(const std::string& value:rendered)if(value.find(text)!=std::string::npos)return true;return false;}
static void fixture(uint32_t hz,SubKind kind=SubKind::Raw){
    openedInfo=SubInfo();openedInfo.kind=kind;openedInfo.frequencyHz=hz;openedFileValid=true;
    CHECK(loadPreset("AM270",openedInfo.radio));waveformCount=4;
    waveform[0]=100;waveform[1]=-100;waveform[2]=200;waveform[3]=-200;
    openedInfo.raw.pulses=4;openedInfo.raw.durationUs=600;openedInfo.rawCompatible=true;
    if(kind==SubKind::Raw)std::strcpy(openedInfo.protocol,"RAW");
    else {std::strcpy(openedInfo.protocol,"Princeton");openedInfo.bits=24;openedInfo.teUs=390;openedInfo.keyBytes=8;openedInfo.key[5]=0xA5;openedInfo.key[6]=0x3A;openedInfo.key[7]=0x53;}
}
static void run(std::initializer_list<unsigned> inputEvents,bool expectedSend){
    CHECK(loadPreset("FM476",radio));CHECK(describeRadio(radio,868350000,settings));
    settings.agc0=0x42;settings.txLimitMs=1234;profile=TxProfile::ShieldedLab;powerIndex=2;
    const RadioSettings saved=settings;const RegisterImage savedRadio=radio;
    expectedHz=openedInfo.frequencyHz;expectedProfile=profile;events=inputEvents;eventAt=0;rendered.clear();
    const unsigned before=transmissions;sendOpenedFile("/subghz/example (test).sub");
    CHECK(transmissions==before+(expectedSend?1:0));
    CHECK(settings.frequencyHz==saved.frequencyHz&&settings.txLimitMs==saved.txLimitMs&&settings.agc0==saved.agc0);
    CHECK(settings.modulation==saved.modulation&&settings.bandwidthHz==saved.bandwidthHz&&settings.syncWord==saved.syncWord);
    CHECK(profile==TxProfile::ShieldedLab&&powerIndex==2);
    CHECK(!std::memcmp(radio.value,savedRadio.value,sizeof radio.value));CHECK(!std::memcmp(radio.present,savedRadio.present,sizeof radio.present));
    CHECK(!std::memcmp(radio.pa,savedRadio.pa,sizeof radio.pa)&&radio.paPresent==savedRadio.paPresent);
}
int main(){
    fixture(906400000);run({0,Transmit},true);CHECK(has("906.400"));CHECK(has("Передача завершена"));
    fixture(906400000);transmitSuccess=false;run({Transmit},true);CHECK(has("Не передано"));transmitSuccess=true;
    fixture(906400000);run({Cancel},false);fixture(906400000);run({HoldCancel},false);
    fixture(433920000,SubKind::Key);run({Transmit},true);CHECK(waveformCount==300);
    fixture(906400000,SubKind::Key);run({Transmit},true);
    fixture(906400000);openedFileValid=false;run({},false);CHECK(has("Сначала откройте"));
    fixture(906400000);openedInfo.raw.pulses=2049;run({},false);CHECK(has("2048"));
    fixture(906400000);openedInfo.raw.durationUs=5000001;run({},false);CHECK(has("5 сек"));
    fixture(906400000);waveform[0]=-100;run({},false);CHECK(has("LOW"));
    fixture(906400000);openedInfo.radio.pa[2]=1;run({},false);CHECK(has("Многоуровневый"));
    fixture(906400000,SubKind::Key);std::strcpy(openedInfo.protocol,"KeeLoq");run({},false);CHECK(has("не поддержана"));
    fixture(500000000);run({},false);CHECK(has("Нужен OOK"));
    fixture(928000000);trim=115;run({},false);CHECK(has("поправкой"));trim=0;
    CHECK(!permitLabTx(TxProfile::ShieldedLab,906400000,1,5000));
    CHECK(permitLabTx(TxProfile::FilePlayback,906400000,1,5000));
    uint32_t hz=99;CHECK(serialPrincetonTestFrequency("rftest princeton433",hz)&&hz==433920000);
    const uint32_t valid[]={300000000,348000000,387000000,433920000,464000000,779000000,906400000,928000000};
    for(uint32_t value:valid){const std::string command="rftest princeton "+std::to_string(value);CHECK(serialPrincetonTestFrequency(command.c_str(),hz)&&hz==value);}
    const char* invalid[]={nullptr,"rftest princeton","rftest princeton ","rftest princeton +906400000","rftest princeton -1","rftest princeton 906400000 ","rftest princeton 906400000x","rftest princeton 4294967296","rftest princeton 99999999999999999999","rftest princeton 348000001","rftest princeton 500000000","rftest princeton 928000001","rftest princeton433x","rftest unknown 433920000"};
    for(const char* command:invalid){hz=99;CHECK(!serialPrincetonTestFrequency(command,hz));CHECK(hz==99);}
    std::printf("File send UI: %u checks passed (actual extracted flow, production codecs, mocked radio)\n",checks);
}
