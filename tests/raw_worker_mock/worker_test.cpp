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
static RawTraceEntry traceStorage[4096];
static RawTraceEntry* rawTraceBank;
static const size_t kRawTraceCapacity=4096;
static RawTrace rawTrace;
static capture_history::History rawHistory;
static int32_t historyStorage[capture_history::kBankPulses],captureStorage[kWaveformCapacity];
static int32_t* rawHistoryBank;
static int32_t* capture=captureStorage;
static RegisterImage activeImage,rawHistoryImages[capture_history::kSlots];
static RadioSettings rawHistorySettings[capture_history::kSlots];
static uint32_t rawHistoryDuplicates,rawHistoryDiscarded;
static bool activeRawHistory,historyCandidateStored;
static void* workerHandle;
static unsigned traceAllocations,traceFrees;
static bool failTraceAllocation;
static const int MALLOC_CAP_INTERNAL=1,MALLOC_CAP_8BIT=2;
static void* heap_caps_malloc(size_t bytes,int caps) {
    ++traceAllocations;CHECK(bytes==sizeof(traceStorage));CHECK(caps==(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT));
    return failTraceAllocation?nullptr:traceStorage;
}
static unsigned historyFrees;
static void heap_caps_free(void* pointer){CHECK(pointer==traceStorage||pointer==historyStorage);if(pointer==traceStorage)++traceFrees;else ++historyFrees;}
static RadioSettings activeSettings;
static size_t captured;
static bool stopRequested;
static uint32_t clockUs;
static unsigned rssiSamples,failSample,stopSample,delayUs=1000;
static int16_t noise=-88;
static bool edgeDuringRssi,edgeAfterTime;
static const unsigned CC1101_GDO0=4;
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
void stop(){raw.running=false;}
bool start(uint8_t pin){CHECK(pin==CC1101_GDO0);pending.clear();raw=Snapshot{};raw.running=true;raw.startedUs=raw.lastEdgeUs=clockUs;return true;}
}}
static void status(const char* text){lastStatus=text;}
static void errorCount(uint32_t LabSnapshot::*field){++(state.*field);}
static uint32_t micros(){
    const uint32_t result=clockUs;
    if(edgeAfterTime){edgeAfterTime=false;queued(7001,result+1);clockUs=result+1;}
    return result;
}
static void vTaskDelay(unsigned ticks){clockUs+=ticks*delayUs;}
static uint32_t millis(){return clockUs/1000;}
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
    rawTraceBank=traceStorage;rawTrace.bind(traceStorage,kRawTraceCapacity);rawTrace.enable(true);
    workerHandle=nullptr;traceAllocations=traceFrees=0;failTraceAllocation=false;
    rawHistory.unbind();rawHistoryBank=nullptr;rawHistoryDuplicates=rawHistoryDiscarded=historyFrees=0;
    activeRawHistory=historyCandidateStored=false;activeImage=RegisterImage();
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

    // Both the actual initial factory and actual restart preserve multi-frame
    // history bursts across a Princeton guard; advanced remains7ms.
    for(bool history:{false,true})for(bool restart:{false,true}) {
        reset();activeRawHistory=history;
        TriggeredCapture configured=makeRawCandidate(0);
        if(restart)CHECK(restartRawCandidate(configured));
        prepareTriggered(configured);configured.poll(13100);
        CHECK(configured.complete()==!history);
        if(history) {
            configured.pulse(11700,17800);configured.pulse(-300,18100);
            configured.rssi(-60,18100);configured.poll(25100);
            CHECK(!configured.complete()&&configured.size()==22);
            configured.poll(48100);CHECK(configured.complete());
        }
    }

    int32_t output[128]={};uint32_t lastSample=0;
    // A real ISR edge ending during the RSSI transaction must be drained before
    // rssi() can poll an older edge and falsely complete at a7ms boundary.
    reset();TriggeredCapture duringSpi(output,128,-75,0);prepareTriggered(duringSpi);
    clockUs=13000;noise=-60;edgeDuringRssi=true;
    CHECK(rawCaptureTick(duringSpi,lastSample,13));CHECK(!duringSpi.complete()&&duringSpi.size()==21&&pending.empty());
    CHECK(state.pulseCount==21&&!state.captureComplete);
    CHECK(state.captureQualifiedPulses==21&&state.captureRssiQualified);
    clockUs=20000;CHECK(!rawCaptureTick(duringSpi,lastSample,20));CHECK(duringSpi.complete()&&state.captureComplete);
    CHECK(labRawTraceInfo().frozen&&labRawTraceInfo().started&&labRawTraceInfo().count>0);
    RawTraceEntry entry{};CHECK(labRawTraceCopy(labRawTraceInfo().count-1,entry));
    CHECK(entry.phase==unsigned(TriggeredCapture::Phase::Complete)&&entry.rssiQualified&&entry.post==21);

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

    // Debug allocation is optional/idle-only. Disable preserves immutable
    // evidence; explicit release returns49KiB before history/Lua operations.
    reset();CHECK(labRawTraceRelease()&&traceFrees==1);CHECK(!rawTraceBank);
    failTraceAllocation=true;CHECK(!labRawTraceEnable(true));CHECK(!labRawTraceInfo().enabled);
    failTraceAllocation=false;CHECK(labRawTraceEnable(true)&&traceAllocations==2);
    workerHandle=traceStorage;CHECK(!labRawTraceEnable(false)&&!labRawTraceRelease());workerHandle=nullptr;
    CHECK(labRawTraceInfo().enabled&&!labRawTraceInfo().frozen);
    for(unsigned i=0;i<5001;++i)rawTrace.record(1,i,int32_t(i),1,2048,2048,true,i>500);
    CHECK(labRawTraceInfo().count==4096&&labRawTraceInfo().dropped==905&&labRawTraceInfo().started);
    CHECK(!labRawTraceCopy(0,entry));CHECK(labRawTraceEnable(false));
    CHECK(labRawTraceCopy(0,entry)&&entry.value==905&&entry.atUs==905&&entry.used==2048&&entry.post==2048);
    CHECK(labRawTraceCopy(4095,entry)&&entry.value==5000);CHECK(!labRawTraceCopy(4096,entry));
    rawTrace.record(1,6000,6000,1,1,1,false,true);CHECK(labRawTraceCopy(4095,entry)&&entry.value==5000);
    CHECK(labRawTraceEnable(true));CHECK(labRawTraceInfo().count==0&&!labRawTraceInfo().frozen);
    CHECK(labRawTraceRelease()&&traceFrees==2&&!labRawTraceCopy(0,entry));

    // Execute the production history retention and stopped-copy functions,
    // not merely the separately tested portable deduplication model.
    reset();CHECK(labRawTraceRelease());rawHistoryBank=historyStorage;
    CHECK(rawHistory.bind(historyStorage,capture_history::kBankPulses));activeRawHistory=true;
    activeImage.set(0x0B,0x55);activeSettings.frequencyHz=433920000;state.peakDbm=-26;
    TriggeredCapture retained(capture,kWaveformCapacity,-75,0);prepareTriggered(retained);retained.poll(13100);CHECK(retained.complete());
    CHECK(!retainRawCandidate(retained,0,false));CHECK(state.captureHistoryCount==1&&historyCandidateStored);
    int32_t selected[2048]={};RegisterImage selectedImage;RadioSettings selectedSettings;
    workerHandle=historyStorage;CHECK(!labCopyCaptureHistory(0,selected,2048,selectedImage,selectedSettings));CHECK(!labClearCaptureHistory());workerHandle=nullptr;
    CHECK(!labCopyCaptureHistory(0,selected,19,selectedImage,selectedSettings));CHECK(!labCopyCaptureHistory(4,selected,2048,selectedImage,selectedSettings));
    CHECK(labCopyCaptureHistory(0,selected,2048,selectedImage,selectedSettings)==20);
    CHECK(selected[0]==300&&selectedImage.value[0x0B]==0x55&&selectedSettings.frequencyHz==433920000);
    capture[0]=999;CHECK(selected[0]==300&&rawHistory.pulses(0)[0]==300);
    TriggeredCapture repeated(capture,kWaveformCapacity,-75,0);prepareTriggered(repeated);repeated.poll(13100);activeImage.set(0x0B,0x66);state.peakDbm=-20;
    CHECK(!retainRawCandidate(repeated,0,false));CHECK(state.captureHistoryCount==1&&state.captureHistoryDuplicates==1);
    CHECK(state.captureHistory[0].observations==2&&state.captureHistory[0].rssiDbm==-20);
    CHECK(labCopyCaptureHistory(0,selected,2048,selectedImage,selectedSettings)==20&&selectedImage.value[0x0B]==0x55);
    // Pause after20 qualified ended intervals while RF remains strong: this
    // is only a prefix of a24-bit frame, never a second completed history slot.
    TriggeredCapture prefix(capture,kWaveformCapacity,-75,0);prepareTriggered(prefix);
    historyCandidateStored=false;clockUs=6200;
    CHECK(prefix.phase()==TriggeredCapture::Phase::Capturing&&prefix.postTriggerPulses()==20&&prefix.rssiQualified());
    stopRawCandidate(prefix,raw_capture::snapshot());
    CHECK(!prefix.complete()&&!state.captureComplete&&state.captureHistoryCount==1&&state.captureHistoryDiscarded==1);
    CHECK(rawHistory.pulses(0)[0]==300&&rawHistory.entry(0)->observations==2);
    CHECK(labCopyCaptureHistory(0,selected,2048,selectedImage,selectedSettings)==20&&selectedImage.value[0x0B]==0x55);
    // The existing advanced single/manual workflow retains its deliberate
    // qualified-prefix semantics, without adding anything to history.
    TriggeredCapture manual(capture,kWaveformCapacity,-75,0);prepareTriggered(manual);
    activeRawHistory=false;stopRawCandidate(manual,raw_capture::snapshot());
    CHECK(manual.complete()&&state.captureComplete&&state.captureHistoryCount==1&&state.captureHistoryDiscarded==1);
    activeRawHistory=true;
    TriggeredCapture corrupt(capture,kWaveformCapacity,-75,0);prepareTriggered(corrupt);corrupt.pulse(0,6200);
    CHECK(!retainRawCandidate(corrupt,0,false));CHECK(state.captureHistoryCount==1&&state.captureHistoryDiscarded==2);
    CHECK(rawHistory.pulses(0)[0]==300&&rawHistory.entry(0)->observations==2);
    for(unsigned slot=1;slot<4;++slot){
        TriggeredCapture next(capture,kWaveformCapacity,-75,0);prepareTriggered(next);next.poll(13100);
        activeSettings.frequencyHz=433920000+slot;activeImage.set(0x0B,uint8_t(0x60+slot));
        CHECK(!retainRawCandidate(next,0,false));CHECK(state.captureHistoryCount==slot+1);
    }
    CHECK(state.captureHistoryFull&&rawHistory.size()==4);
    CHECK(labCopyCaptureHistory(3,selected,2048,selectedImage,selectedSettings)==20&&selectedSettings.frequencyHz==433920003&&selectedImage.value[0x0B]==0x63);
    CHECK(!labRawTraceEnable(true));CHECK(labClearCaptureHistory()&&historyFrees==1&&!rawHistoryBank&&!state.captureHistoryCount);
    CHECK(selected[0]==300&&selectedSettings.frequencyHz==433920003&&!labCopyCaptureHistory(0,selected,2048,selectedImage,selectedSettings));
    for(int16_t floor:{int16_t(-88),int16_t(-26)}){
        reset();noise=floor;activeSettings.rssiThresholdDbm=-75;
        CHECK(waitRawHistoryBoundary(0));CHECK(clockUs>=7000&&clockUs<20000);
    }
    reset();activeSettings.rxTimeoutMs=5;CHECK(!waitRawHistoryBoundary(0));CHECK(clockUs<10000);
    reset();stopSample=2;CHECK(!waitRawHistoryBoundary(0)&&stopRequested);
    reset();failSample=2;CHECK(!waitRawHistoryBoundary(0)&&!ELECHOUSE_cc1101.spiReady());

    std::printf("RAW worker: %u checks passed (real startup/drain/tick/publish, SPI-overlap IRQ and policy doubles)\n",checks);
}
