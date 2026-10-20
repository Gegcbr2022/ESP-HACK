#pragma once

#include <stdint.h>

namespace research {
namespace raw_capture {

// One shared GDO capture engine. The owner must detach other interrupts from the
// pin before start(), and stop() before changing the pin direction/radio mode.
// Positive intervals are HIGH, negative intervals LOW; all values are in us.
constexpr uint16_t kCapacity = 2048;

struct Snapshot {
    bool running;
    uint8_t pin;
    uint8_t lastLevel;
    uint16_t queued;
    uint16_t highWater;
    uint32_t edges;
    uint32_t captured;
    uint32_t dropped;
    uint32_t rejected;
    uint32_t lastEdgeUs;
};

bool start(uint8_t pin);
void stop();
bool pop(int32_t& durationUs);
Snapshot snapshot();

}  // namespace raw_capture
}  // namespace research
