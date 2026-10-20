#include "static_sub_encoder.h"
#include "lab_limits.h"
#include <cstdio>
#include <cstring>
using namespace research;
static unsigned checks;
#define CHECK(test) do { ++checks; if(!(test)) { std::fprintf(stderr,"FAIL line%d: %s\n",__LINE__,#test); return false; } } while(0)
static SubInfo keyFile(const char* protocol,unsigned bits,uint32_t key,uint32_t te=390) {
    SubInfo info;info.kind=SubKind::Key;info.frequencyHz=433920000;info.bits=bits;
    std::snprintf(info.protocol,sizeof(info.protocol),"%s",protocol);info.keyBytes=8;info.teUs=te;
    for(unsigned i=0;i<4;++i)info.key[7-i]=uint8_t(key>>(8*i));
    loadPreset("AM270",info.radio);return info;
}
static bool waveform(const SubInfo& info,uint32_t key) {
    struct Guarded {uint32_t before;int32_t pulses[2048];uint32_t after;} memory;
    std::memset(&memory,0xA5,sizeof memory);memory.before=memory.after=0x12345678;
    StaticSubWaveform result={0,0,0,false};
    CHECK(encodeStaticSub(info,memory.pulses,2048,result)==StaticSubError::None);
    CHECK(result.repeats==6&&result.count==(2*info.bits+2)*6U&&result.count<=2048);
    CHECK(result.phaseRotated==!std::strcmp(info.protocol,"CAME"));
    CHECK(memory.before==0x12345678&&memory.after==0x12345678);
    uint64_t duration=0;size_t halves=0;
    for(size_t i=0;i<result.count;++i) {
        const int32_t pulse=memory.pulses[i];CHECK(pulse!=0&&pulse!=INT32_MIN);
        CHECK((pulse>0)==!(i&1));duration+=pulse>0?pulse:-pulse;halves+=rmtHalves(pulse);
    }
    CHECK(duration==result.durationUs&&duration<=5000000&&halves<=4094);
    const bool came=result.phaseRotated;
    const size_t period=2*info.bits+2;
    for(size_t repeat=0;repeat<6;++repeat) {
        size_t at=repeat*period;uint32_t decoded=0;
        if(came)CHECK(memory.pulses[at++]==320);
        for(unsigned bit=0;bit<info.bits;++bit) {
            const int32_t first=memory.pulses[at++],second=memory.pulses[at++];
            if(came) {
                CHECK(first<0&&second>0&&-first+second==960);
                CHECK((first==-640&&second==320)||(first==-320&&second==640));
                decoded=(decoded<<1)|(first==-640);
            } else {
                CHECK(first>0&&second<0&&first-second==int32_t(info.teUs*4));
                CHECK((first==int32_t(info.teUs*3)&&second==-int32_t(info.teUs))||
                      (first==int32_t(info.teUs)&&second==-int32_t(info.teUs*3)));
                decoded=(decoded<<1)|(first==int32_t(info.teUs*3));
            }
        }
        CHECK(decoded==key);
        if(came)CHECK(memory.pulses[at++]==-(info.bits==12?15040:24320));
        else {
            const unsigned guard=info.guardTime>=15&&info.guardTime<=72?info.guardTime:30;
            CHECK(memory.pulses[at++]==int32_t(info.teUs));CHECK(memory.pulses[at++]==-int32_t(info.teUs*guard));
        }
        CHECK(at==(repeat+1)*period);
    }
    return true;
}
static bool tests() {
    int32_t pulses[2048];StaticSubWaveform result={0,0,0,false};
    SubInfo info=keyFile("CAME",12,0xA53);
    CHECK(encodeStaticSub(info,pulses,2048,result)==StaticSubError::None);
    // Pinned FlipperCAME12 golden: original LOW15040 is rotated to end.
    const int32_t came[]={320,-640,320,-320,640,-640,320,-320,640,-320,640,-640,320,
                         -320,640,-640,320,-320,640,-320,640,-640,320,-640,320,-15040};
    CHECK(!std::memcmp(pulses,came,sizeof came));CHECK(result.durationUs==161280);
    CHECK(waveform(info,0xA53));
    info=keyFile("CAME",24,0x800001);
    CHECK(encodeStaticSub(info,pulses,2048,result)==StaticSubError::None);
    CHECK(result.durationUs==286080&&pulses[49]==-24320);CHECK(waveform(info,0x800001));
    info=keyFile("Princeton",24,0xA53A53);
    CHECK(encodeStaticSub(info,pulses,2048,result)==StaticSubError::None);
    const int units[]={3,-1,1,-3,3,-1,1,-3,1,-3,3,-1,1,-3,3,-1,1,-3,1,-3,3,-1,3,-1,
                       3,-1,1,-3,3,-1,1,-3,1,-3,3,-1,1,-3,3,-1,1,-3,1,-3,3,-1,3,-1,1,-30};
    for(unsigned i=0;i<50;++i)CHECK(pulses[i]==units[i]*390);
    CHECK(result.durationUs==297180);CHECK(waveform(info,0xA53A53));
    for(unsigned guard=0;guard<256;++guard){info.guardTime=uint8_t(guard);CHECK(waveform(info,0xA53A53));}
    uint32_t random=1;
    for(unsigned i=0;i<400;++i) {
        random=random*1664525+1013904223;const uint32_t key=random&0xFFFFFF;
        info=keyFile(i&1?"CAME":"Princeton",24,key,1+i%1000);CHECK(waveform(info,key));
        info=keyFile("CAME",12,key&0xFFF);CHECK(waveform(info,key&0xFFF));
    }
    info=keyFile("Princeton",24,0);CHECK(waveform(info,0));
    info=keyFile("Princeton",24,0xFFFFFF);CHECK(waveform(info,0xFFFFFF));
    info.teUs=0;CHECK(encodeStaticSub(info,pulses,2048,result)==StaticSubError::Timing);
    info.teUs=10001;CHECK(encodeStaticSub(info,pulses,2048,result)==StaticSubError::Timing);
    info.teUs=10000;CHECK(encodeStaticSub(info,pulses,2048,result)==StaticSubError::Duration);
    info=keyFile("CAME",12,0xFFF);std::memset(pulses,0x5A,sizeof pulses);
    StaticSubWaveform unchanged={77,88,99,true};result=unchanged;
    CHECK(encodeStaticSub(info,pulses,155,result)==StaticSubError::Capacity);
    CHECK(pulses[0]==0x5A5A5A5A&&result.count==77&&result.durationUs==88&&result.repeats==99&&result.phaseRotated);
    CHECK(encodeStaticSub(info,nullptr,2048,result)==StaticSubError::Capacity);
    info.bits=13;CHECK(encodeStaticSub(info,pulses,2048,result)==StaticSubError::Key);
    info=keyFile("CAME",12,0x1000);CHECK(encodeStaticSub(info,pulses,2048,result)==StaticSubError::Key);
    info=keyFile("CAME",12,1);info.key[0]=1;CHECK(encodeStaticSub(info,pulses,2048,result)==StaticSubError::Key);
    info=keyFile("CAME",12,1);info.keyBytes=9;CHECK(encodeStaticSub(info,pulses,2048,result)==StaticSubError::Key);
    info=keyFile("Princeton",12,1);CHECK(encodeStaticSub(info,pulses,2048,result)==StaticSubError::Key);
    info=keyFile("RcSwitch",24,1);CHECK(encodeStaticSub(info,pulses,2048,result)==StaticSubError::Protocol);
    const char* rolling[]={"KeeLoq","KeeLoq BFT","Alutech AT-4N","Star Line","StarLine"};
    for(unsigned i=0;i<5;++i){info=keyFile(rolling[i],24,1);CHECK(encodeStaticSub(info,pulses,2048,result)==StaticSubError::Dynamic);}
    info=keyFile("Princeton",24,1);loadPreset("FM476",info.radio);CHECK(encodeStaticSub(info,pulses,2048,result)==StaticSubError::Radio);
    loadPreset("AM270",info.radio);info.radio.pa[2]=1;CHECK(encodeStaticSub(info,pulses,2048,result)==StaticSubError::Radio);
    loadPreset("AM270",info.radio);info.frequencyHz=500000000;CHECK(encodeStaticSub(info,pulses,2048,result)==StaticSubError::Radio);
    std::printf("static .sub encoders: %u checks passed\n",checks);return true;
}
int main(){return tests()?0:1;}
