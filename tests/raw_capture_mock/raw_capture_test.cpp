#include <stdio.h>
#include <stdlib.h>
#include "Arduino.h"
#include "soc/gpio_struct.h"
#include "research/raw_capture.h"
#include "research/triggered_capture.h"
#include "esp_heap_caps.h"

// Always enabled: some Windows CRT builds disable assert without _DEBUG.
static unsigned checks;
#define CHECK(condition) do { ++checks; if (!(condition)) { \
    fprintf(stderr, "check failed line %d: %s\n", __LINE__, #condition); abort(); \
} } while (0)

MockGpio GPIO = {};
static uint32_t nowUs;
static uint8_t attachedPin;
static void (*callback)(void*);
static void* callbackArg;
static unsigned int detachCount;
static bool fireAtAttach;
static bool failAllocation;
static unsigned allocations;
static uint32_t timestampBank[research::raw_capture::kCapacity];
void* heap_caps_malloc(size_t bytes,int caps) {
    ++allocations;
    CHECK(bytes==sizeof(timestampBank));CHECK(caps==(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT));
    return failAllocation?nullptr:timestampBank;
}

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
    if(fireAtAttach) {
        fireAtAttach=false;nowUs+=100;
        if(pin<32)GPIO.in^=1UL<<pin;else GPIO.in1.val^=1UL<<(pin-32);
        callback(callbackArg);
    }
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
    int32_t duration = 0;uint32_t endedAt=0;
    CHECK(!pop(duration));
    CHECK(!start(40));
    CHECK(!start(20));
    nowUs = 100;
    level(4, false);
    failAllocation=true;CHECK(!start(4));
    CHECK(!callback&&!snapshot().running&&allocations==1);
    failAllocation=false;
    CHECK(start(4));
    CHECK(allocations==2);
    CHECK(snapshot().startedUs==100);
    edge(500, true);
    edge(1000, false);
    CHECK(pop(duration,endedAt) && duration == -500 && endedAt==600);
    CHECK(pop(duration,endedAt) && duration == 1000 && endedAt==1600);
    CHECK(snapshot().startedUs==100);
    CHECK(!pop(duration));
    CHECK(snapshot().captured == 2 && snapshot().edges == 2);
    CHECK(snapshot().lastLevel == 0 && snapshot().lastEdgeUs == 1600);
    stop();
    CHECK(!callback && !snapshot().running && detachCount == 1);

    // Start resets counters and handles unsigned micros wraparound.
    nowUs = 0xFFFFFF00U;
    CHECK(start(4));
    edge(400, true);
    CHECK(pop(duration,endedAt) && duration == -400 && endedAt==uint32_t(0xFFFFFF00U+400));
    CHECK(snapshot().startedUs==0xFFFFFF00U);
    CHECK(allocations==2); // Retained internal bank, no repeated allocation.
    CHECK(snapshot().captured == 1 && snapshot().dropped == 0);

    // Overflow must retain the full bounded queue and count each dropped edge.
    stop();
    level(4, false);
    CHECK(start(4));
    const uint32_t overflowStart=snapshot().startedUs;
    for (unsigned int i = 0; i < kCapacity + 7U; ++i) edge(100, (i & 1U) == 0);
    auto full = snapshot();
    CHECK(full.queued == kCapacity && full.highWater == kCapacity);
    CHECK(full.captured == kCapacity && full.dropped == 7 && full.edges == kCapacity + 7U);
    for (unsigned int i = 0; i < kCapacity; ++i) {
        CHECK(pop(duration,endedAt));
        CHECK(duration == ((i & 1U) ? 100 : -100));
        CHECK(endedAt==uint32_t(overflowStart+(i+1)*100));
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

    // Rejected intervals still advance wall time. A duration sum would miss
    // the100us rejected callback, while the atomic timestamp stays exact.
    nowUs=5000;level(4,false);CHECK(start(4));
    edge(100,false);edge(250,true);
    CHECK(pop(duration,endedAt)&&duration==-250&&endedAt==5350);
    CHECK(endedAt!=snapshot().startedUs+uint32_t(-duration));
    CHECK(snapshot().rejected==1&&snapshot().startedUs==5000);
    stop();

    // An IRQ may fire before start() returns. The epoch is immutable, and
    // queued real ending times must never become a reconstructed future edge.
    nowUs=1000;level(4,false);fireAtAttach=true;CHECK(start(4));
    CHECK(snapshot().startedUs==1000&&snapshot().lastEdgeUs==1100&&snapshot().queued==1);
    int32_t output[64]={};research::TriggeredCapture trigger(output,64,-75,snapshot().startedUs);
    while(pop(duration,endedAt))trigger.pulse(duration,endedAt);
    trigger.rssi(-60,nowUs);
    high=true;
    for(unsigned i=0;i<20;++i){high=!high;edge(300,high);CHECK(pop(duration,endedAt));CHECK(endedAt==nowUs);trigger.pulse(duration,endedAt);}
    trigger.rssi(-60,nowUs);
    CHECK(trigger.size()==20);trigger.poll(nowUs);CHECK(!trigger.complete());
    trigger.poll(nowUs+6999);CHECK(!trigger.complete());trigger.poll(nowUs+7000);CHECK(trigger.complete());
    stop();
    CHECK(allocations==2);
    printf("raw_capture: %u checks passed (actual ISR timing/epoch, wrap, overflow, GPIO banks, rejected-edge gaps, trigger race)\n",checks);
}

