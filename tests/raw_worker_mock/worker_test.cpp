#include "lab_service.h"
#include "raw_capture.h"
#include "triggered_capture.h"
#include "raw_startup_gate.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <string>
using namespace research;
static unsigned checks;
#define CHECK(x) do{++checks;if(!(x)){std::fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x);std::exit(1);}}while(0)
#define portENTER_CRITICAL(x) ((void)(x))
#define portEXIT_CRITICAL(x) ((void)(x))
static int lock;
static LabSnapshot state;
static RadioSettings activeSettings;
static size_t captured;
static bool stopRequested;
static uint32_t clockUs;
static unsigned rssiSamples,failSample,stopSample,delayUs=1000;
static int16_t noise=-88;
static bool edgeDuringRssi,edgeAfterTime;
static std::string lastStatus;
struct Driver {bool ready=true;bool spiReady()const{return ready;}}ELECHOUSE_cc1101;
struct Pulse {int32_t duration;uint32_t ended;};
static std::deque<Pulse> pending;
static raw_capture::Snapshot raw;
static void queued(int32_t value,uint32_t ended){pending.push_back(Pulse{value,ended});raw.lastEdgeUs=ended;++raw.edges;}
namespace research { namespace raw_capture {
bool pop(int32_t& duration,uint32_t& ended){if(pending.empty())return false;duration=pending.front().duration;ended=pending.front().ended;pending.pop_front();return true;}
bool pop(int32_t& duration){uint32_t ended;return pop(duration,ended);}
Snapshot snapshot(){Snapshot result=raw;result.queued=uint16_t(pending.size());return result;}
}}
static void status(const char* text){lastStatus=text;}
static void errorCount(uint32_t LabSnapshot::*field){++(state.*field);}
static uint32_t micros(){
    const uint32_t result=clockUs;
    if(edgeAfterTime){edgeAfterTime=false;queued(7001,result+1);clockUs=result+1;}
    return result;
}
static void vTaskDelay(unsigned ticks){clockUs+=ticks*delayUs;}
static int16_t sampleRawRssi(){
    ++rssiSamples;++state.samples;state.rssiDbm=noise;
    if(failSample&&rssiSamples==failSample)ELECHOUSE_cc1101.ready=false;
    if(stopSample&&rssiSamples==stopSample)stopRequested=true;
    if(edgeDuringRssi){edgeDuringRssi=false;queued(6900,clockUs);}
    clockUs+=100;
    return noise;
}
// PRODUCTION_FUNCTIONS
static void reset(){
    state=LabSnapshot{};activeSettings=RadioSettings();activeSettings.rssiThresholdDbm=-90;
    captured=0;stopRequested=false;clockUs=0;rssiSamples=failSample=stopSample=0;
    delayUs=1000;noise=-88;edgeDuringRssi=edgeAfterTime=false;lastStatus.clear();ELECHOUSE_cc1101.ready=true;
    pending.clear();raw=raw_capture::Snapshot{};raw.running=true;
}
static void prepareTriggered(TriggeredCapture& trigger){
    // The initial interval is partial;20 actual intervals follow the gate.
    trigger.pulse(-100,100);trigger.rssi(-60,100);
    for(unsigned i=0;i<20;++i)trigger.pulse((i&1)?-300:300,400+i*300);
    trigger.rssi(-60,6100);
    CHECK(trigger.size()==20&&!trigger.complete());raw.lastEdgeUs=6100;
}
int main(){
    reset();state.rawThresholdAuto=false;CHECK(prepareRawThreshold());CHECK(!rssiSamples&&activeSettings.rssiThresholdDbm==-90);
    for(int16_t floor:{int16_t(-120),int16_t(-88),int16_t(-60),int16_t(-20)}){
        reset();state.rawThresholdAuto=true;noise=floor;CHECK(prepareRawThreshold());
        const int expected=floor+10<-100?-100:floor+10>-45?-45:floor+10;
        CHECK(state.rawThresholdReady&&state.rawThresholdSamples==64&&state.rawNoiseDbm==floor);
        CHECK(state.rawThresholdDbm==expected&&activeSettings.rssiThresholdDbm==expected);
        CHECK(rssiSamples==64&&clockUs>=252000&&clockUs<512000&&pending.empty());
    }
    for(unsigned i=1;i<=64;++i){reset();state.rawThresholdAuto=true;failSample=i;CHECK(!prepareRawThreshold());CHECK(!state.rawThresholdReady&&state.spiErrors==1);CHECK(activeSettings.rssiThresholdDbm==-90);}
    for(unsigned i=1;i<=64;++i){reset();state.rawThresholdAuto=true;stopSample=i;CHECK(!prepareRawThreshold());CHECK(!state.rawThresholdReady&&stopRequested);CHECK(activeSettings.rssiThresholdDbm==-90);}
    reset();state.rawThresholdAuto=true;delayUs=100000;CHECK(!prepareRawThreshold());CHECK(clockUs<700000&&!state.rawThresholdReady&&rssiSamples<64);
    reset();state.rawThresholdAuto=true;clockUs=UINT32_MAX-100000;CHECK(prepareRawThreshold());CHECK(state.rawThresholdReady&&state.rawThresholdSamples==64);

    int32_t output[128]={};uint32_t lastSample=0;
    // A real ISR edge ending during the RSSI transaction must be drained before
    // rssi() can poll an older edge and falsely complete at a7ms boundary.
    reset();TriggeredCapture duringSpi(output,128,-75,0);prepareTriggered(duringSpi);
    clockUs=13000;noise=-60;edgeDuringRssi=true;
    CHECK(rawCaptureTick(duringSpi,lastSample,13));CHECK(!duringSpi.complete()&&duringSpi.size()==21&&pending.empty());
    CHECK(state.pulseCount==21&&!state.captureComplete);
    CHECK(state.captureQualifiedPulses==21&&state.captureRssiQualified);
    clockUs=20000;CHECK(!rawCaptureTick(duringSpi,lastSample,20));CHECK(duringSpi.complete()&&state.captureComplete);

    // An ISR between the second drain and atomic snapshot leaves queued data;
    // defer quiet polling to the next tick, even when the old edge was7ms ago.
    reset();TriggeredCapture afterClock(output,128,-75,0);prepareTriggered(afterClock);
    clockUs=13100;noise=-60;edgeAfterTime=true;lastSample=13;
    CHECK(rawCaptureTick(afterClock,lastSample,13));CHECK(!afterClock.complete()&&pending.size()==1);
    clockUs=13200;CHECK(rawCaptureTick(afterClock,lastSample,14));CHECK(pending.empty()&&!afterClock.complete()&&afterClock.size()==21);

    // Gaps in accepted durations do not move the absolute ISR edge into past
    // or future; high-rate prior noise and timer wrap stay bounded.
    reset();TriggeredCapture skipped(output,128,-75,0);prepareTriggered(skipped);
    queued(300,20000);clockUs=20100;noise=-60;lastSample=20;
    CHECK(rawCaptureTick(skipped,lastSample,20));CHECK(!skipped.complete()&&pending.empty());
    reset();TriggeredCapture failed(output,128,-75,0);prepareTriggered(failed);
    failSample=1;clockUs=6200;lastSample=0;CHECK(!rawCaptureTick(failed,lastSample,7));CHECK(!failed.complete()&&state.spiErrors==1);
    reset();TriggeredCapture losses(output,128,-75,0);prepareTriggered(losses);
    raw.rejected=1;clockUs=7000;lastSample=7;CHECK(!rawCaptureTick(losses,lastSample,7));CHECK(!losses.complete()&&state.rejected);

    // Exercise the whole worker step with64 weak prehistory intervals, one
    // strong RSSI observation, and64 more noise intervals. Neither prehistory
    // nor a lone RSSI spike may manufacture a completed/savable signal.
    reset();int32_t noiseOutput[256]={};TriggeredCapture spike(noiseOutput,256,-75,0);
    queued(-100,100);
    for(unsigned i=0;i<64;++i)queued((i&1)?-50:50,150+i*50);
    clockUs=3400;noise=-88;lastSample=0;CHECK(rawCaptureTick(spike,lastSample,3));
    clockUs=4000;noise=-60;CHECK(rawCaptureTick(spike,lastSample,4));
    CHECK(state.pulseCount==64&&state.captureQualifiedPulses==0&&!state.captureRssiQualified&&!state.captureComplete);
    for(unsigned i=0;i<64;++i)queued((i&1)?-50:50,4100+i*50);
    clockUs=7400;noise=-88;CHECK(rawCaptureTick(spike,lastSample,7));
    CHECK(state.captureQualifiedPulses==64&&!state.captureRssiQualified&&!state.captureComplete);
    clockUs=14500;CHECK(rawCaptureTick(spike,lastSample,14));
    CHECK(!spike.complete()&&!spike.triggered()&&state.captureRearms==1&&state.captureShortDiscarded==128);
    CHECK(!state.pulseCount&&!state.captureQualifiedPulses&&!state.captureComplete);

    reset();TriggeredCapture contextOnly(noiseOutput,256,-75,0);
    queued(-100,100);for(unsigned i=0;i<64;++i)queued((i&1)?-50:50,150+i*50);
    clockUs=4000;noise=-60;lastSample=0;CHECK(rawCaptureTick(contextOnly,lastSample,4));
    CHECK(state.pulseCount==64&&state.captureQualifiedPulses==0);
    contextOnly.stop(4200);CHECK(!contextOnly.complete());

    std::printf("RAW worker: %u checks passed (real startup/drain/tick/publish, SPI-overlap IRQ and policy doubles)\n",checks);
}
