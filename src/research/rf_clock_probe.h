#pragma once
#include "rf_clock_math.h"

namespace research {
enum class RfClockProbeError : uint8_t {
    None, Busy, Spi, NotIdle, OtherClock, Counter, Overflow, NoClock, Timing,
    Implausible, Restore
};
struct RfClockSample { uint32_t edges, elapsedUs, oscillatorHz; };
struct RfClockProbeResult {
    RfClockSample samples[3];
    uint32_t medianHz;
    uint8_t oldIocfg0;
    bool restored;
    RfClockProbeError error;
};
// Main-task-only, caller must suspendLegacyReceive() before calling, then
// restoreReceiveMode() afterward on success/failure. Caller owns SPI exclusively,
// CC1101 already initialized, no other PCNT7 owner. Independently refuses active
// Lab workers before touching GPIO/SPI. Never starts RX/TX or writes PATABLE.
// SIDLE keeps XOSC running. Saves/restores/verifies IOCFG0; leaves GPIO4 INPUT,
// PCNT7 paused/disabled/disconnected.3x50ms sampling; finite<=40idle polls.
// Normal duration<300ms, reject any measured interval>200ms. Scheduler latency
// can extend wall time: this task-context helper is not a hard-time preemption.
bool radioLabClockProbe(RfClockProbeResult& result);
const char* rfClockProbeErrorText(RfClockProbeError error);
}
