#pragma once
#include "radio_config.h"
#include "lab_limits.h"
#include <stddef.h>
#include <stdint.h>

namespace research {
enum class ChatMode : uint8_t { EspAck, FlipperPlaintext };
struct LabSnapshot {
    bool active, connected, carrier, captureFull, captureTriggered, captureComplete, txEnabled, chatDiagnostic;
    LabMode mode;
    uint8_t part, version, marcState, rxBytes, pktStatus;
    uint32_t frequencyHz, samples, packets, crcErrors, fifoErrors, spiErrors, timeouts, misoTimeouts;
    uint32_t pulseCount, dropped, edgeCount, rejected, captureHighWater, captureTailDiscarded;
    int16_t rssiDbm, peakDbm;
    uint16_t sweepIndex, sweepPasses;
    int16_t sweep[kSweepBins], peakHold[kSweepBins];
    uint8_t registers[kRegisterCount];
    uint8_t explorerPreset;
    uint32_t explorerEdges[4];
    int16_t explorerRssi[4];
    uint16_t peer, self;
    uint16_t peers[8];
    uint32_t receivedMessages, sentMessages, chatRetries;
    uint32_t plaintextRejected, plaintextTruncated;
    uint32_t syncEvents, rxRearms;
    uint8_t lastPacketLength, lastLqi;
    int16_t packetRssiHalfDbm;
    int32_t packetFrequencyErrorHz;
    uint8_t lastStatusLqi;
    bool lastCrcOk, lastStatusCrcOk;
    ChatMode chatMode;
    uint8_t delivery;
    char message[161], status[64], txBlockReason[32];
};
// One radio worker owns every SPI transaction while active. No OLED or SD in it.
bool labStart(LabMode mode, const RegisterImage& image, const RadioSettings& settings,
              TxProfile profile, uint8_t powerIndex, uint32_t sweepStepHz = 25000,
              ChatMode chatMode = ChatMode::EspAck, bool chatDiagnostics = false);
bool labStop();
void labRequestStop();
LabSnapshot labSnapshot();
bool labQueueText(const char* text, uint16_t destination = 0);
bool labDiscover();
bool labReceiveMessage(uint16_t& source, char* text, size_t capacity);
size_t labCopyPulses(int32_t* output, size_t capacity);
// Called only while worker stopped. Exact user waveform, finite duration, manual BACK abort.
bool labTransmitRaw(const int32_t* pulses, size_t count, const RegisterImage& image,
                    uint32_t frequencyHz, TxProfile profile, uint8_t powerIndex,
                    uint32_t limitMs, uint32_t& elapsedUs, int32_t& timingErrorUs);
}
