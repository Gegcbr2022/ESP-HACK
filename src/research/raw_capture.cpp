#include "raw_capture.h"

#include <Arduino.h>
#include <driver/gpio.h>
#include <soc/gpio_struct.h>

namespace research {
namespace raw_capture {
namespace {

static_assert((kCapacity & (kCapacity - 1)) == 0, "Ring must be a power of two");
DRAM_ATTR int32_t intervals[kCapacity];
DRAM_ATTR uint16_t head = 0;
DRAM_ATTR uint16_t tail = 0;
DRAM_ATTR uint16_t used = 0;
DRAM_ATTR Snapshot counters = {};
portMUX_TYPE captureMux = portMUX_INITIALIZER_UNLOCKED;

void IRAM_ATTR edgeInterrupt(void*) {
    const uint32_t now = micros();
    portENTER_CRITICAL_ISR(&captureMux);
    if (counters.running) {
        const uint32_t duration = now - counters.lastEdgeUs;
        const uint8_t pin = counters.pin;
        const uint8_t level = pin < 32 ? ((GPIO.in >> pin) & 1U)
                                      : ((GPIO.in1.val >> (pin - 32)) & 1U);
        ++counters.edges;
        // Reject the first corrupted/repeated edge rather than fabricate a
        // zero interval, and avoid signed overflow after a >35-minute idle.
        if (level == counters.lastLevel || duration == 0 || duration > INT32_MAX) {
            ++counters.rejected;
        } else if (used == kCapacity) {
            ++counters.dropped;
        } else {
            intervals[head] = counters.lastLevel ? static_cast<int32_t>(duration)
                                                 : -static_cast<int32_t>(duration);
            head = (head + 1U) & (kCapacity - 1U);
            ++used;
            ++counters.captured;
            if (used > counters.highWater) counters.highWater = used;
        }
        counters.lastEdgeUs = now;
        counters.lastLevel = level;
    }
    portEXIT_CRITICAL_ISR(&captureMux);
}

}  // namespace

bool start(uint8_t pin) {
    if (!GPIO_IS_VALID_GPIO(pin)) return false;
    stop();
    pinMode(pin, INPUT);
    portENTER_CRITICAL(&captureMux);
    head = tail = used = 0;
    counters = {};
    counters.pin = pin;
    counters.lastLevel = digitalRead(pin) ? 1 : 0;
    counters.lastEdgeUs = micros();
    counters.running = true;
    portEXIT_CRITICAL(&captureMux);
    attachInterruptArg(pin, edgeInterrupt, nullptr, CHANGE);
    return true;
}

void stop() {
    portENTER_CRITICAL(&captureMux);
    const bool wasRunning = counters.running;
    const uint8_t pin = counters.pin;
    counters.running = false;
    portEXIT_CRITICAL(&captureMux);
    if (wasRunning) detachInterrupt(pin);
}

bool pop(int32_t& durationUs) {
    portENTER_CRITICAL(&captureMux);
    const bool available = used != 0;
    if (available) {
        durationUs = intervals[tail];
        tail = (tail + 1U) & (kCapacity - 1U);
        --used;
    }
    portEXIT_CRITICAL(&captureMux);
    return available;
}

Snapshot snapshot() {
    portENTER_CRITICAL(&captureMux);
    Snapshot result = counters;
    result.queued = used;
    portEXIT_CRITICAL(&captureMux);
    return result;
}

}  // namespace raw_capture
}  // namespace research
