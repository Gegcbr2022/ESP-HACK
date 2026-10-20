#pragma once
#include <stdint.h>
#include <stddef.h>

namespace research {
enum class TxProfile : uint8_t { ReceiveOnly, ShieldedLab, FilePlayback };
inline uint32_t cc1101FrequencyWord(uint32_t hz);
// These are deliberate test channels, not a regional authorization database.
inline bool laboratoryChannel(uint32_t hz) {
    return hz == 433920000UL || hz == 868350000UL;
}
inline bool permitLabTx(TxProfile profile, uint32_t hz, uint32_t durationMs,
                        uint32_t configuredLimitMs) {
    if(durationMs == 0 || durationMs > configuredLimitMs || configuredLimitMs > 5000)
        return false;
    switch(profile) {
    case TxProfile::ShieldedLab: return laboratoryChannel(hz);
    // Manual, finite file playback preserves the file's supported RF frequency.
    case TxProfile::FilePlayback: return cc1101FrequencyWord(hz) != 0;
    default: return false;
    }
}
static const size_t kWaveformCapacity = 2048;
static const size_t kSweepBins = 64;
inline uint32_t cc1101FrequencyWord(uint32_t hz) {
    static const uint32_t bands[][2] = {{300000000,348000000},{387000000,464000000},{779000000,928000000}};
    for(size_t i=0;i<3;++i)if(hz>=bands[i][0]&&hz<=bands[i][1]) {
        const uint32_t lower=(uint64_t(bands[i][0])*65536+25999999)/26000000;
        const uint32_t upper=uint64_t(bands[i][1])*65536/26000000;
        uint32_t word=(uint64_t(hz)*65536+13000000)/26000000;
        return word<lower?lower:word>upper?upper:word;
    }
    return 0;
}
// RMT uses a 1 MHz tick. Each half item is a 15-bit duration.
inline size_t rmtHalves(int32_t pulse) {
    if(pulse == 0 || pulse == INT32_MIN) return 0;
    uint32_t us = pulse < 0 ? uint32_t(-pulse) : uint32_t(pulse);
    return (us + 32766UL) / 32767UL;
}
enum class LabMode : uint8_t { Idle, Monitor, Raw, Sweep, Explorer, Chat };
enum class FifoAction : uint8_t { Wait, Read, Recover };
inline FifoAction fifoAction(uint8_t marc, uint8_t rxBytes, uint32_t ageMs) {
    if((rxBytes&0x80)||marc==17||(rxBytes&0x7F)>63)return FifoAction::Recover;
    if(marc==1)return (rxBytes&0x7F)>=3?FifoAction::Read:FifoAction::Recover;
    if(marc==13)return ageMs>=1000?FifoAction::Recover:FifoAction::Wait;
    return ageMs>=100?FifoAction::Recover:FifoAction::Wait;
}
enum class LabEvent : uint8_t { Stop, StartMonitor, StartRaw, StartSweep, StartExplorer, StartChat };
inline LabMode transition(LabMode, LabEvent event) {
    switch(event) {
    case LabEvent::StartMonitor: return LabMode::Monitor;
    case LabEvent::StartRaw: return LabMode::Raw;
    case LabEvent::StartSweep: return LabMode::Sweep;
    case LabEvent::StartExplorer: return LabMode::Explorer;
    case LabEvent::StartChat: return LabMode::Chat;
    default: return LabMode::Idle;
    }
}
}
