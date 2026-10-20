#include "lab_service.h"
#include "raw_capture.h"
#include "triggered_capture.h"
#include "../CONFIG.h"
#include "../../shared/chat_radio.h"
#include "../../shared/chat_session.h"
#include "../../shared/flipper_chat_compat.h"
#include <Arduino.h>
#include <ELECHOUSE_CC1101_SRC_DRV.h>
#include <driver/rmt.h>
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
void frequency(uint32_t hz) {
    const uint32_t word = cc1101FrequencyWord(hz);
    ELECHOUSE_cc1101.SpiWriteReg(0x0D, word >> 16);
    ELECHOUSE_cc1101.SpiWriteReg(0x0E, word >> 8);
    ELECHOUSE_cc1101.SpiWriteReg(0x0F, word);
}
bool receive(uint32_t hz, bool calibrate = true) {
    if(!idle()) return false;
    frequency(hz);
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
    frequency(hz);
    bool ok = true;
    for(size_t i = 0; i < kRegisterCount; ++i) {
        uint8_t reg = ELECHOUSE_cc1101.SpiReadReg(i);
        portENTER_CRITICAL(&lock); state.registers[i] = reg; portEXIT_CRITICAL(&lock);
        if(image.present[i] && i != 0x0D && i != 0x0E && i != 0x0F && reg != image.value[i]) ok = false;
    }
    if(!ok) { errorCount(&LabSnapshot::spiErrors); status("Register readback mismatch"); return false; }
    return receive(hz);
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
    portEXIT_CRITICAL(&lock);
}
void drainRaw(TriggeredCapture& trigger, uint32_t& edgeTime, bool finishing = false) {
    int32_t pulse;
    for(size_t drained = 0; drained < raw_capture::kCapacity &&
        (finishing || !stopRequested) && raw_capture::pop(pulse); ++drained) {
        if(pulse && pulse != INT32_MIN)
            edgeTime += pulse > 0 ? uint32_t(pulse) : uint32_t(-pulse);
        trigger.pulse(pulse, edgeTime);
    }
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
    ELECHOUSE_cc1101.SpiReadBurstReg(0x3F, frame, length);
    uint8_t trailer[2]; ELECHOUSE_cc1101.SpiReadBurstReg(0x3F, trailer, 2);
    bool valid = trailer[1] & 0x80;
    portENTER_CRITICAL(&lock);
    state.lastPacketLength = length; state.lastLqi = trailer[1] & 0x7F;
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
    if(configured && (mode == LabMode::Raw || mode == LabMode::Explorer))
        configured = raw_capture::start(CC1101_GDO0);
    uint32_t rawEdgeTime = raw_capture::snapshot().lastEdgeUs;
    TriggeredCapture rawTrigger(capture, kWaveformCapacity, activeSettings.rssiThresholdDbm, rawEdgeTime);
    if(configured && mode == LabMode::Raw) status("Armed: waiting RSSI threshold");
    uint32_t lastSample = millis(), lastPreset = millis(), lastEdges = 0;
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
            drainRaw(rawTrigger, rawEdgeTime);
            const auto s = raw_capture::snapshot();
            rawTrigger.integrity(s.dropped, s.rejected);
            if(now - lastSample >= 1) {
                const bool wasTriggered = rawTrigger.triggered();
                const int16_t rssi = sampleRawRssi(); lastSample = now;
                if(!ELECHOUSE_cc1101.spiReady()) {
                    rawTrigger.pulse(0, micros()); errorCount(&LabSnapshot::spiErrors);
                    publishRaw(rawTrigger, s); status("SPI fault during RAW"); break;
                }
                rawTrigger.rssi(rssi, micros());
                if(!wasTriggered && rawTrigger.triggered()) status("Triggered: capturing one burst");
            }
            rawTrigger.poll(micros()); publishRaw(rawTrigger, s);
            if(rawTrigger.terminal()) {
                if(rawTrigger.full()) status("RAW full: capture not saved");
                else if(rawTrigger.complete()) status("One burst ready: OK to save");
                else status("RAW damaged: capture not saved");
                break;
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
            const bool carrierBusy = ELECHOUSE_cc1101.SpiReadStatus(0x38) & 0x40;
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
        drainRaw(rawTrigger, rawEdgeTime, true);
        const auto finalCapture = raw_capture::snapshot();
        rawTrigger.integrity(finalCapture.dropped, finalCapture.rejected);
        rawTrigger.stop(micros()); publishRaw(rawTrigger, finalCapture);
    }
    idle(); pinMode(CC1101_GDO0, INPUT);
    portENTER_CRITICAL(&lock); state.active = false; portEXIT_CRITICAL(&lock);
    xEventGroupSetBits(events, kDone);
    vTaskDelete(nullptr);
}
}

bool labStart(LabMode mode, const RegisterImage& image, const RadioSettings& settings,
              TxProfile profile, uint8_t powerIndex, uint32_t sweepStepHz, ChatMode chatMode, bool chatDiagnostics) {
    if(!labStop()) return false;
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
    activePower = powerIndex <= 2 ? powerIndex : 0; stepHz = sweepStepHz;
    memset(&state, 0, sizeof(state)); captured = 0;
    LabEvent event=LabEvent::Stop;
    switch(mode){case LabMode::Monitor:event=LabEvent::StartMonitor;break;case LabMode::Raw:event=LabEvent::StartRaw;break;
    case LabMode::Sweep:event=LabEvent::StartSweep;break;case LabMode::Explorer:event=LabEvent::StartExplorer;break;
    case LabMode::Chat:event=LabEvent::StartChat;break;default:break;}
    state.mode = transition(LabMode::Idle,event); state.active = true; state.rssiDbm = state.peakDbm = -127;
    state.chatMode = chatMode;
    state.chatDiagnostic = chatDiagnostics && mode == LabMode::Chat;
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
    activeSettings.frequencyHz = hz;
    stopRequested = false;
    if(!configure(image, hz) || !idle()) return false;
    powerTable(hz, powerIndex, true);
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
    ELECHOUSE_cc1101.SpiStrobe(0x35);
    bool ok = waitState(19, 40); const uint32_t start = micros();
    if(ok) ok = rmt_write_items(RMT_CHANNEL_0, rmtItems, (h + 1) / 2, false) == ESP_OK;
    while(ok) {
        const esp_err_t result=rmt_wait_tx_done(RMT_CHANNEL_0,0);
        if(result==ESP_OK)break;
        if(result!=ESP_ERR_TIMEOUT){ok=false;rmt_tx_stop(RMT_CHANNEL_0);break;}
        if(digitalRead(BUTTON_BACK) == LOW || uint32_t(micros() - start) > limitMs * 1000) {
            rmt_tx_stop(RMT_CHANNEL_0); ok = false; break;
        }
        delay(1);
    }
    elapsedUs = micros() - start; timingErrorUs = int64_t(elapsedUs) - int64_t(total);
    // Wall completion includes task polling latency; this is NOT measured edge jitter.
    rmt_driver_uninstall(RMT_CHANNEL_0); idle(); pinMode(CC1101_GDO0, INPUT);
    return ok;
}
}
