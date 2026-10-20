#include "static_sub_encoder.h"
#include "lab_limits.h"
#include <cstdio>
#include <cstring>
#include <bitset>
#include <string>
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
            const unsigned guard=!std::strcmp(info.protocol,"RcSwitch")?31:(info.guardTime>=15&&info.guardTime<=72?info.guardTime:30);
            CHECK(memory.pulses[at++]==int32_t(info.teUs));CHECK(memory.pulses[at++]==-int32_t(info.teUs*guard));
        }
        CHECK(at==(repeat+1)*period);
    }
    return true;
}
// Independent oracle: convert the numeric payload to an MSB-first bit string,
// then expand ideal HIGH/LOW unit cells. No encoder shifts or duration formula
// are reused. rc-switch2.6.4 protocol1 is zero=HLLL,one=HHHL,sync=H+31L.
static bool unitTimeline(const SubInfo& info,uint32_t key,unsigned guard) {
    int32_t pulses[2048];StaticSubWaveform result={};
    CHECK(encodeStaticSub(info,pulses,2048,result)==StaticSubError::None);
    CHECK(result.count<=2048);
    const std::string bits=std::bitset<32>(key).to_string().substr(32-info.bits);
    std::string period;
    for(size_t i=0;i<bits.size();++i)period+=bits[i]=='1'?"HHHL":"HLLL";
    period+='H';period.append(guard,'L');
    std::string expected,actual;
    for(unsigned i=0;i<6;++i)expected+=period;
    for(size_t i=0;i<result.count;++i) {
        const uint32_t duration=pulses[i]>0?uint32_t(pulses[i]):uint32_t(-int64_t(pulses[i]));
        CHECK(duration%info.teUs==0);
        actual.append(duration/info.teUs,pulses[i]>0?'H':'L');
    }
    CHECK(actual==expected);CHECK(result.durationUs==expected.size()*info.teUs);
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
    info=keyFile("RcSwitch",4,0xA,350);
    CHECK(encodeStaticSub(info,pulses,2048,result)==StaticSubError::None);
    const int32_t rcGolden[]={1050,-350,350,-1050,1050,-350,350,-1050,350,-10850};
    CHECK(!std::memcmp(pulses,rcGolden,sizeof rcGolden));CHECK(result.count==60&&result.durationUs==100800);
    CHECK(waveform(info,0xA));CHECK(unitTimeline(info,0xA,31));
    // Guard_time is a Princeton parameter; it cannot alter rc-switch protocol1.
    for(unsigned guard=0;guard<256;++guard){info.guardTime=uint8_t(guard);CHECK(unitTimeline(info,0xA,31));}
    uint32_t oracleRandom=0x29471031;
    for(unsigned bits=1;bits<=32;++bits) {
        const uint32_t mask=bits==32?UINT32_MAX:(uint32_t(1)<<bits)-1;
        oracleRandom=oracleRandom*1664525+1013904223;
        const uint32_t values[]={0,mask,uint32_t(1)<<(bits-1),oracleRandom&mask};
        for(unsigned kind=0;kind<2;++kind)for(unsigned i=0;i<4;++i) {
            info=keyFile(kind?"Princeton":"RcSwitch",bits,values[i],1+bits*17);
            CHECK(waveform(info,values[i]));CHECK(unitTimeline(info,values[i],kind?30:31));
            if(kind){info.guardTime=72;CHECK(unitTimeline(info,values[i],72));}
        }
    }
    info=keyFile("RcSwitch",1,1,10000);CHECK(waveform(info,1));CHECK(unitTimeline(info,1,31));
    info=keyFile("RcSwitch",32,UINT32_MAX,5208);
    CHECK(encodeStaticSub(info,pulses,2048,result)==StaticSubError::None);CHECK(result.durationUs==4999680&&result.count==396);
    info.teUs=5209;CHECK(encodeStaticSub(info,pulses,2048,result)==StaticSubError::Duration);
    info=keyFile("Princeton",32,UINT32_MAX,4145);info.guardTime=72;
    CHECK(encodeStaticSub(info,pulses,2048,result)==StaticSubError::None);CHECK(result.durationUs==4998870&&result.count==396);
    info.teUs=4146;CHECK(encodeStaticSub(info,pulses,2048,result)==StaticSubError::Duration);
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
    const char* supported[]={"Princeton","RcSwitch"};
    for(unsigned name=0;name<2;++name) {
        const unsigned invalidBits[]={0,33,64,65535};
        for(unsigned i=0;i<4;++i){info=keyFile(supported[name],invalidBits[i],1);CHECK(encodeStaticSub(info,pulses,2048,result)==StaticSubError::Key);}
        info=keyFile(supported[name],32,UINT32_MAX);info.key[3]=1;CHECK(encodeStaticSub(info,pulses,2048,result)==StaticSubError::Key);
        info=keyFile(supported[name],1,2);CHECK(encodeStaticSub(info,pulses,2048,result)==StaticSubError::Key);
        info=keyFile(supported[name],32,UINT32_MAX);std::memset(pulses,0x5A,sizeof pulses);result=unchanged;
        CHECK(encodeStaticSub(info,pulses,395,result)==StaticSubError::Capacity);
        CHECK(pulses[0]==0x5A5A5A5A&&pulses[2047]==0x5A5A5A5A&&result.count==77&&result.durationUs==88&&result.repeats==99&&result.phaseRotated);
        info.teUs=0;CHECK(encodeStaticSub(info,pulses,2048,result)==StaticSubError::Timing);
        info.teUs=10001;CHECK(encodeStaticSub(info,pulses,2048,result)==StaticSubError::Timing);
    }
    const char* aliases[]={"RCSwitch","RCswitch","RcSwitch1","RcSwitch2","princeton","Princeton1","Came","CAME TWEE"};
    for(unsigned i=0;i<8;++i){info=keyFile(aliases[i],24,1);CHECK(encodeStaticSub(info,pulses,2048,result)==StaticSubError::Protocol);}
    const char* rolling[]={"KeeLoq","KeeLoq BFT","Alutech AT-4N","Star Line","StarLine"};
    for(unsigned i=0;i<5;++i){info=keyFile(rolling[i],24,1);CHECK(encodeStaticSub(info,pulses,2048,result)==StaticSubError::Dynamic);}
    info=keyFile("Princeton",24,1);loadPreset("FM476",info.radio);CHECK(encodeStaticSub(info,pulses,2048,result)==StaticSubError::Radio);
    loadPreset("AM270",info.radio);info.radio.pa[2]=1;CHECK(encodeStaticSub(info,pulses,2048,result)==StaticSubError::Radio);
    loadPreset("AM270",info.radio);info.frequencyHz=500000000;CHECK(encodeStaticSub(info,pulses,2048,result)==StaticSubError::Radio);
    std::printf("static .sub encoders: %u checks passed\n",checks);return true;
}
int main(){return tests()?0:1;}
