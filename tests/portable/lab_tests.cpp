#include "lab_limits.h"
#include "test_decoder.h"
#include <limits.h>
#include <stdio.h>
#include <string.h>

using namespace research;
static unsigned checks;
#define CHECK(test) do { ++checks; if(!(test)) { fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#test); return 1; } } while(0)
int main() {
    CHECK(!permitLabTx(TxProfile::ReceiveOnly,433920000,100,1000));
    CHECK(permitLabTx(TxProfile::ShieldedLab,433920000,1,1));
    CHECK(permitLabTx(TxProfile::ShieldedLab,868350000,5000,5000));
    CHECK(!permitLabTx(TxProfile::ShieldedLab,433919830,100,1000));
    CHECK(!permitLabTx(TxProfile::ShieldedLab,433920000,0,1000));
    CHECK(!permitLabTx(TxProfile::ShieldedLab,433920000,1001,1000));
    CHECK(!permitLabTx(TxProfile::ShieldedLab,433920000,100,5001));
    CHECK(!permitLabTx(TxProfile::ShieldedLab,433920000,UINT32_MAX,5000));
    CHECK(cc1101FrequencyWord(433920000UL)==1093745UL);
    CHECK(!cc1101FrequencyWord(0)); CHECK(!cc1101FrequencyWord(348000001));
    CHECK(!cc1101FrequencyWord(500000000)); CHECK(!cc1101FrequencyWord(UINT32_MAX));
    const uint32_t bands[][2]={{300000000,348000000},{387000000,464000000},{779000000,928000000}};
    for(unsigned b=0;b<3;++b) for(unsigned i=0;i<=1000;++i) {
        const uint32_t hz=bands[b][0]+uint32_t(uint64_t(bands[b][1]-bands[b][0])*i/1000);
        const uint32_t word=cc1101FrequencyWord(hz);
        const uint64_t synthesized=uint64_t(word)*26000000;
        CHECK(synthesized>=uint64_t(bands[b][0])*65536);
        CHECK(synthesized<=uint64_t(bands[b][1])*65536);
        const uint64_t target=uint64_t(hz)*65536;
        const uint64_t error=synthesized>target?synthesized-target:target-synthesized;
        CHECK(error<26000000); // less than one synthesizer step, including edge clamps.
    }
    CHECK(rmtHalves(0)==0); CHECK(rmtHalves(INT32_MIN)==0);
    const int32_t durations[]={1,32766,32767,32768,65534,65535,INT32_MAX};
    for(unsigned i=0;i<sizeof durations/sizeof durations[0];++i) {
        const int32_t us=durations[i];
        const size_t halves=rmtHalves(us);
        CHECK(halves==rmtHalves(-us));
        CHECK(uint64_t(halves)*32767>=uint32_t(us));
        CHECK(uint64_t(halves-1)*32767<uint32_t(us));
    }
    LabMode mode=transition(LabMode::Idle,LabEvent::StartRaw); CHECK(mode==LabMode::Raw);
    mode=transition(mode,LabEvent::Stop); CHECK(mode==LabMode::Idle);
    mode=transition(mode,LabEvent::StartChat); CHECK(mode==LabMode::Chat);
    mode=transition(mode,LabEvent::Stop); CHECK(mode==LabMode::Idle);
    // CRC-8/SMBUS independent known-answer vector: "123456789" has F4.
    // For this fixed two-byte payload, 0x1234 has CRC F1 (poly07/init00).
    CHECK(testCrc8(0x1234)==0xF1);
    int32_t pulses[52];
    for(unsigned i=0;i<52;++i) pulses[i]=0x12345678;
    CHECK(!encodeLabOok(0x1234,pulses,49));
    CHECK(pulses[0]==0x12345678);
    CHECK(encodeLabOok(0x1234,pulses,50)==50);
    CHECK(pulses[50]==0x12345678 && pulses[51]==0x12345678);
    DecodedSignal decoded;
    CHECK(decodeLabOok(pulses,50,decoded)); CHECK(decoded.valid && decoded.payload==0x1234 && decoded.bits==24);
    CHECK(!decodeLabOok(pulses,49,decoded));
    CHECK(!decodeLabOok(0,50,decoded));
    // Every single symbol error must be detected by the CRC, not treated as a new payload.
    for(unsigned bit=0;bit<24;++bit) {
        const int32_t saved=pulses[2+bit*2]; pulses[2+bit*2]=saved==300?900:300;
        CHECK(!decodeLabOok(pulses,50,decoded)); pulses[2+bit*2]=saved;
    }
    // Timing jitter within documented decoder tolerance retains the same data.
    for(unsigned i=2;i<50;++i) pulses[i]+=i&1?50:-50;
    CHECK(decodeLabOok(pulses,50,decoded)); CHECK(decoded.payload==0x1234);
    pulses[3]=0; CHECK(!decodeLabOok(pulses,50,decoded));
    CHECK(decoderAt(0)!=0); CHECK(!decoderAt(1));
    printf("lab portable tests: %u checks passed (limits, RMT splitting, decoder corruption, transitions)\n",checks);
    return 0;
}
