#include "radio_calibration.h"
#include "trim_math.h"
#include "lab_service.h"
#include "Preferences.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
using namespace research;
static unsigned checks;
static void check(bool okay, int line) {
    ++checks;
    if(!okay) { std::fprintf(stderr,"FAIL line%d\n",line); std::exit(1); }
}
#define CHECK(test) check(bool(test),__LINE__)
namespace {
bool busy, present, beginFailure, opened;
int32_t stored;
PreferenceType type = PT_I32;
size_t writeResult = 4;
unsigned begins, ends, writes;
bool supported(uint32_t hz) {
    return (hz>=300000000 && hz<=348000000) ||
        (hz>=387000000 && hz<=464000000) || (hz>=779000000 && hz<=928000000);
}
void vector(uint32_t nominal,int32_t ppm) {
    uint32_t output=0xAABBCCDD;
    const long double adjusted=std::floor(static_cast<long double>(nominal)*
        (1000000.0L+static_cast<long double>(ppm))/1000000.0L+0.5L);
    const bool expected=ppm>=-250 && ppm<=250 && supported(nominal) &&
        adjusted>=0 && adjusted<=UINT32_MAX && supported(uint32_t(adjusted));
    CHECK(trimmedFrequencyHz(nominal,ppm,output)==expected);
    if(expected) {
        CHECK(output==uint32_t(adjusted));
        const uint32_t word=cc1101FrequencyWord(output);
        CHECK(word!=0);
        const uint64_t realized=uint64_t(word)*26000000UL;
        CHECK(realized>=uint64_t(300000000)*65536);
        CHECK(realized<=uint64_t(928000000)*65536);
        CHECK(std::fabs(static_cast<double>(realized)/65536.0-output)<397.0);
    } else CHECK(output==0xAABBCCDD);
}
}
namespace research {
LabSnapshot labSnapshot() { LabSnapshot state={};state.active=busy;return state; }
}
bool Preferences::begin(const char* name,bool readOnly) {
    ++begins;CHECK(!opened);CHECK(std::strcmp(name,"esphack_rf")==0);CHECK(!readOnly);
    opened=!beginFailure;return opened;
}
void Preferences::end() { CHECK(opened);opened=false;++ends; }
bool Preferences::isKey(const char* key) { CHECK(opened);CHECK(!std::strcmp(key,"ppm"));return present; }
PreferenceType Preferences::getType(const char* key) { CHECK(opened);CHECK(!std::strcmp(key,"ppm"));return type; }
int32_t Preferences::getInt(const char* key,int32_t fallback) {
    CHECK(opened);CHECK(!std::strcmp(key,"ppm"));return present?stored:fallback;
}
size_t Preferences::putInt(const char* key,int32_t value) {
    CHECK(opened);CHECK(!std::strcmp(key,"ppm"));++writes;
    if(writeResult==sizeof(int32_t)){stored=value;present=true;type=PT_I32;}
    return writeResult;
}
int main() {
    CHECK(radioCalibrationPpm()==0);
    static const char* const badText[]={nullptr,"","+","-"," 1","1 ","1\n","++1","--1","+-1","1x","1.0","251","-251","2147483647","-2147483648","4294967296","00000000000","\t0"};
    for(const char* text:badText){int32_t ppm=777;CHECK(!parseRadioTrimPpm(text,ppm));CHECK(ppm==777);}
    for(int32_t value=-250;value<=250;++value){
        char text[16];std::snprintf(text,sizeof(text),"%ld",long(value));
        int32_t ppm=777;CHECK(parseRadioTrimPpm(text,ppm));CHECK(ppm==value);
        if(value>=0){std::snprintf(text,sizeof(text),"+%ld",long(value));CHECK(parseRadioTrimPpm(text,ppm));CHECK(ppm==value);}
    }
    int32_t parsed=777;CHECK(parseRadioTrimPpm("0000000250",parsed)&&parsed==250);
    CHECK(parseRadioTrimPpm("-0",parsed)&&parsed==0);
    CHECK(loadRadioCalibration());CHECK(radioCalibrationPpm()==0);CHECK(!present);CHECK(ends==1);
    static const uint32_t bands[][2]={{300000000,348000000},{387000000,464000000},{779000000,928000000}};
    for(int32_t ppm=-251;ppm<=251;++ppm) {
        for(unsigned b=0;b<3;++b) {
            for(unsigned edge=0;edge<2;++edge) {
                const uint32_t hz=bands[b][edge];
                for(int32_t d=-2;d<=2;++d)vector(uint32_t(int64_t(hz)+d),ppm);
            }
            for(uint32_t hz=bands[b][0];hz<=bands[b][1]-123457;hz+=123457)vector(hz,ppm);
        }
    }
    static const uint32_t extremes[]={0,1,UINT32_MAX,0x80000000,433920000,868350000};
    static const int32_t badPpm[]={INT32_MIN,INT32_MAX,-1000000,1000000,-251,251};
    for(uint32_t hz:extremes)for(int32_t ppm:badPpm)vector(hz,ppm);
    uint32_t output=0;
    CHECK(trimmedFrequencyHz(433920000,115,output)&&output==433969901);
    CHECK(trimmedFrequencyHz(433920000,-115,output)&&output==433870099);
    CHECK(!permitLabTx(TxProfile::ShieldedLab,433969901,100,1000));
    CHECK(permitLabTx(TxProfile::ShieldedLab,433920000,100,1000));
    for(int32_t ppm=-250;ppm<=250;++ppm) {
        CHECK(saveRadioCalibration(ppm));CHECK(stored==ppm);CHECK(radioCalibrationPpm()==ppm);
        CHECK(loadRadioCalibration());CHECK(radioCalibrationPpm()==ppm);
    }
    const unsigned before=begins;
    busy=true;CHECK(!saveRadioCalibration(0));CHECK(!loadRadioCalibration());CHECK(begins==before);
    busy=false;CHECK(!saveRadioCalibration(INT32_MIN));CHECK(!saveRadioCalibration(251));CHECK(begins==before);
    CHECK(radioCalibrationPpm()==250);
    beginFailure=true;CHECK(!loadRadioCalibration());CHECK(!saveRadioCalibration(0));CHECK(!opened);
    CHECK(radioCalibrationPpm()==250);beginFailure=false;
    writeResult=0;CHECK(!saveRadioCalibration(0));CHECK(radioCalibrationPpm()==250);CHECK(!opened);
    writeResult=2;CHECK(!saveRadioCalibration(0));CHECK(radioCalibrationPpm()==250);writeResult=4;
    stored=251;CHECK(!loadRadioCalibration());CHECK(radioCalibrationPpm()==250);
    stored=-251;CHECK(!loadRadioCalibration());CHECK(radioCalibrationPpm()==250);
    stored=0;type=PT_STR;CHECK(!loadRadioCalibration());CHECK(radioCalibrationPpm()==250);
    present=false;CHECK(loadRadioCalibration());CHECK(radioCalibrationPpm()==0);
    CHECK(!opened);CHECK(ends==begins-2);CHECK(writes==503);
    std::printf("radio calibration: %u checks passed\n",checks);
}
