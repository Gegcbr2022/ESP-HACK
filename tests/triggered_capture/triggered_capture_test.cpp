#include "triggered_capture.h"
#include <stdio.h>
#include <string.h>

using research::TriggeredCapture;
static unsigned checks;
#define CHECK(x) do { ++checks; if(!(x)) { fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x); return false; } } while(0)

static void feed(TriggeredCapture& c, uint32_t& now, int32_t value) {
    if(value && value != INT32_MIN) now += value > 0 ? uint32_t(value) : uint32_t(-value);
    c.pulse(value, now);
}
static void startBurst(TriggeredCapture& c, uint32_t& now, size_t intervals = 22) {
    feed(c, now, -17); // partial interval at arming is never a valid waveform.
    c.rssi(-70, now);
    for(size_t i = 0; i < intervals; ++i) feed(c, now, i & 1 ? -100 : 100);
}
static bool noise_inactive_and_pretrigger(void) {
    int32_t out[256]; memset(out, 0, sizeof(out)); uint32_t now = 0;
    TriggeredCapture c(out, 256, -90, now);
    feed(c, now, -17);
    for(unsigned i = 0; i < 10000; ++i) {
        feed(c, now, i & 1 ? -50 : 50); c.rssi(-105, now); c.poll(now);
    }
    CHECK(c.phase() == TriggeredCapture::Phase::Armed && c.size() == 0 && !c.full());
    CHECK(!c.triggered() && c.dropped() == 0 && c.rejected() == 0);
    c.stop(now); CHECK(!c.complete() && c.size() == 0);

    now = 0; TriggeredCapture ring(out, 256, -90, now);
    feed(ring, now, 123); // started while HIGH: discard this partial HIGH.
    int32_t input[80];
    for(size_t i = 0; i < 80; ++i) { input[i] = i & 1 ? int32_t(i + 100) : -int32_t(i + 100); feed(ring, now, input[i]); }
    ring.rssi(-91, now); CHECK(!ring.triggered());
    ring.rssi(-90, now); CHECK(ring.triggered());
    // Last64 begin with a LOW interval. Drop it, retain the following63 exactly.
    CHECK(ring.size() == 63 && out[0] == input[17]);
    for(size_t i = 0; i < 63; ++i) CHECK(out[i] == input[i + 17]);
    ring.poll(now + 6999); CHECK(!ring.complete());
    ring.poll(now + 7000); CHECK(ring.complete());
    return true;
}
static bool minimum_gap_and_wrap(void) {
    int32_t out[256]; uint32_t now = 0xFFFFFF00u;
    TriggeredCapture c(out, 256, -90, now); startBurst(c, now, 18);
    c.poll(now + 100000); CHECK(!c.complete()); // minimum20 never fabricated by idle time
    feed(c, now, 200); feed(c, now, -200);
    CHECK(c.size() == 20);
    c.poll(now + 6999); CHECK(!c.complete()); c.poll(now + 7000); CHECK(c.complete());
    CHECK(c.dropped() == 0 && c.rejected() == 0);
    return true;
}
static bool short_rssi_dip_and_tail(void) {
    int32_t out[256]; uint32_t now = 100;
    TriggeredCapture c(out, 256, -90, now); startBurst(c, now);
    CHECK(c.size() == 22);
    c.rssi(-110, now); const uint32_t weak = now;
    feed(c, now, 300); feed(c, now, -400);
    c.poll(weak + 6999); CHECK(!c.complete() && c.size() == 22);
    now = weak + 6999;
    c.rssi(-80, now); CHECK(c.size() == 24 && out[22] == 300 && out[23] == -400);
    CHECK(c.dropped() == 0 && c.discardedTail() == 0);
    c.rssi(-110, now); const uint32_t tail_started = now;
    for(unsigned i = 0; i < 1000; ++i) feed(c, now, i & 1 ? -1 : 1);
    CHECK(!c.terminal());
    c.poll(tail_started + 6999); CHECK(!c.complete());
    c.poll(tail_started + 7000); CHECK(c.complete() && c.size() == 24);
    CHECK(c.discardedTail() == 1000 && c.dropped() == 0); // explicitly excluded weak tail, not hidden capture loss
    return true;
}
static bool deferred_overflow_and_stop(void) {
    int32_t out[256]; uint32_t now = 0;
    TriggeredCapture c(out, 256, -90, now); startBurst(c, now);
    c.rssi(-110, now);
    for(unsigned i = 0; i < 100; ++i) feed(c, now, i & 1 ? -1 : 1);
    c.rssi(-70, now); CHECK(c.phase() == TriggeredCapture::Phase::Corrupt);
    CHECK(c.dropped() == 36 && !c.complete());

    now = 0; TriggeredCapture manual(out, 256, -90, now); startBurst(manual, now);
    manual.rssi(-110, now); feed(manual, now, 300); feed(manual, now, -300);
    manual.stop(now); CHECK(manual.complete() && manual.size() == 24 && out[22] == 300 && out[23] == -300);

    now = 0; TriggeredCapture partial(out, 256, -90, now); startBurst(partial, now, 4);
    partial.stop(now); CHECK(!partial.complete() && partial.phase() == TriggeredCapture::Phase::Stopped);
    return true;
}
static bool full_and_corruption(void) {
    int32_t guarded[22]; for(size_t i = 0; i < 22; ++i) guarded[i] = 1234567;
    uint32_t now = 0; TriggeredCapture full(guarded + 1, 20, -90, now); startBurst(full, now);
    CHECK(full.full() && full.size() == 20 && !full.complete());
    CHECK(guarded[0] == 1234567 && guarded[21] == 1234567);
    int32_t out[256];
    now = 0; TriggeredCapture dropped(out, 256, -90, now); startBurst(dropped, now);
    dropped.integrity(2, 0); CHECK(!dropped.complete() && dropped.phase() == TriggeredCapture::Phase::Corrupt && dropped.dropped() == 2);
    now = 0; TriggeredCapture rejected(out, 256, -90, now); startBurst(rejected, now);
    rejected.integrity(0, 3); CHECK(rejected.rejected() == 3 && rejected.phase() == TriggeredCapture::Phase::Corrupt);
    now = 0; TriggeredCapture invalid(out, 256, -90, now); startBurst(invalid, now);
    feed(invalid, now, INT32_MIN); CHECK(invalid.rejected() == 1 && invalid.phase() == TriggeredCapture::Phase::Corrupt);
    now = 0; TriggeredCapture zero(out, 256, -90, now); startBurst(zero, now);
    feed(zero, now, 0); CHECK(zero.rejected() == 1 && zero.phase() == TriggeredCapture::Phase::Corrupt);
    now = 0; TriggeredCapture repeated(out, 256, -90, now); startBurst(repeated, now);
    feed(repeated, now, -300); CHECK(repeated.rejected() == 1 && repeated.phase() == TriggeredCapture::Phase::Corrupt);
    now = 0; TriggeredCapture dirty_pre(out, 256, -90, now); dirty_pre.integrity(1, 0);
    dirty_pre.rssi(-70, now); CHECK(dirty_pre.triggered() && !dirty_pre.complete() && dirty_pre.dropped() == 1);
    return true;
}
int main() {
    if(!noise_inactive_and_pretrigger() || !minimum_gap_and_wrap() || !short_rssi_dip_and_tail() ||
       !deferred_overflow_and_stop() || !full_and_corruption()) return 1;
    printf("triggered_capture: PASS (%u checks; noise gating, pretrigger wrap/order, quiet windows, bounds, corruption)\n", checks);
    return 0;
}
