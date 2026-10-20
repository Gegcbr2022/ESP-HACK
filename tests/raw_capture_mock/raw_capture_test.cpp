#include <stdio.h>
#include <stdlib.h>
#include "Arduino.h"
#include "soc/gpio_struct.h"
#include "research/raw_capture.h"

// Always enabled: some Windows CRT builds disable assert without _DEBUG.
#define CHECK(condition) do { if (!(condition)) { \
    fprintf(stderr, "check failed line %d: %s\n", __LINE__, #condition); abort(); \
} } while (0)

MockGpio GPIO = {};
static uint32_t nowUs;
static uint8_t attachedPin;
static void (*callback)(void*);
static void* callbackArg;
static unsigned int detachCount;

uint32_t micros() { return nowUs; }
int digitalRead(uint8_t pin) {
    return pin < 32 ? ((GPIO.in >> pin) & 1U) : ((GPIO.in1.val >> (pin - 32)) & 1U);
}
void pinMode(uint8_t, int mode) { CHECK(mode == INPUT); }
void attachInterruptArg(uint8_t pin, void (*handler)(void*), void* arg, int mode) {
    CHECK(mode == CHANGE);
    attachedPin = pin;
    callback = handler;
    callbackArg = arg;
}
void detachInterrupt(uint8_t pin) { CHECK(pin == attachedPin); callback = nullptr; ++detachCount; }

static void level(uint8_t pin, bool high) {
    uint32_t& bank = pin < 32 ? GPIO.in : GPIO.in1.val;
    const uint32_t mask = 1U << (pin < 32 ? pin : pin - 32);
    if (high) bank |= mask;
    else bank &= ~mask;
}
static void edge(uint32_t deltaUs, bool high) {
    CHECK(callback);
    nowUs += deltaUs;
    level(attachedPin, high);
    callback(callbackArg);
}

int main() {
    using namespace research::raw_capture;
    int32_t duration = 0;
    CHECK(!pop(duration));
    CHECK(!start(40));
    CHECK(!start(20));
    nowUs = 100;
    level(4, false);
    CHECK(start(4));
    edge(500, true);
    edge(1000, false);
    CHECK(pop(duration) && duration == -500);
    CHECK(pop(duration) && duration == 1000);
    CHECK(!pop(duration));
    CHECK(snapshot().captured == 2 && snapshot().edges == 2);
    CHECK(snapshot().lastLevel == 0 && snapshot().lastEdgeUs == 1600);
    stop();
    CHECK(!callback && !snapshot().running && detachCount == 1);

    // Start resets counters and handles unsigned micros wraparound.
    nowUs = 0xFFFFFF00U;
    CHECK(start(4));
    edge(400, true);
    CHECK(pop(duration) && duration == -400);
    CHECK(snapshot().captured == 1 && snapshot().dropped == 0);

    // Overflow must retain the full bounded queue and count each dropped edge.
    stop();
    level(4, false);
    CHECK(start(4));
    for (unsigned int i = 0; i < kCapacity + 7U; ++i) edge(100, (i & 1U) == 0);
    auto full = snapshot();
    CHECK(full.queued == kCapacity && full.highWater == kCapacity);
    CHECK(full.captured == kCapacity && full.dropped == 7 && full.edges == kCapacity + 7U);
    for (unsigned int i = 0; i < kCapacity; ++i) {
        CHECK(pop(duration));
        CHECK(duration == ((i & 1U) ? 100 : -100));
    }
    CHECK(!pop(duration));

    // Refill after head/tail wrap and verify ordering/levels are retained.
    bool high = digitalRead(4);
    for (unsigned int i = 0; i < 1500; ++i) { high = !high; edge(111, high); }
    for (unsigned int i = 0; i < 1000; ++i) CHECK(pop(duration));
    for (unsigned int i = 0; i < 1000; ++i) { high = !high; edge(222, high); }
    for (unsigned int i = 0; i < 500; ++i) { CHECK(pop(duration)); CHECK(duration == 111 || duration == -111); }
    for (unsigned int i = 0; i < 1000; ++i) { CHECK(pop(duration)); CHECK(duration == 222 || duration == -222); }
    CHECK(!pop(duration));

    // Same-level/spurious callbacks and unrepresentable intervals are visible.
    edge(100, high);
    CHECK(snapshot().rejected == 1);
    high = !high;
    edge(0, high);
    CHECK(snapshot().rejected == 2);
    high = !high;
    edge(0x80000000U, high);
    CHECK(snapshot().rejected == 3 && !pop(duration));

    // Direct access to GPIO32..39 uses the second hardware input bank.
    stop();
    level(33, false);
    CHECK(start(33));
    edge(777, true);
    edge(333, false);
    CHECK(pop(duration) && duration == -777);
    CHECK(pop(duration) && duration == 333);
    CHECK(snapshot().pin == 33 && snapshot().rejected == 0);
    stop();
    CHECK(!snapshot().running && snapshot().queued == 0);
    puts("raw_capture: signed timing, wrap, bounds, overflow, reset, GPIO banks, rejection tests passed");
}

