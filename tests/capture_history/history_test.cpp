#include "capture_history.h"
#include <limits.h>
#include <stdio.h>
#include <string.h>

using namespace research::capture_history;
static unsigned checks;
#define CHECK(x) do { ++checks; if(!(x)) { fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x); return false; } } while(0)
static const CandidateStatus complete = {true,false,0,0};
static int32_t bank[kBankPulses + 2];
static int32_t other[kBankPulses];
static void resetBank() {
    for(size_t i=0;i<kBankPulses+2;++i) bank[i]=0x12345678;
}
static bool bind_and_metadata() {
    resetBank();History h;size_t selected=123;
    const int32_t wave[]={100,-200,300,-400};
    CHECK(!h.bound()&&h.size()==0&&!h.entry(0)&&!h.pulses(0));
    CHECK(h.add(wave,4,433889000,-80,complete,&selected)==AddResult::NoBank&&selected==123);
    CHECK(!h.bind(nullptr,kBankPulses));CHECK(!h.bind(bank+1,kBankPulses-1));
    alignas(int32_t) char misaligned[sizeof(int32_t)*kBankPulses+1];
    CHECK(!h.bind(reinterpret_cast<int32_t*>(misaligned+1),kBankPulses));
    CHECK(h.bind(bank+1,kBankPulses)&&h.bound());
    CHECK(h.add(wave,4,433889000,-80,complete,&selected)==AddResult::Added&&selected==0);
    CHECK(h.size()==1&&h.entry(0)&&h.entry(0)->valid);
    CHECK(h.entry(0)->count==4&&h.entry(0)->frequencyHz==433889000&&h.entry(0)->durationUs==1000);
    CHECK(h.entry(0)->rssiDbm==-80&&h.entry(0)->observations==1);
    CHECK(h.entry(0)->hash==0x1D89BD4DUL); // independent Python struct.pack/FNV vector
    CHECK(!memcmp(h.pulses(0),wave,sizeof wave));
    CHECK(bank[0]==0x12345678&&bank[kBankPulses+1]==0x12345678);
    for(size_t i=5;i<=kBankPulses;++i)CHECK(bank[i]==0x12345678);
    CHECK(!h.bind(other,kBankPulses)&&h.pulses(0)==bank+1);
    CHECK(h.bind(bank+1,kBankPulses));
    CHECK(!h.entry(1)&&!h.entry(SIZE_MAX)&&!h.pulses(1)&&!h.pulses(SIZE_MAX));
    h.clear();CHECK(h.bound()&&h.size()==0&&!h.entry(0)&&!h.pulses(0));
    CHECK(h.bind(other,kBankPulses));
    h.unbind();CHECK(!h.bound()&&h.size()==0);
    return true;
}
static bool candidate_validation() {
    History h;CHECK(h.bind(bank+1,kBankPulses));size_t selected=77;
    int32_t wave[2049];for(unsigned i=0;i<2049;++i)wave[i]=i&1?-1000:1000;
    CHECK(h.add(nullptr,2,433920000,-70,complete,&selected)==AddResult::Invalid);
    CHECK(h.add(wave,0,433920000,-70,complete,&selected)==AddResult::Invalid);
    CHECK(h.add(wave,2049,433920000,-70,complete,&selected)==AddResult::Invalid);
    CHECK(h.add(wave,SIZE_MAX,433920000,-70,complete,&selected)==AddResult::Invalid);
    const CandidateStatus denied[]={
        {false,false,0,0},{true,true,0,0},{true,false,1,0},
        {true,false,0,1},{true,false,UINT32_MAX,UINT32_MAX}
    };
    for(size_t i=0;i<sizeof denied/sizeof denied[0];++i)
        CHECK(h.add(wave,24,433920000,-70,denied[i],&selected)==AddResult::Invalid);
    const uint32_t deniedHz[]={0,299999999,348000001,386999999,464000001,778999999,928000001,UINT32_MAX};
    for(size_t i=0;i<sizeof deniedHz/sizeof deniedHz[0];++i)
        CHECK(h.add(wave,24,deniedHz[i],-70,complete,&selected)==AddResult::Invalid);
    for(size_t i=0;i<2048;++i) {
        const int32_t saved=wave[i];
        wave[i]=0;CHECK(h.add(wave,2048,433920000,-70,complete,&selected)==AddResult::Invalid);
        wave[i]=INT32_MIN;CHECK(h.add(wave,2048,433920000,-70,complete,&selected)==AddResult::Invalid);
        wave[i]=-saved;CHECK(h.add(wave,2048,433920000,-70,complete,&selected)==AddResult::Invalid);
        wave[i]=saved;
    }
    CHECK(h.size()==0&&selected==77);
    // The backend guarantees signal qualification. The model validates a
    // complete RAW span, without imposing a protocol bit count or TX duration.
    CHECK(h.add(wave,1,433920000,-70,complete,&selected)==AddResult::Added&&selected==0);
    CHECK(h.entry(0)->count==1&&h.entry(0)->durationUs==1000);
    return true;
}
static bool tolerance_boundaries() {
    const uint32_t reference[]={1,100,1000,1000,INT32_MAX};
    const uint32_t candidate[]={41,160,1240,800,INT32_MAX-1};
    for(size_t test=0;test<sizeof reference/sizeof reference[0];++test) {
        for(unsigned reverse=0;reverse<2;++reverse) {
            History h;CHECK(h.bind(bank+1,kBankPulses));
            const int32_t a=int32_t(reverse?candidate[test]:reference[test]);
            const int32_t b=int32_t(reverse?reference[test]:candidate[test]);
            const int32_t first[]={a,-a},second[]={b,-b};
            CHECK(h.add(first,2,906400000,-100,complete)==AddResult::Added);
            CHECK(h.add(second,2,906400000,-50,complete)==AddResult::Duplicate);
            CHECK(h.size()==1&&h.entry(0)->observations==2&&h.entry(0)->rssiDbm==-50);
            CHECK(h.pulses(0)[0]==a&&h.pulses(0)[1]==-a);
            CHECK(h.entry(0)->durationUs==uint64_t(a)*2);
        }
    }
    const int32_t outside[][2]={{1,42},{100,161},{1000,1241},{1000,799}};
    for(size_t i=0;i<sizeof outside/sizeof outside[0];++i) {
        for(unsigned reverse=0;reverse<2;++reverse) {
            History h;CHECK(h.bind(bank+1,kBankPulses));
            const int32_t a=outside[i][reverse],b=outside[i][1-reverse];
            const int32_t first[]={a,-a},second[]={b,-b};
            CHECK(h.add(first,2,433920000,-80,complete)==AddResult::Added);
            CHECK(h.add(second,2,433920000,-80,complete)==AddResult::Added);
            CHECK(h.size()==2);
        }
    }
    History h;CHECK(h.bind(bank+1,kBankPulses));
    const int32_t first[]={1000,-1000,1000,-1000};
    const int32_t distinct[]={1000,-1000,1000,-1241};
    CHECK(h.add(first,4,433920000,-80,complete)==AddResult::Added);
    CHECK(h.add(distinct,4,433920000,-80,complete)==AddResult::Added); // one changed interval suffices
    CHECK(h.add(first,2,433920000,-80,complete)==AddResult::Added); // count differs
    CHECK(h.add(first,4,906400000,-80,complete)==AddResult::Added); // frequency differs
    return true;
}
static bool capacity_and_immutability() {
    resetBank();History h;CHECK(h.bind(bank+1,kBankPulses));
    int32_t longest[kPulsesPerSlot];
    for(size_t i=0;i<kPulsesPerSlot;++i)longest[i]=i&1?-INT32_MAX:INT32_MAX;
    const uint32_t frequencies[]={300000000,433889000,906400000,928000000};
    for(size_t slot=0;slot<kSlots;++slot) {
        size_t selected=99;
        CHECK(h.add(longest,kPulsesPerSlot,frequencies[slot],-90,complete,&selected)==AddResult::Added);
        CHECK(selected==slot&&h.size()==slot+1);
        CHECK(h.entry(slot)->durationUs==uint64_t(INT32_MAX)*kPulsesPerSlot);
        CHECK(h.entry(slot)->count==2048&&!memcmp(h.pulses(slot),longest,sizeof longest));
    }
    CHECK(bank[0]==0x12345678&&bank[kBankPulses+1]==0x12345678);
    int32_t original[kBankPulses];memcpy(original,bank+1,sizeof original);
    Entry metadata[kSlots];for(size_t i=0;i<kSlots;++i)metadata[i]=*h.entry(i);
    size_t selected=99;
    const int32_t fifth[]={100,-200,300,-400};
    CHECK(h.add(fifth,4,433920000,-40,complete,&selected)==AddResult::Full&&selected==99);
    CHECK(!memcmp(original,bank+1,sizeof original));
    for(size_t i=0;i<kSlots;++i)CHECK(h.entry(i)->hash==metadata[i].hash&&h.entry(i)->observations==1);
    // Repeats remain recognizable when all slots are occupied, and never
    // replace the payload/hash/duration with the jittered input.
    longest[123]=-INT32_MAX+1;
    CHECK(h.add(longest,kPulsesPerSlot,frequencies[2],-20,complete,&selected)==AddResult::Duplicate&&selected==2);
    CHECK(h.entry(2)->observations==2&&h.entry(2)->rssiDbm==-20);
    CHECK(h.entry(2)->durationUs==metadata[2].durationUs&&h.entry(2)->hash==metadata[2].hash);
    CHECK(!memcmp(original,bank+1,sizeof original));
    // The slot index remains stable across many repeats; weaker observations
    // cannot overwrite the recorded signal peak.
    for(unsigned i=0;i<500;++i) {
        CHECK(h.add(longest,kPulsesPerSlot,frequencies[2],-100,complete,&selected)==AddResult::Duplicate&&selected==2);
        CHECK(h.entry(2)->observations==i+3&&h.entry(2)->rssiDbm==-20&&h.size()==4);
    }
    h.clear();CHECK(h.size()==0&&h.bound());
    CHECK(h.add(fifth,4,433920000,-80,complete,&selected)==AddResult::Added&&selected==0);
    CHECK(!memcmp(h.pulses(0),fifth,sizeof fifth));
    return true;
}
int main() {
    if(!bind_and_metadata()||!candidate_validation()||!tolerance_boundaries()||!capacity_and_immutability())return 1;
    printf("capture_history: %u checks passed (external banks, 4 slots, clean RAW validation, bounded timing dedupe, immutable payloads)\n",checks);
    return 0;
}
