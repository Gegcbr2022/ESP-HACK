#include "lab_service.h"
#include "raw_capture.h"
#include "triggered_capture.h"
#include "packet_metadata.h"
#include "radio_calibration.h"
#include "trim_math.h"
#include "chat_channel.h"
#include "raw_radio_restore.h"
#include "raw_startup_gate.h"
#include "../CONFIG.h"
#include "../../shared/chat_radio.h"
#include "../../shared/chat_session.h"
#include "../../shared/flipper_chat_compat.h"
#include <Arduino.h>
#include <ELECHOUSE_CC1101_SRC_DRV.h>
#include <driver/rmt.h>
#include <soc/gpio_struct.h>
#include <soc/gpio_periph.h>
#include <soc/soc.h>
#include <esp_system.h>
#include <esp_heap_caps.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/event_groups.h>
#include <string.h>
#include <stdio.h>

namespace research {
namespace {
portMUX_TYPE lock = portMUX_INITIALIZER_UNLOCKED;
LabSnapshot state = {};
RegisterImage activeImage;
RadioSettings activeSettings;
TxProfile activeProfile = TxProfile::ReceiveOnly;
uint8_t activePower;
uint32_t stepHz;
uint32_t rxArmedAt;
uint32_t syncStarted;
bool syncSeen;
volatile bool stopRequested;
TaskHandle_t workerHandle;
StaticEventGroup_t eventStorage;
EventGroupHandle_t events;
static const EventBits_t kDone = 1;
struct ChatCommand { uint16_t destination; bool discover; char text[161]; };
StaticQueue_t commandQueueStorage;
uint8_t commandQueueBytes[3 * sizeof(ChatCommand)];
QueueHandle_t commands;
struct IncomingMessage { uint16_t source; char text[161]; };
StaticQueue_t inboxStorage;
uint8_t inboxBytes[8 * sizeof(IncomingMessage)];
QueueHandle_t inbox;
// Lazily allocate two fixed banks once, avoiding ESP32's smaller static DRAM
// segment and repeated allocation/free cycles. Neither bank needs PSRAM.
int32_t* capture;
size_t captured;
ChatSession chat;
FlipperChatRx plaintextRx;
FlipperChatTx plaintextTx;
// At most 2048 pulses plus splits; 4096 half-items cover all <=5s waveforms.
rmt_item32_t* rmtItems;
RawTraceEntry* rawTraceBank;
RawTrace rawTrace;
static const size_t kRawTraceCapacity=4096;
capture_history::History rawHistory;
int32_t* rawHistoryBank;
RegisterImage rawHistoryImages[capture_history::kSlots];
RadioSettings rawHistorySettings[capture_history::kSlots];
uint32_t rawHistoryDuplicates,rawHistoryDiscarded;
bool activeRawHistory;
bool historyCandidateStored;

void traceRaw(uint8_t kind,uint32_t atUs,int32_t value,const TriggeredCapture& trigger) {
    portENTER_CRITICAL(&lock);
    rawTrace.record(kind,atUs,value,uint8_t(trigger.phase()),trigger.size(),trigger.postTriggerPulses(),
                    trigger.rssiQualified(),trigger.triggered());
    if(trigger.terminal())rawTrace.freeze();
    portEXIT_CRITICAL(&lock);
}
void freezeRawTrace() { portENTER_CRITICAL(&lock);rawTrace.freeze();portEXIT_CRITICAL(&lock); }

void status(const char* text);
void errorCount(uint32_t LabSnapshot::*field);
uint8_t stableStatus(uint8_t address);

struct RawRestoreBackend {
    struct PinState { uint32_t mux, pin, outputSelect; bool enabled, high; };
    bool installed=false;
    bool failurePrinted=false;
    uint8_t lastMarc=0;
    esp_err_t stopResult=ESP_OK,uninstallResult=ESP_OK;
    static_assert(CC1101_GDO0<32,"RAW GPIO snapshot targets the original GPIO bank");
    bool ready() const { return ELECHOUSE_cc1101.spiReady(); }
    bool readState(uint8_t& value) {
        const uint32_t before=ELECHOUSE_cc1101.spiTimeouts();
        value=stableStatus(0x35)&0x1F;
        lastMarc=value;
        return before!=UINT32_MAX && ELECHOUSE_cc1101.spiTimeouts()==before;
    }
    bool readReg(uint8_t address,uint8_t& value) {
        const uint32_t before=ELECHOUSE_cc1101.spiTimeouts();
        value=ELECHOUSE_cc1101.SpiReadReg(address);
        const bool ok=before!=UINT32_MAX && ELECHOUSE_cc1101.spiTimeouts()==before;
        if(ok) { portENTER_CRITICAL(&lock); state.registers[address]=value; portEXIT_CRITICAL(&lock); }
        return ok;
    }
    bool writeReg(uint8_t address,uint8_t value) {
        const uint32_t before=ELECHOUSE_cc1101.spiTimeouts();
        ELECHOUSE_cc1101.SpiWriteReg(address,value);
        return before!=UINT32_MAX && ELECHOUSE_cc1101.spiTimeouts()==before;
    }
    bool readPa(uint8_t* values) {
        const uint32_t before=ELECHOUSE_cc1101.spiTimeouts();
        ELECHOUSE_cc1101.SpiReadBurstReg(0x3E,values,8);
        return before!=UINT32_MAX && ELECHOUSE_cc1101.spiTimeouts()==before;
    }
    bool writePa(const uint8_t* values) {
        uint8_t copy[8];memcpy(copy,values,sizeof(copy));
        const uint32_t before=ELECHOUSE_cc1101.spiTimeouts();
        ELECHOUSE_cc1101.SpiWriteBurstReg(0x3E,copy,8);
        return before!=UINT32_MAX && ELECHOUSE_cc1101.spiTimeouts()==before;
    }
    bool wait(uint8_t wanted) {
        for(unsigned i=0;i<40;++i) {
            uint8_t current;
            if(!readState(current)) return false;
            if(current==wanted) return true;
            delay(1);
        }
        return false;
    }
    bool idle() {
        const uint32_t before=ELECHOUSE_cc1101.spiTimeouts();
        ELECHOUSE_cc1101.SpiStrobe(0x36);
        return before!=UINT32_MAX && ELECHOUSE_cc1101.spiTimeouts()==before && wait(1);
    }
    bool enterRx() {
        const uint32_t before=ELECHOUSE_cc1101.spiTimeouts();
        ELECHOUSE_cc1101.SpiStrobe(0x3A);
        ELECHOUSE_cc1101.SpiStrobe(0x34);
        return before!=UINT32_MAX && ELECHOUSE_cc1101.spiTimeouts()==before && wait(13);
    }
    void savePin(PinState& value) {
        value.mux=REG_READ(GPIO_PIN_MUX_REG[CC1101_GDO0]);
        value.pin=GPIO.pin[CC1101_GDO0].val;
        value.outputSelect=GPIO.func_out_sel_cfg[CC1101_GDO0].val;
        value.enabled=(GPIO.enable&(1UL<<CC1101_GDO0))!=0;
        value.high=(GPIO.out&(1UL<<CC1101_GDO0))!=0;
    }
    void safePin() { pinMode(CC1101_GDO0,INPUT); }
    void restorePin(const PinState& value) {
        const uint32_t mask=1UL<<CC1101_GDO0;
        GPIO.enable_w1tc=mask;
        if(value.high)GPIO.out_w1ts=mask;else GPIO.out_w1tc=mask;
        GPIO.pin[CC1101_GDO0].val=value.pin;
        GPIO.func_out_sel_cfg[CC1101_GDO0].val=value.outputSelect;
        REG_WRITE(GPIO_PIN_MUX_REG[CC1101_GDO0],value.mux);
        if(value.enabled)GPIO.enable_w1ts=mask;
    }
    bool stopRmt() {
        if(!installed)return true;
        stopResult=rmt_tx_stop(RMT_CHANNEL_0);
        uninstallResult=rmt_driver_uninstall(RMT_CHANNEL_0);
        installed=false;
        return stopResult==ESP_OK&&uninstallResult==ESP_OK;
    }
    void failure(const char* phase,uint8_t address,uint8_t expected,uint8_t actual) {
        if(failurePrinted)return;
        failurePrinted=true;
        Serial.printf("[RAWRestore] FAIL phase=%s addr=%02X expected=%02X actual=%02X marc=%02X rmtStop=%ld rmtFree=%ld spiReady=%u miso=%lu\n",
            phase,address,expected,actual,lastMarc,(long)stopResult,(long)uninstallResult,
            ELECHOUSE_cc1101.spiReady(),(unsigned long)ELECHOUSE_cc1101.spiTimeouts());
    }
    void restored(bool ok) {
        if(!ok) { errorCount(&LabSnapshot::spiErrors); status("Radio restore failed; RX stopped"); }
    }
};

struct RawSoftwareRestore {
    const RadioSettings settings=activeSettings;
    const bool stopping=stopRequested, seen=syncSeen;
    const uint32_t armed=rxArmedAt, started=syncStarted;
    ~RawSoftwareRestore() {
        activeSettings=settings;stopRequested=stopping;
        rxArmedAt=armed;syncStarted=started;syncSeen=seen;
    }
};

void status(const char* text) {
    portENTER_CRITICAL(&lock);
    snprintf(state.status, sizeof(state.status), "%s", text);
    portEXIT_CRITICAL(&lock);
}
void errorCount(uint32_t LabSnapshot::*field) {
    portENTER_CRITICAL(&lock); ++(state.*field); portEXIT_CRITICAL(&lock);
}
uint8_t stableStatus(uint8_t address) {
    // TI errata: changing status registers can be sampled during an update.
    uint8_t previous = ELECHOUSE_cc1101.SpiReadStatus(address);
    for(uint8_t retry = 0; retry < 5; ++retry) {
        const uint8_t current = ELECHOUSE_cc1101.SpiReadStatus(address);
        if(current == previous) return current;
        previous = current;
    }
    errorCount(&LabSnapshot::spiErrors);
    return previous;
}
uint8_t marc() { return stableStatus(0x35) & 0x1F; }
bool waitState(uint8_t wanted, uint32_t timeoutMs = 40) {
    uint32_t start = millis();
    do {
        if(!ELECHOUSE_cc1101.spiReady())break;
        if(marc() == wanted) return true;
        if(stopRequested) break;
        vTaskDelay(1);
    } while(millis() - start < timeoutMs);
    errorCount(&LabSnapshot::timeouts);
    status("Radio state timeout");
    return false;
}
bool idle() { ELECHOUSE_cc1101.SpiStrobe(0x36); return waitState(1); }
bool frequency(uint32_t hz) {
    uint32_t programmedHz;
    if(!trimmedFrequencyHz(hz, radioCalibrationPpm(), programmedHz)) {
        status("Calibrated frequency outside hardware bands");
        return false;
    }
    const uint32_t word = cc1101FrequencyWord(programmedHz);
    ELECHOUSE_cc1101.SpiWriteReg(0x0D, word >> 16);
    ELECHOUSE_cc1101.SpiWriteReg(0x0E, word >> 8);
    ELECHOUSE_cc1101.SpiWriteReg(0x0F, word);
    if(!ELECHOUSE_cc1101.spiReady()) {
        errorCount(&LabSnapshot::spiErrors);
        status("Frequency SPI write failed");
        return false;
    }
    return true;
}
bool receive(uint32_t hz, bool calibrate = true) {
    if(!idle()) return false;
    if(!frequency(hz)) return false;
    if(calibrate) {
        ELECHOUSE_cc1101.SpiStrobe(0x33);
        if(!waitState(1)) return false;
    }
    ELECHOUSE_cc1101.SpiStrobe(0x3A); // SFRX is valid only in IDLE/overflow.
    pinMode(CC1101_GDO0, INPUT);
    ELECHOUSE_cc1101.SpiStrobe(0x34);
    const bool ready = waitState(13);rxArmedAt=millis();syncSeen=false;
    if(ready)errorCount(&LabSnapshot::rxRearms);
    return ready;
}
void powerTable(uint32_t hz, uint8_t index, bool ook) {
    uint8_t pa[8] = {};
    pa[ook ? 1 : 0] = chat_radio_pa(hz, index);
    ELECHOUSE_cc1101.SpiWriteBurstReg(0x3E, pa, 8);
    const uint8_t frend = ELECHOUSE_cc1101.SpiReadReg(0x22);
    ELECHOUSE_cc1101.SpiWriteReg(0x22, (frend & 0xF8) | (ook ? 1 : 0));
}
bool configure(const RegisterImage& image, uint32_t hz) {
    // Reset establishes all unspecified config registers, preventing stale presets.
    if(!idle()) return false;
    ELECHOUSE_cc1101.Init();
    // Init also writes library defaults. Sparse Flipper presets require silicon
    // reset defaults, so issue a second SRES after SPI initialization is complete.
    ELECHOUSE_cc1101.SpiStrobe(0x30);
    delay(2);
    pinMode(CC1101_GDO0, INPUT);
    uint8_t part = ELECHOUSE_cc1101.SpiReadStatus(0x30);
    uint8_t version = ELECHOUSE_cc1101.SpiReadStatus(0x31);
    portENTER_CRITICAL(&lock);
    state.part = part; state.version = version;
    state.connected = part == 0 && version != 0 && version != 0xFF;
    portEXIT_CRITICAL(&lock);
    if(!state.connected) { errorCount(&LabSnapshot::spiErrors); status("CC1101 not detected"); return false; }
    if(!idle()) return false;
    for(size_t i = 0; i < kRegisterCount; ++i) {
        if(image.present[i]) ELECHOUSE_cc1101.SpiWriteReg(i, image.value[i]);
    }
    // RX paths cannot accidentally inherit the high-power table in an imported preset.
    uint8_t zeros[8] = {};
    ELECHOUSE_cc1101.SpiWriteBurstReg(0x3E, zeros, 8);
    if(!frequency(hz)) return false;
    bool ok = true;
    for(size_t i = 0; i < kRegisterCount; ++i) {
        uint8_t reg = ELECHOUSE_cc1101.SpiReadReg(i);
        portENTER_CRITICAL(&lock); state.registers[i] = reg; portEXIT_CRITICAL(&lock);
        if(image.present[i] && i != 0x0D && i != 0x0E && i != 0x0F && reg != image.value[i]) ok = false;
    }
    if(!ok) { errorCount(&LabSnapshot::spiErrors); status("Register readback mismatch"); return false; }
    if(!receive(hz)) return false;
    // SCAL and RX entry update calibration results. Cache these only after the
    // first successful RX entry so diagnostics do not show pre-calibration data.
    static const uint8_t calibratedAddresses[] = {0x23, 0x24, 0x25, 0x26, 0x2C, 0x2D, 0x2E};
    uint8_t calibratedValues[sizeof(calibratedAddresses)];
    for(size_t i = 0; i < sizeof(calibratedAddresses); ++i) {
        calibratedValues[i] = ELECHOUSE_cc1101.SpiReadReg(calibratedAddresses[i]);
        if(!ELECHOUSE_cc1101.spiReady()) {
            errorCount(&LabSnapshot::spiErrors);
            status("Calibration diagnostic SPI read failed");
            return false;
        }
    }
    portENTER_CRITICAL(&lock);
    for(size_t i = 0; i < sizeof(calibratedAddresses); ++i)
        state.registers[calibratedAddresses[i]] = calibratedValues[i];
    state.misoTimeouts = ELECHOUSE_cc1101.spiTimeouts();
    portEXIT_CRITICAL(&lock);
    return true;
}
void sampleRadio() {
    uint8_t bytes = stableStatus(0x3B);
    uint8_t m = marc(), p = ELECHOUSE_cc1101.SpiReadStatus(0x38);
    uint8_t raw = ELECHOUSE_cc1101.SpiReadStatus(0x34);
    // Datasheet: signed RSSI / 2 - RSSI_offset. 74dB is nominal at 433/868MHz.
    int16_t rssi = int16_t(int8_t(raw)) / 2 - 74;
    portENTER_CRITICAL(&lock);
    state.marcState = m; state.rxBytes = bytes; state.pktStatus = p;
    state.misoTimeouts = ELECHOUSE_cc1101.spiTimeouts();
    state.rssiDbm = rssi; state.carrier = p & 0x40; ++state.samples;
    if(rssi > state.peakDbm) state.peakDbm = rssi;
    portEXIT_CRITICAL(&lock);
    if((bytes & 0x80) || m == 17) {
        errorCount(&LabSnapshot::fifoErrors);
        receive(activeSettings.frequencyHz, false);
    }
}
void refreshDiagnosticRegisters() {
    uint8_t registers[kRegisterCount];
    for(size_t i = 0; i < kRegisterCount; ++i) {
        if(stopRequested) return;
        registers[i] = ELECHOUSE_cc1101.SpiReadReg(i);
        if(!ELECHOUSE_cc1101.spiReady()) {
            errorCount(&LabSnapshot::spiErrors); status("Diagnostic SPI read failed"); return;
        }
    }
    portENTER_CRITICAL(&lock);
    memcpy(state.registers, registers, sizeof(registers));
    state.misoTimeouts = ELECHOUSE_cc1101.spiTimeouts();
    portEXIT_CRITICAL(&lock);
}
int16_t sampleRawRssi() {
    // One short SPI register transaction per nominal1ms worker tick. The GPIO
    // ISR continues recording every edge and never performs SPI/RSSI gating.
    const uint8_t raw = ELECHOUSE_cc1101.SpiReadStatus(0x34);
    const int16_t rssi = int16_t(int8_t(raw)) / 2 - 74;
    portENTER_CRITICAL(&lock);
    state.rssiDbm = rssi; state.carrier = rssi >= activeSettings.rssiThresholdDbm;
    state.frequencyHz = activeSettings.frequencyHz;
    state.misoTimeouts = ELECHOUSE_cc1101.spiTimeouts(); ++state.samples;
    if(rssi > state.peakDbm) state.peakDbm = rssi;
    portEXIT_CRITICAL(&lock);
    return rssi;
}
void publishRaw(const TriggeredCapture& trigger, const raw_capture::Snapshot& counters) {
    portENTER_CRITICAL(&lock);
    captured = trigger.size(); state.pulseCount = captured;
    state.captureFull = trigger.full(); state.captureTriggered = trigger.triggered();
    state.captureComplete = trigger.complete(); state.captureTailDiscarded = trigger.discardedTail();
    state.edgeCount = counters.edges; state.captureHighWater = counters.highWater;
    state.dropped = trigger.dropped(); state.rejected = trigger.rejected();
    state.captureRearms=trigger.rearmCount();state.captureShortDiscarded=trigger.discardedShort();
    state.captureArmingDropped=trigger.discardedArmingDropped();state.captureArmingRejected=trigger.discardedArmingRejected();
    state.captureQualifiedPulses=trigger.postTriggerPulses();state.captureRssiQualified=trigger.rssiQualified();
    state.lastRejectedCause=counters.lastRejectedCause;state.lastRejectedUs=counters.lastRejectedUs;
    portEXIT_CRITICAL(&lock);
}
void drainRaw(TriggeredCapture& trigger, bool finishing = false) {
    int32_t pulse;uint32_t endedAtUs;
    for(size_t drained = 0; drained < raw_capture::kCapacity &&
        (finishing || !stopRequested) && raw_capture::pop(pulse,endedAtUs); ++drained) {
        trigger.pulse(pulse, endedAtUs);
        traceRaw(1,endedAtUs,pulse,trigger);
    }
}
bool rawCaptureTick(TriggeredCapture& trigger,uint32_t& lastSample,uint32_t nowMs) {
    drainRaw(trigger);
    const auto before=trigger.phase();const uint32_t beforeRearm=trigger.rearmCount();
    bool sampled=false;int16_t rssi=-127;uint32_t sampledAt=0;
    if(nowMs-lastSample>=1) {
        rssi=sampleRawRssi();sampledAt=micros();lastSample=nowMs;sampled=true;
        if(!ELECHOUSE_cc1101.spiReady()) {
            trigger.pulse(0,micros());errorCount(&LabSnapshot::spiErrors);
            traceRaw(3,micros(),-1,trigger);freezeRawTrace();
            publishRaw(trigger,raw_capture::snapshot());status("SPI fault during RAW");return false;
        }
    }
    // RSSI SPI can overlap an IRQ. Feed its actual ending timestamp before
    // allowing the trigger policy to decide whether a quiet gap has elapsed.
    drainRaw(trigger);
    // Time must precede the atomic snapshot. Queued/future-ended intervals
    // defer this decision; an IRQ after the snapshot is later than decisionUs.
    const uint32_t decisionUs=micros();
    const auto counters=raw_capture::snapshot();
    trigger.integrity(counters.dropped,counters.rejected);
    if(!counters.queued && int32_t(decisionUs-counters.lastEdgeUs)>=0) {
        const bool wasTriggered=trigger.triggered();
        if(sampled)trigger.rssi(rssi,decisionUs);
        if(!wasTriggered&&trigger.triggered())status("Triggered: capturing one burst");
        trigger.poll(decisionUs);
    }
    if(sampled)traceRaw(2,sampledAt,rssi,trigger);
    if(before!=trigger.phase()||beforeRearm!=trigger.rearmCount())
        traceRaw(3,decisionUs,int32_t(trigger.rearmCount()),trigger);
    publishRaw(trigger,counters);
    if(!trigger.terminal())return true;
    if(trigger.full())status("RAW full: capture not saved");
    else if(trigger.complete())status("One burst ready: OK to save");
    else status("RAW damaged: capture not saved");
    freezeRawTrace();
    return false;
}
bool prepareRawThreshold() {
    if(!state.rawThresholdAuto)return true;
    status("Sampling noise: wait before remote");
    RawStartupGate gate(micros());
    while(!stopRequested && !gate.ready() && !gate.failed()) {
        const uint32_t nowUs=micros();
        if(gate.due(nowUs)) {
            const int16_t rssi=sampleRawRssi();
            if(!ELECHOUSE_cc1101.spiReady()) {
                errorCount(&LabSnapshot::spiErrors);status("SPI fault during RAW noise sample");return false;
            }
            gate.sample(rssi,micros());
            portENTER_CRITICAL(&lock);
            rawTrace.record(2,micros(),rssi,uint8_t(TriggeredCapture::Phase::Armed),0,0,false,false);
            portEXIT_CRITICAL(&lock);
            portENTER_CRITICAL(&lock);state.rawThresholdSamples=gate.samples();portEXIT_CRITICAL(&lock);
        }
        if(!gate.ready() && !gate.failed())vTaskDelay(1);
    }
    if(stopRequested){status("RAW cancelled during noise sample");return false;}
    if(!gate.ready()){status("RAW noise sample deadline");return false;}
    activeSettings.rssiThresholdDbm=gate.thresholdDbm();
    portENTER_CRITICAL(&lock);
    state.rawThresholdDbm=gate.thresholdDbm();state.rawNoiseDbm=gate.noiseDbm();
    state.rawThresholdSamples=gate.samples();state.rawThresholdReady=true;
    state.carrier=state.rssiDbm>=state.rawThresholdDbm;
    portEXIT_CRITICAL(&lock);
    status("Armed: measured noise threshold");
    return true;
}
void publishCaptureHistory() {
    portENTER_CRITICAL(&lock);
    state.captureHistoryCount=rawHistory.size();
    state.captureHistoryFull=rawHistory.size()==capture_history::kSlots;
    state.captureHistoryDuplicates=rawHistoryDuplicates;state.captureHistoryDiscarded=rawHistoryDiscarded;
    for(size_t i=0;i<capture_history::kSlots;++i) {
        const auto* entry=rawHistory.entry(i);
        state.captureHistory[i]=entry?*entry:capture_history::Entry{};
    }
    portEXIT_CRITICAL(&lock);
}
bool restartRawCandidate(TriggeredCapture& trigger) {
    raw_capture::stop();
    if(!raw_capture::start(CC1101_GDO0)){status("RAW rearm allocation failed");return false;}
    trigger=TriggeredCapture(capture,kWaveformCapacity,activeSettings.rssiThresholdDbm,raw_capture::snapshot().startedUs);
    historyCandidateStored=false;
    portENTER_CRITICAL(&lock);state.peakDbm=-127;portEXIT_CRITICAL(&lock);
    publishRaw(trigger,raw_capture::snapshot());
    return true;
}
bool waitRawHistoryBoundary(uint32_t receiveStartedMs) {
    uint32_t lastFaultAt=micros(),weakAt=0;
    auto previous=raw_capture::snapshot();bool weak=false;
    while(!stopRequested) {
        int32_t ignored;uint32_t ended;
        for(size_t n=0;n<raw_capture::kCapacity&&raw_capture::pop(ignored,ended);++n){}
        const int16_t rssi=sampleRawRssi();
        if(!ELECHOUSE_cc1101.spiReady()){status("SPI fault during history rearm");return false;}
        const uint32_t nowUs=micros();const auto current=raw_capture::snapshot();
        if(current.dropped!=previous.dropped||current.rejected!=previous.rejected)lastFaultAt=nowUs;
        previous=current;
        if(rssi<activeSettings.rssiThresholdDbm){if(!weak){weak=true;weakAt=nowUs;}}
        else weak=false;
        const bool edgeQuiet=!current.queued&&int32_t(nowUs-current.lastEdgeUs)>=int32_t(TriggeredCapture::kQuietUs);
        const bool weakQuiet=weak&&uint32_t(nowUs-weakAt)>=TriggeredCapture::kQuietUs;
        if(uint32_t(nowUs-lastFaultAt)>=TriggeredCapture::kQuietUs&&(weakQuiet||edgeQuiet))return true;
        if(activeSettings.rxTimeoutMs&&millis()-receiveStartedMs>=activeSettings.rxTimeoutMs)return false;
        vTaskDelay(1);
    }
    return false;
}
bool retainRawCandidate(TriggeredCapture& trigger,uint32_t receiveStartedMs,bool rearm = true) {
    raw_capture::stop();drainRaw(trigger,true);
    const auto final=raw_capture::snapshot();trigger.integrity(final.dropped,final.rejected);
    publishRaw(trigger,final);
    bool accepted=false;
    if(trigger.complete()) {
        size_t index=0;
        const capture_history::CandidateStatus quality={true,trigger.full(),trigger.dropped(),trigger.rejected()};
        const auto result=rawHistory.add(capture,trigger.size(),activeSettings.frequencyHz,state.peakDbm,quality,&index);
        if(result==capture_history::AddResult::Added) {
            rawHistoryImages[index]=activeImage;rawHistorySettings[index]=activeSettings;accepted=true;
        } else if(result==capture_history::AddResult::Duplicate) {++rawHistoryDuplicates;accepted=true;}
        else if(result==capture_history::AddResult::Full) {publishCaptureHistory();status("History full: choose/save signals");return false;}
    }
    if(!accepted)++rawHistoryDiscarded;
    historyCandidateStored=accepted;
    publishCaptureHistory();
    if(rawHistory.size()==capture_history::kSlots){status("History full: choose/save signals");return false;}
    if(!rearm)return false;
    if(stopRequested||!ELECHOUSE_cc1101.spiReady())return false;
    if(!accepted) {
        // Damaged candidates are never repaired/stored. Observe a separate
        // clean boundary before starting a new candidate; retain prior slots.
        if(!restartRawCandidate(trigger))return false;
        status("Discarded damaged candidate; waiting quiet");
        if(!waitRawHistoryBoundary(receiveStartedMs))return false;
    }
    if(!restartRawCandidate(trigger))return false;
    status(accepted?"Signal retained: listening for next":"Rearmed after discarded candidate");
    return true;
}
void stopRawCandidate(TriggeredCapture& trigger,const raw_capture::Snapshot& counters) {
    if(activeRawHistory) {
        // History accepts only live quiet-policy completions. Manual pause or
        // timeout must not promote a qualified prefix of an ongoing frame.
        if(!historyCandidateStored&&trigger.phase()==TriggeredCapture::Phase::Capturing) {
            ++rawHistoryDiscarded;publishCaptureHistory();
            status("Partial candidate discarded on pause");
        }
    } else trigger.stop(micros()); // Preserve advanced single/manual capture.
    publishRaw(trigger,counters);
}
bool transmitPacket(const uint8_t* data, size_t length) {
    if(length > 60 || !permitLabTx(activeProfile, activeSettings.frequencyHz, 100,
                                   activeSettings.txLimitMs)) return false;
    // A finite 100ms packet deadline, clear-channel observation and engine backoff.
    if(!idle()) return false;
    ELECHOUSE_cc1101.SpiStrobe(0x3B);
    powerTable(activeSettings.frequencyHz, activePower, false);
    ELECHOUSE_cc1101.SpiWriteReg(0x3F, length);
    ELECHOUSE_cc1101.SpiWriteBurstReg(0x3F, const_cast<uint8_t*>(data), length);
    ELECHOUSE_cc1101.SpiStrobe(0x35);
    uint32_t start = millis(); bool sawTx = false, complete = false;
    while(!stopRequested && millis() - start < 100) {
        uint8_t m = marc();
        if(m == 19) sawTx = true;
        if(sawTx && m == 1) { complete = true; break; }
        if(m == 22) { errorCount(&LabSnapshot::fifoErrors); break; }
        vTaskDelay(1);
    }
    if(!complete) errorCount(&LabSnapshot::timeouts);
    idle(); ELECHOUSE_cc1101.SpiStrobe(0x3B);
    receive(activeSettings.frequencyHz, false);
    return complete;
}
bool readPacket(uint8_t* frame, size_t& length) {
    const uint8_t m = marc();
    // Check IDLE first. Only then is RX FIFO frozen and its count meaningful.
    const uint8_t bytes = m==1||m==17?stableStatus(0x3B):0;
    uint32_t age=millis()-rxArmedAt;
    if(m==13&&digitalRead(CC1101_GDO0)) {
        if(!syncSeen){syncStarted=millis();syncSeen=true;errorCount(&LabSnapshot::syncEvents);}
        age=millis()-syncStarted>=200?1000:0;
    } else syncSeen=false;
    const FifoAction action=fifoAction(m,bytes,age);
    if(action==FifoAction::Recover){
        if((bytes&0x80)||m==17)errorCount(&LabSnapshot::fifoErrors);
        receive(activeSettings.frequencyHz,false);return false;
    }
    // RXOFF_MODE=IDLE: only consume a complete packet; never consume a partial FIFO.
    if(action!=FifoAction::Read)return false;
    length = ELECHOUSE_cc1101.SpiReadReg(0x3F);
    if(length > 60 || length + 3 != (bytes & 0x7F)) {
        errorCount(&LabSnapshot::fifoErrors); receive(activeSettings.frequencyHz, false); return false;
    }
    // Capture demodulator status while the packet remains frozen in IDLE.
    // Re-entering RX clears CRC_OK and can replace RSSI/FREQEST with noise.
    const uint8_t frequencyEstimate = ELECHOUSE_cc1101.SpiReadStatus(0x32);
    const uint8_t statusLqi = ELECHOUSE_cc1101.SpiReadStatus(0x33);
    ELECHOUSE_cc1101.SpiReadBurstReg(0x3F, frame, length);
    uint8_t trailer[2]; ELECHOUSE_cc1101.SpiReadBurstReg(0x3F, trailer, 2);
    const PacketMetadata metadata = packetMetadata(trailer[0], trailer[1], frequencyEstimate, statusLqi);
    const bool valid = metadata.crcOk;
    portENTER_CRITICAL(&lock);
    state.lastPacketLength = length; state.lastLqi = metadata.lqi;
    state.packetRssiHalfDbm = metadata.rssiHalfDbm; state.packetFrequencyErrorHz = metadata.frequencyErrorHz;
    state.lastStatusLqi = metadata.statusLqi; state.lastCrcOk = metadata.crcOk; state.lastStatusCrcOk = metadata.statusCrcOk;
    portEXIT_CRITICAL(&lock);
    if(!valid) errorCount(&LabSnapshot::crcErrors);
    else errorCount(&LabSnapshot::packets);
    receive(activeSettings.frequencyHz, false);
    return valid;
}
void publishMessage(uint16_t source, const char* text) {
    IncomingMessage incoming = {}; incoming.source = source;
    snprintf(incoming.text, sizeof(incoming.text), "%s", text);
    if(xQueueSend(inbox, &incoming, 0) != pdTRUE) {
        IncomingMessage old; xQueueReceive(inbox, &old, 0); xQueueSend(inbox, &incoming, 0);
    }
    portENTER_CRITICAL(&lock);
    snprintf(state.message, sizeof(state.message), "%s", text);
    portEXIT_CRITICAL(&lock);
}
void worker(void*) {
    LabMode mode = state.mode;
    const bool plaintext = state.chatMode == ChatMode::FlipperPlaintext;
    const bool diagnostic = state.chatDiagnostic;
    if(mode == LabMode::Explorer) loadPreset("AM270",activeImage);
    bool configured = configure(activeImage, activeSettings.frequencyHz);
    if(configured && mode == LabMode::Chat) status(diagnostic ? "Diagnostic RX-only30s; manual CRC" : "Chat RX ready");
    if(configured && mode==LabMode::Raw)configured=prepareRawThreshold();
    if(configured && (mode == LabMode::Raw || mode == LabMode::Explorer)) {
        configured = raw_capture::start(CC1101_GDO0);
        if(!configured)status("No internal RAM for RAW timestamps");
    }
    const uint32_t rawStartedUs = raw_capture::snapshot().startedUs;
    TriggeredCapture rawTrigger(capture, kWaveformCapacity, activeSettings.rssiThresholdDbm, rawStartedUs);
    if(configured && mode == LabMode::Raw) status("Armed: waiting RSSI threshold");
    // Sample immediately on the first iteration; chat must never decide to TX
    // using the initial placeholder RSSI. Further samples remain 10ms apart.
    uint32_t lastSample = millis() - 10, lastPreset = millis(), lastEdges = 0;
    uint32_t lastRegisters = millis() - 1000;
    uint16_t bin = 0; uint8_t presetIndex = 0;
    uint32_t nextTx = millis(), rxStart = millis(), budgetStart = millis(), airtimeMs = 0;
    uint32_t receivedMessages = 0, sentMessages = 0;
    ChatDelivery previousDelivery = ChatIdle;
    while(configured && !stopRequested) {
        uint32_t now = millis();
        if(mode != LabMode::Raw && now - lastSample >= 10) {
            sampleRadio(); lastSample = now;
            portENTER_CRITICAL(&lock); state.frequencyHz = activeSettings.frequencyHz; portEXIT_CRITICAL(&lock);
        }
        if(mode == LabMode::Monitor && now - lastRegisters >= 1000) {
            refreshDiagnosticRegisters(); lastRegisters = now;
        }
        if(mode == LabMode::Raw) {
            // Continuously drain noise into the bounded pretrigger window; it
            // cannot fill the2048-interval output bank before a real RSSI gate.
            if(!rawCaptureTick(rawTrigger,lastSample,now)) {
                if(!activeRawHistory||!retainRawCandidate(rawTrigger,rxStart))break;
                lastSample=millis()-1;
            }
        } else if(mode == LabMode::Sweep) {
            const uint32_t hz = activeSettings.frequencyHz + uint32_t(bin) * stepHz;
            if(!supportedFrequency(hz)) { status("Sweep exceeds CC1101 band"); break; }
            if(!receive(hz)) break;
            vTaskDelay(pdMS_TO_TICKS(5)); // >8 RSSI averaging periods for selected presets.
            const int16_t rssi = ELECHOUSE_cc1101.getRssi();
            portENTER_CRITICAL(&lock);
            state.sweep[bin] = rssi;
            if(rssi > state.peakHold[bin]) state.peakHold[bin] = rssi;
            state.sweepIndex = bin; state.frequencyHz = hz;
            portEXIT_CRITICAL(&lock);
            bin = (bin + 1) % kSweepBins;
            if(!bin) { portENTER_CRITICAL(&lock); ++state.sweepPasses; portEXIT_CRITICAL(&lock); }
        } else if(mode == LabMode::Explorer) {
            if(now - lastPreset >= 1500) {
                raw_capture::stop();
                auto s = raw_capture::snapshot();
                portENTER_CRITICAL(&lock);
                state.explorerEdges[presetIndex] = s.edges - lastEdges;
                state.explorerRssi[presetIndex] = state.rssiDbm;
                portEXIT_CRITICAL(&lock);
                presetIndex = (presetIndex + 1) % 4;
                RegisterImage next;
                if(!loadPreset(presetAt(presetIndex)->name, next) || !configure(next, activeSettings.frequencyHz)) break;
                raw_capture::start(CC1101_GDO0); lastEdges = 0;
                portENTER_CRITICAL(&lock); state.explorerPreset = presetIndex; portEXIT_CRITICAL(&lock);
                lastPreset = now;
            }
            int32_t ignored;for(size_t drained=0;drained<raw_capture::kCapacity&&!stopRequested&&raw_capture::pop(ignored);++drained){}
        } else if(mode == LabMode::Chat) {
            // Session engine calls are owned by this task, including UI commands.
            ChatCommand command;
            if(xQueueReceive(commands, &command, 0) == pdTRUE) {
                if(plaintext) {
                    const char* text = command.discover ? "online" : command.text;
                    if(diagnostic || activeProfile != TxProfile::ShieldedLab ||
                       !flipper_chat_tx_queue(&plaintextTx, text, strlen(text), now)) status("Chat busy or TX off");
                    else previousDelivery = ChatQueued; // A one-packet line can finish in this iteration.
                }
                else if(command.discover) chat_session_discover(&chat);
                else {
                    const uint16_t recipient = command.destination ? command.destination : state.peer;
                    if(!recipient) status("Discover/select peer first");
                    else if(!chat_session_send(&chat, recipient, command.text, strlen(command.text), now)) status("Chat busy or TX off");
                }
            }
            uint8_t frame[64]; size_t length = 0;
            if(plaintext) {
                FlipperChatMessage message;
                if(flipper_chat_rx_poll(&plaintextRx, now, &message)) {
                    publishMessage(0, message.text); ++receivedMessages;
                }
                if(readPacket(frame, length)) for(size_t i = 0; i < length; ++i) {
                    if(flipper_chat_rx_byte(&plaintextRx, frame[i], now, &message)) {
                        publishMessage(0, message.text); ++receivedMessages;
                    }
                }
            } else if(readPacket(frame, length) && chat_session_receive(&chat, frame, length, now)) {
                publishMessage(chat.received.source, chat.received.text); ++receivedMessages;
            }
            if(now - budgetStart >= 60000) { budgetStart = now; airtimeMs = 0; }
            // Reserve the worst-case100ms deadline for every unsent plaintext
            // chunk before starting. Our own budget cannot split a line for60s.
            const uint32_t allowance = plaintext && plaintextTx.offset == 0 && plaintextTx.length ?
                ((plaintextTx.length + FLIPPER_CHAT_PACKET - 1) / FLIPPER_CHAT_PACKET) * 100 : 100;
            const bool carrierBusy = chatChannelBusy(state.rssiDbm, activeSettings.rssiThresholdDbm,
                                                     digitalRead(CC1101_GDO0) == HIGH);
            const bool packetGap = int32_t(now - nextTx) < 0;
            const bool budgetBusy = airtimeMs + allowance > 1000;
            const bool busy = diagnostic || carrierBusy || packetGap || budgetBusy;
            length = plaintext ? flipper_chat_tx_poll(&plaintextTx, now, busy, frame, sizeof(frame)) :
                                 chat_session_poll(&chat, now, busy, frame, sizeof(frame));
            if(length) {
                const uint32_t started = millis();
                bool sent = transmitPacket(frame, length);
                if(plaintext) flipper_chat_tx_result(&plaintextTx, sent);
                else chat_session_tx_result(&chat, sent, millis());
                // Charge the whole attempt (including failed/timeout TX).
                airtimeMs += millis() - started;
                nextTx = millis() + 100;
            }
            const ChatDelivery delivery = plaintext ? ChatDelivery(plaintextTx.delivery) : chat.delivery;
            if(delivery == ChatDelivered && previousDelivery != ChatDelivered) ++sentMessages;
            previousDelivery = delivery;
            portENTER_CRITICAL(&lock);
            state.receivedMessages = receivedMessages; state.sentMessages = sentMessages;
            state.delivery = delivery; state.chatRetries = plaintext ? 0 : chat.attempts;
            state.plaintextRejected = plaintextRx.rejected; state.plaintextTruncated = plaintextRx.truncations;
            snprintf(state.txBlockReason, sizeof(state.txBlockReason), "%s", !state.txEnabled ? "TX off: Radio Settings" :
                     carrierBusy ? "Carrier busy" : packetGap ? "100ms packet gap" : budgetBusy ? "60s airtime budget" : "Ready: manual send");
            if(!plaintext) {
                for(size_t i = 0; i < 8; ++i) state.peers[i] = chat.peers[i].id;
                if(chat.peers[0].id) state.peer = chat.peers[0].id;
            }
            portEXIT_CRITICAL(&lock);
        }
        if(activeSettings.rxTimeoutMs && (mode != LabMode::Chat || diagnostic) && now - rxStart >= activeSettings.rxTimeoutMs) {
            status("Receive timeout"); break;
        }
        vTaskDelay(1);
    }
    raw_capture::stop();
    if(mode == LabMode::Raw) {
        drainRaw(rawTrigger, true);
        const auto finalCapture = raw_capture::snapshot();
        rawTrigger.integrity(finalCapture.dropped, finalCapture.rejected);
        stopRawCandidate(rawTrigger,finalCapture);
        traceRaw(3,micros(),int32_t(rawTrigger.rearmCount()),rawTrigger);freezeRawTrace();
    }
    idle(); pinMode(CC1101_GDO0, INPUT);
    portENTER_CRITICAL(&lock); state.active = false; portEXIT_CRITICAL(&lock);
    xEventGroupSetBits(events, kDone);
    vTaskDelete(nullptr);
}
}

bool labStart(LabMode mode, const RegisterImage& image, const RadioSettings& settings,
              TxProfile profile, uint8_t powerIndex, uint32_t sweepStepHz, ChatMode chatMode, bool chatDiagnostics,
              bool autoRawThreshold,bool multiRawHistory) {
    if(!labStop()) return false;
    const bool historyRequested=mode==LabMode::Raw&&multiRawHistory;
    if(historyRequested) {
        if(rawTraceBank){status("Release debug trace before history");return false;}
        if(rawHistory.size()){status("Clear retained history before new listen");return false;}
        if(!rawHistoryBank)rawHistoryBank=static_cast<int32_t*>(heap_caps_malloc(capture_history::kBankPulses*sizeof(int32_t),MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT));
        if(!rawHistoryBank||!rawHistory.bind(rawHistoryBank,capture_history::kBankPulses)){status("No internal RAM for history");return false;}
        rawHistoryDuplicates=rawHistoryDiscarded=0;
    } else if(!rawHistory.size()&&rawHistoryBank) {
        rawHistory.unbind();heap_caps_free(rawHistoryBank);rawHistoryBank=nullptr;
    }
    activeRawHistory=historyRequested;historyCandidateStored=false;
    if(mode==LabMode::Raw&&!capture) {
        capture=static_cast<int32_t*>(heap_caps_malloc(kWaveformCapacity*sizeof(int32_t),MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT));
        if(!capture){status("No RAM for RAW bank");return false;}
    }
    if(!events) events = xEventGroupCreateStatic(&eventStorage);
    if(!commands) commands = xQueueCreateStatic(3, sizeof(ChatCommand), commandQueueBytes, &commandQueueStorage);
    if(!inbox) inbox = xQueueCreateStatic(8, sizeof(IncomingMessage), inboxBytes, &inboxStorage);
    xQueueReset(commands);
    xQueueReset(inbox);
    activeImage = image; activeSettings = settings; activeProfile = profile;
    if(mode==LabMode::Raw){portENTER_CRITICAL(&lock);rawTrace.reset();portEXIT_CRITICAL(&lock);}
    activePower = powerIndex <= 2 ? powerIndex : 0; stepHz = sweepStepHz;
    memset(&state, 0, sizeof(state)); captured = 0;
    publishCaptureHistory();
    LabEvent event=LabEvent::Stop;
    switch(mode){case LabMode::Monitor:event=LabEvent::StartMonitor;break;case LabMode::Raw:event=LabEvent::StartRaw;break;
    case LabMode::Sweep:event=LabEvent::StartSweep;break;case LabMode::Explorer:event=LabEvent::StartExplorer;break;
    case LabMode::Chat:event=LabEvent::StartChat;break;default:break;}
    state.mode = transition(LabMode::Idle,event); state.active = true; state.rssiDbm = state.peakDbm = -127;
    state.chatMode = chatMode;
    state.chatDiagnostic = chatDiagnostics && mode == LabMode::Chat;
    state.rawThresholdAuto=autoRawThreshold && mode==LabMode::Raw;
    state.rawThresholdReady=mode==LabMode::Raw && !state.rawThresholdAuto;
    state.rawThresholdDbm=settings.rssiThresholdDbm;state.rawNoiseDbm=-127;
    state.frequencyHz = settings.frequencyHz;
    state.txEnabled = !state.chatDiagnostic && permitLabTx(profile, settings.frequencyHz, 100, settings.txLimitMs);
    snprintf(state.txBlockReason, sizeof(state.txBlockReason), "%s", state.txEnabled ? "Ready: manual send" : "TX off: Radio Settings");
    for(size_t i = 0; i < kSweepBins; ++i) state.sweep[i] = state.peakHold[i] = -127;
    snprintf(state.status, sizeof(state.status), "Starting radio");
    stopRequested = false; xEventGroupClearBits(events, kDone);
    if(mode == LabMode::Chat) {
        activeImage = RegisterImage();
        const uint8_t* registers = chatMode == ChatMode::FlipperPlaintext ? flipper_chat_registers : chat_radio_registers;
        const size_t registerBytes = chatMode == ChatMode::FlipperPlaintext ? FLIPPER_CHAT_REGISTER_BYTES : CHAT_RADIO_REGISTER_BYTES;
        for(size_t i = 0; i < registerBytes; i += 2) activeImage.set(registers[i], registers[i+1]);
        // Diagnostic receive keeps CRC-bad packets for the bounded manual check.
        // This changes local filtering only, with no transmission or wire change.
        if(state.chatDiagnostic) activeImage.set(0x07, 0x04);
        state.self = uint16_t(ESP.getEfuseMac()) ?: 1;
        // Initialized with a random boot session so rebooted senders do not reuse IDs.
        chat_session_init(&chat, state.self, uint16_t(esp_random()) ?: 1, "ESP-HACK", esp_random());
        state.self = chat.id;
        chat_session_enable_tx(&chat, state.txEnabled);
        flipper_chat_rx_init(&plaintextRx); flipper_chat_tx_init(&plaintextTx);
    }
    if(xTaskCreatePinnedToCore(worker, "radio-lab", 6144, nullptr, 2, &workerHandle, 0) != pdPASS) {
        workerHandle = nullptr; state.active = false; status("No RAM for worker"); return false;
    }
    return true;
}
bool labStop() {
    if(!workerHandle) return true;
    stopRequested = true;
    if(!(xEventGroupWaitBits(events, kDone, pdFALSE, pdTRUE, pdMS_TO_TICKS(2500)) & kDone)) return false;
    workerHandle = nullptr; return true;
}
void labRequestStop(){stopRequested=true;}
LabSnapshot labSnapshot() {
    LabSnapshot result;
    portENTER_CRITICAL(&lock); result = state; portEXIT_CRITICAL(&lock);
    return result;
}
bool labQueueText(const char* text, uint16_t destination) {
    const LabSnapshot current = labSnapshot();
    if(!text || !*text || strlen(text) > 160 || !commands || !current.active || current.mode != LabMode::Chat ||
       !current.txEnabled) return false;
    ChatCommand c = {}; c.destination = destination; snprintf(c.text, sizeof(c.text), "%s", text);
    return xQueueSend(commands, &c, 0) == pdTRUE;
}
bool labDiscover() {
    const LabSnapshot current = labSnapshot();
    if(!current.active || current.mode != LabMode::Chat || !current.txEnabled) return false;
    ChatCommand c = {}; c.discover = true;
    return commands && xQueueSend(commands, &c, 0) == pdTRUE;
}
bool labReceiveMessage(uint16_t& source, char* text, size_t capacity) {
    IncomingMessage incoming;
    if(!inbox || !text || !capacity || xQueueReceive(inbox, &incoming, 0) != pdTRUE) return false;
    source = incoming.source; snprintf(text, capacity, "%s", incoming.text); return true;
}
size_t labCopyPulses(int32_t* output, size_t capacity) {
    if(!output) return 0;
    portENTER_CRITICAL(&lock);
    size_t n = captured < capacity ? captured : capacity;
    portEXIT_CRITICAL(&lock);
    // Existing prefix is immutable until the UI explicitly starts a new capture.
    if(n)memcpy(output, capture, n * sizeof(*output));
    return n;
}
size_t labCopyCaptureHistory(size_t index,int32_t* output,size_t capacity,
                            RegisterImage& image,RadioSettings& settings) {
    if(workerHandle||!output)return 0;
    const auto* entry=rawHistory.entry(index);const int32_t* pulses=rawHistory.pulses(index);
    if(!entry||!entry->valid||!pulses||capacity<entry->count)return 0;
    memcpy(output,pulses,entry->count*sizeof(*output));
    image=rawHistoryImages[index];settings=rawHistorySettings[index];return entry->count;
}
bool labClearCaptureHistory() {
    if(workerHandle)return false;
    rawHistory.unbind();
    if(rawHistoryBank){heap_caps_free(rawHistoryBank);rawHistoryBank=nullptr;}
    rawHistoryDuplicates=rawHistoryDiscarded=0;activeRawHistory=false;publishCaptureHistory();return true;
}
bool labRawTraceEnable(bool enabled) {
    if(workerHandle)return false;
    if(enabled&&rawHistoryBank)return false;
    if(enabled&&!rawTraceBank) {
        rawTraceBank=static_cast<RawTraceEntry*>(heap_caps_malloc(kRawTraceCapacity*sizeof(RawTraceEntry),MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT));
        if(!rawTraceBank)return false;
        rawTrace.bind(rawTraceBank,kRawTraceCapacity);
    }
    portENTER_CRITICAL(&lock);rawTrace.enable(enabled);portEXIT_CRITICAL(&lock);
    return true;
}
RawTraceInfo labRawTraceInfo() {
    portENTER_CRITICAL(&lock);const RawTraceInfo result=rawTrace.info();portEXIT_CRITICAL(&lock);return result;
}
bool labRawTraceCopy(size_t index,RawTraceEntry& entry) {
    portENTER_CRITICAL(&lock);const bool copied=rawTrace.copy(index,entry);portEXIT_CRITICAL(&lock);return copied;
}
bool labRawTraceRelease() {
    if(workerHandle)return false;
    portENTER_CRITICAL(&lock);rawTrace.bind(nullptr,0);portEXIT_CRITICAL(&lock);
    if(rawTraceBank){heap_caps_free(rawTraceBank);rawTraceBank=nullptr;}
    return true;
}
bool labTransmitRaw(const int32_t* pulses, size_t count, const RegisterImage& image,
                    uint32_t hz, TxProfile profile, uint8_t powerIndex,
                    uint32_t limitMs, uint32_t& elapsedUs, int32_t& timingErrorUs) {
    elapsedUs = 0; timingErrorUs = 0;
    if(!labStop() || !pulses || !count || count > kWaveformCapacity) return false;
    uint64_t total = 0; size_t halves = 0;
    for(size_t i = 0; i < count; ++i) {
        if(!rmtHalves(pulses[i]) || (i == 0 && pulses[i] < 0) ||
           (i && (pulses[i] > 0) == (pulses[i-1] > 0))) return false;
        total += pulses[i] > 0 ? pulses[i] : -int64_t(pulses[i]); halves += rmtHalves(pulses[i]);
    }
    if(halves > 4094 || !permitLabTx(profile, hz, (total + 999) / 1000, limitMs)) return false;
    RadioSettings description;
    if(!describeRadio(image, hz, description) || description.packetFormat != PacketFormat::Async ||
       description.modulation != Modulation::AskOok) return false;
    if(image.paPresent) {
        // Multi-step ASK PA shaping cannot be replaced with binary OOK silently.
        if(image.pa[0] != 0) return false;
        for(size_t i = 2; i < 8; ++i) if(image.pa[i]) return false;
    }
    if(image.present[0x22] && (image.value[0x22] & 7) != 1) return false;
    RawSoftwareRestore software;
    RawRestoreBackend backend;
    RawRadioRestore<RawRestoreBackend> restore(backend);
    if(!restore.capture()) { status("RAW requires a readable stable RX/IDLE radio"); return false; }
    stopRequested = false;
    restore.mutating(); // Every later exit restores the physical snapshot.
    if(!configure(image, hz) || !idle()) return false;
    powerTable(hz, powerIndex, true);
    if(!ELECHOUSE_cc1101.spiReady()) { status("RAW power SPI write failed"); return false; }
    if(!rmtItems)rmtItems=static_cast<rmt_item32_t*>(heap_caps_calloc(2048,sizeof(rmt_item32_t),MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT));
    if(!rmtItems){status("No RAM for RMT bank");return false;}
    memset(rmtItems, 0, 2048*sizeof(rmt_item32_t)); size_t h = 0;
    for(size_t i = 0; i < count; ++i) {
        uint32_t remaining = pulses[i] > 0 ? pulses[i] : -pulses[i];
        while(remaining) {
            uint16_t part = remaining > 32767 ? 32767 : remaining;
            rmt_item32_t& item = rmtItems[h / 2];
            if(h & 1) { item.duration1 = part; item.level1 = pulses[i] > 0; }
            else { item.duration0 = part; item.level0 = pulses[i] > 0; }
            remaining -= part; ++h;
        }
    }
    rmt_config_t r = RMT_DEFAULT_CONFIG_TX(gpio_num_t(CC1101_GDO0), RMT_CHANNEL_0);
    r.clk_div = 80; r.mem_block_num = 2; r.tx_config.idle_output_en = true; r.tx_config.idle_level = RMT_IDLE_LEVEL_LOW;
    if(rmt_config(&r) != ESP_OK || rmt_driver_install(RMT_CHANNEL_0, 0, 0) != ESP_OK) return false;
    backend.installed=true;
    ELECHOUSE_cc1101.SpiStrobe(0x35);
    bool ok = waitState(19, 40); const uint32_t start = micros();
    if(ok) ok = rmt_write_items(RMT_CHANNEL_0, rmtItems, (h + 1) / 2, false) == ESP_OK;
    while(ok) {
        const esp_err_t result=rmt_wait_tx_done(RMT_CHANNEL_0,0);
        if(result==ESP_OK)break;
        if(result!=ESP_ERR_TIMEOUT){ok=false;rmt_tx_stop(RMT_CHANNEL_0);break;}
        if(stopRequested || digitalRead(BUTTON_BACK) == LOW || uint32_t(micros() - start) > limitMs * 1000) {
            rmt_tx_stop(RMT_CHANNEL_0); ok = false; break;
        }
        delay(1);
    }
    elapsedUs = micros() - start; timingErrorUs = int64_t(elapsedUs) - int64_t(total);
    // Wall completion includes task polling latency; this is NOT measured edge jitter.
    const bool restored=restore.finish();
    return ok && restored;
}
}
