#include "triggered_capture.h"
#include "raw_startup_gate.h"
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
    c.rssi(-70, now); // a second real observation, not a single RSSI spike
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
    ring.poll(now + 7000);
    CHECK(!ring.complete()&&!ring.triggered()&&ring.size()==0&&ring.rearmCount()==1);
    return true;
}
static bool minimum_gap_and_wrap(void) {
    int32_t out[256]; uint32_t now = 0xFFFFFF00u;
    TriggeredCapture c(out, 256, -90, now); startBurst(c, now, 18);
    c.poll(now + 6999); CHECK(!c.complete()); // minimum20 never fabricated by idle time
    feed(c, now, 200); feed(c, now, -200);
    CHECK(c.size() == 20);
    c.poll(now + 6999); CHECK(!c.complete()); c.poll(now + 7000); CHECK(c.complete());
    CHECK(c.dropped() == 0 && c.rejected() == 0);
    return true;
}
static bool startup_gate(void) {
    using research::RawStartupGate;
    const int16_t floors[]={-138,-111,-110,-109,-100,-90,-88,-60,-55,-54,-11,INT16_MIN,INT16_MAX};
    const int16_t thresholds[]={-100,-100,-100,-99,-90,-80,-78,-50,-45,-45,-45,-100,-45};
    for(size_t f=0;f<sizeof floors/sizeof floors[0];++f) {
        uint32_t now=0xFFFFF000u; RawStartupGate gate(now);
        for(unsigned i=0;i<64;++i) {
            CHECK(gate.due(now)); gate.sample(floors[f],now);
            CHECK(gate.samples()==i+1);
            CHECK(gate.ready()==(i==63)); CHECK(!gate.failed());
            if(i<63) {
                CHECK(!gate.due(now+3999));gate.sample(0,now+3999);
                CHECK(gate.samples()==i+1); now+=4000;
            }
        }
        CHECK(gate.noiseDbm()==floors[f]&&gate.thresholdDbm()==thresholds[f]);
        CHECK(!gate.due(now+1000000));gate.sample(0,now+1000000);
        CHECK(gate.ready()&&!gate.failed()&&gate.samples()==64);
    }
    // Outliers and acquisition order cannot bias the median noise floor.
    int16_t values[64];
    for(unsigned i=0;i<64;++i)values[i]=i<31?-130:i<63?-88:0;
    uint32_t rng=0x4297;
    for(unsigned trial=0;trial<40;++trial) {
        for(unsigned i=63;i;--i) {
            rng=rng*1664525u+1013904223u;const unsigned j=rng%(i+1);
            const int16_t save=values[i];values[i]=values[j];values[j]=save;
        }
        RawStartupGate gate(123);for(unsigned i=0;i<64;++i)gate.sample(values[i],123+i*4000);
        CHECK(gate.ready()&&gate.noiseDbm()==-88&&gate.thresholdDbm()==-78);
    }
    RawStartupGate even(0);
    for(unsigned i=0;i<64;++i)even.sample(i<32?-92:-90,i*4000);
    CHECK(even.ready()&&even.noiseDbm()==-91&&even.thresholdDbm()==-81);
    RawStartupGate timeout(0xFFFFFF00u);
    for(unsigned i=0;i<63;++i)timeout.sample(-88,0xFFFFFF00u+i*8000);
    CHECK(!timeout.ready()&&!timeout.failed()&&timeout.samples()==63);
    CHECK(timeout.due(0xFFFFFF00u+RawStartupGate::kDeadlineUs));
    timeout.sample(-88,0xFFFFFF00u+RawStartupGate::kDeadlineUs);
    CHECK(timeout.failed()&&!timeout.ready()&&timeout.samples()==63);
    timeout.sample(-88,1);CHECK(timeout.failed()&&timeout.samples()==63);
    RawStartupGate noSamples(0);noSamples.sample(-88,RawStartupGate::kDeadlineUs);
    CHECK(noSamples.failed()&&!noSamples.ready()&&noSamples.samples()==0);
    return true;
}
static bool clean_armed_recovery(void) {
    int32_t out[256];uint32_t now=0xFFFFFF00u;
    TriggeredCapture c(out,256,-78,now);
    c.integrity(2,1); // old demodulator noise, before a clean quiet boundary
    feed(c,now,-10);feed(c,now,100);feed(c,now,100); // old repeated sign
    c.rssi(-88,now);const uint32_t quiet=now;
    c.rssi(-88,quiet+6999);CHECK(c.dropped()==2&&c.rejected()==2);
    c.rssi(-88,quiet+7000);
    CHECK(c.dropped()==0&&c.rejected()==0&&!c.triggered());
    CHECK(c.discardedArmingDropped()==2&&c.discardedArmingRejected()==2);
    now=quiet+7000;startBurst(c,now);CHECK(c.triggered()&&c.size()==22);
    c.poll(now+7000);CHECK(c.complete()&&c.dropped()==0&&c.rejected()==0);
    c.integrity(3,1);CHECK(!c.complete()&&c.dropped()==1); // active loss is never retired
    c.rssi(-100,now+100000);c.rssi(-100,now+200000);
    CHECK(c.phase()==TriggeredCapture::Phase::Corrupt&&c.dropped()==1);

    now=0;TriggeredCapture interrupted(out,256,-90,now);
    interrupted.integrity(0,1);interrupted.rssi(-110,0);
    interrupted.rssi(-110,6999);interrupted.integrity(0,2); // new error restarts quiet window
    interrupted.rssi(-110,7000);interrupted.rssi(-110,13999);
    CHECK(interrupted.rejected()==2);
    interrupted.rssi(-110,14000);CHECK(interrupted.rejected()==0);
    CHECK(interrupted.discardedArmingRejected()==2);

    now=0;TriggeredCapture premature(out,256,-90,now);
    premature.integrity(0,1);premature.rssi(-110,0);premature.rssi(-70,6999);
    CHECK(premature.phase()==TriggeredCapture::Phase::Corrupt); // no full clean boundary
    return true;
}
static bool short_candidate_rearms(void) {
    int32_t out[256];uint32_t now=100;
    TriggeredCapture c(out,256,-90,now);startBurst(c,now,6);
    c.rssi(-110,now);const uint32_t weak=now;
    c.poll(weak+6999);CHECK(c.triggered()&&c.size()==6);
    c.poll(weak+7000);
    CHECK(!c.triggered()&&!c.complete()&&c.size()==0&&c.rearmCount()==1&&c.discardedShort()==6);
    now=weak+7000;startBurst(c,now,24);
    CHECK(c.size()==24&&c.triggered());
    for(size_t i=0;i<24;++i)CHECK(out[i]==(i&1?-100:100));
    c.rssi(-110,now);c.poll(now+7000);CHECK(c.complete()&&c.rearmCount()==1);

    now=0;TriggeredCapture carrier(out,256,-90,now);startBurst(carrier,now,2);
    carrier.poll(now+7000);CHECK(carrier.rearmCount()==1&&!carrier.triggered());
    carrier.rssi(-70,now+8000);CHECK(!carrier.triggered()); // strong unmodulated carrier cannot loop-trigger
    carrier.rssi(-110,now+9000);carrier.rssi(-110,now+16000);
    now+=16000;startBurst(carrier,now);CHECK(carrier.triggered()&&carrier.size()==22);

    now=0;TriggeredCapture lost(out,256,-90,now);startBurst(lost,now,2);
    lost.integrity(1,0);lost.poll(now+7000);
    CHECK(lost.phase()==TriggeredCapture::Phase::Corrupt&&lost.dropped()==1&&lost.rearmCount()==0);
    now=0;TriggeredCapture pending(out,256,-90,now);startBurst(pending,now,2);
    pending.rssi(-110,now);const uint32_t began=now;
    for(unsigned i=0;i<100;++i)feed(pending,now,i&1?-1:1);
    pending.poll(began+7000);
    CHECK(pending.phase()==TriggeredCapture::Phase::Corrupt&&pending.dropped()==36&&pending.rearmCount()==0);
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
static bool post_gate_qualification(void) {
    int32_t out[256];
    // Arbitrarily long weak prehistory plus one strong RSSI observation must
    // not qualify, even when many additional noise edges follow that spike.
    const unsigned noiseAfter[]={0,1,19,20,48,64};
    for(size_t trial=0;trial<sizeof noiseAfter/sizeof noiseAfter[0];++trial) {
        uint32_t now=0xFFFF0000u;TriggeredCapture c(out,256,-78,now);
        feed(c,now,-17);
        for(unsigned i=0;i<1000;++i) {feed(c,now,i&1?-50:50);c.rssi(-88,now);}
        CHECK(!c.triggered()&&c.size()==0);
        c.rssi(-60,now);const size_t context=c.size();
        CHECK(context>=63&&c.postTriggerPulses()==0&&!c.rssiQualified());
        for(unsigned i=0;i<noiseAfter[trial];++i)feed(c,now,i&1?-50:50);
        CHECK(c.postTriggerPulses()==noiseAfter[trial]);
        c.rssi(-88,now);c.poll(now+7000);
        CHECK(!c.complete()&&!c.triggered()&&c.size()==0&&c.rearmCount()==1);
        CHECK(c.discardedShort()==context+noiseAfter[trial]);
        now+=7000;startBurst(c,now,24);c.rssi(-88,now);c.poll(now+7000);
        CHECK(c.complete()&&c.postTriggerPulses()==24&&c.rssiQualified());
    }
    // Sustained RSSI alone cannot turn 64 old noise intervals into a signal.
    uint32_t now=100;TriggeredCapture old(out,256,-90,now);feed(old,now,-17);
    for(unsigned i=0;i<80;++i)feed(old,now,i&1?-10:10);
    old.rssi(-70,now);old.rssi(-70,now+1000);
    CHECK(old.rssiQualified()&&old.postTriggerPulses()==0);
    old.rssi(-110,now+1001);old.poll(now+8001);
    CHECK(!old.complete()&&old.rearmCount()==1);

    // With independently sustained RSSI, every count below20 still rejects;
    // exactly20 succeeds even with a full64-interval weak noise prefix.
    for(unsigned count=0;count<=20;++count) {
        now=100;TriggeredCapture minimum(out,256,-90,now);feed(minimum,now,-17);
        for(unsigned i=0;i<80;++i)feed(minimum,now,i&1?-10:10);
        minimum.rssi(-70,now);const uint32_t began=now;
        for(unsigned i=0;i<count;++i)feed(minimum,now,i&1?-100:100);
        if(now-began<1000)now=began+1000;
        minimum.rssi(-70,now);
        CHECK(minimum.rssiQualified()&&minimum.postTriggerPulses()==count);
        minimum.rssi(-110,now);minimum.poll(now+7000);
        CHECK(minimum.complete()==(count==20));
        CHECK(minimum.rearmCount()==(count<20?1u:0u));
    }

    // Exactly 20 post-gate ended intervals and a second strong observation at
    // 1000us qualify; 999us and duplicate observations do not. Cross micros wrap.
    now=0xFFFFFE00u;TriggeredCapture boundary(out,256,-90,now);feed(boundary,now,-17);
    const uint32_t gate=now;boundary.rssi(-70,gate);boundary.rssi(-70,gate);
    for(unsigned i=0;i<20;++i)feed(boundary,now,i&1?-25:25);
    boundary.rssi(-70,gate+999);
    CHECK(boundary.postTriggerPulses()==20&&!boundary.rssiQualified());
    boundary.rssi(-70,gate+1000);CHECK(boundary.rssiQualified());
    boundary.rssi(-110,gate+1001);boundary.poll(gate+8001);CHECK(boundary.complete());

    // Deferred weak edges qualify only if a later strong sample confirms the
    // short dip. A manual stop must not use an unconfirmed weak tail to reach20.
    now=0;TriggeredCapture dip(out,256,-90,now);startBurst(dip,now,18);
    CHECK(dip.postTriggerPulses()==18&&dip.rssiQualified());
    dip.rssi(-110,now);feed(dip,now,200);feed(dip,now,-200);
    CHECK(dip.postTriggerPulses()==18);
    dip.rssi(-70,now);CHECK(dip.postTriggerPulses()==20);
    dip.rssi(-110,now);dip.poll(now+7000);CHECK(dip.complete());
    now=0;TriggeredCapture stop(out,256,-90,now);startBurst(stop,now,18);
    stop.rssi(-110,now);feed(stop,now,200);feed(stop,now,-200);stop.stop(now);
    CHECK(!stop.complete()&&stop.size()==20&&stop.postTriggerPulses()==18);

    // Queued intervals ending before the sampled gate time are context even
    // if delivered by the worker after that sample (absolute ISR timestamps).
    TriggeredCapture delayed(out,256,-90,0);delayed.pulse(-17,17);delayed.rssi(-70,5000);
    for(unsigned i=0;i<20;++i)delayed.pulse(i&1?-100:100,117+i*100);
    CHECK(delayed.size()==20&&delayed.postTriggerPulses()==0);
    delayed.stop(6000);CHECK(!delayed.complete());

    // Independent Princeton A53A53/TE390 and CAME12 A53/TE320 bit timings:
    // both have >=20 post-gate intervals and survive normal sampled RSSI dips.
    const unsigned bits[]={24,12};const uint32_t key[]={0xA53A53,0xA53};
    for(unsigned protocol=0;protocol<2;++protocol) {
        now=0xFFFF0000u;TriggeredCapture real(out,256,-90,now);feed(real,now,-17);
        real.rssi(-70,now);
        for(unsigned bit=0;bit<bits[protocol];++bit) {
            const bool one=(key[protocol]>>(bits[protocol]-1-bit))&1u;
            const int32_t te=protocol?320:390;
            feed(real,now,one?(protocol?640:1170):te);
            feed(real,now,-(one?te:(protocol?640:1170)));
            if((bit&1)==1)real.rssi(-70,now);
        }
        CHECK(real.postTriggerPulses()==bits[protocol]*2&&real.rssiQualified());
        real.rssi(-110,now);real.poll(now+7000);
        CHECK(real.complete()&&real.size()==bits[protocol]*2);
        for(size_t i=0;i<real.size();++i)CHECK((out[i]>0)==!(i&1));
    }
    // Active corruption remains terminal even with otherwise qualified signal.
    now=0;TriggeredCapture corrupt(out,256,-90,now);startBurst(corrupt,now,24);
    corrupt.integrity(0,1);corrupt.rssi(-110,now);corrupt.poll(now+7000);
    CHECK(corrupt.phase()==TriggeredCapture::Phase::Corrupt&&corrupt.rejected()==1&&corrupt.rearmCount()==0);
    return true;
}
struct RecoveryTraceEvent { uint32_t at; int32_t value; uint8_t type; };
#include "strong_leadin_trace.inc"
static bool carrier_leadin_recovers(void) {
    int32_t out[256];TriggeredCapture replay(out,256,-70,recoveryTrace[0].at);
    bool sawBlockedCarrier=false,sawRealTrigger=false;
    for(size_t i=0;i<sizeof recoveryTrace/sizeof recoveryTrace[0];++i) {
        const RecoveryTraceEvent& event=recoveryTrace[i];
        if(event.type==1)replay.pulse(event.value,event.at);
        else {replay.rssi(int16_t(event.value),event.at);replay.poll(event.at);}
        if(event.at>=2909441&&event.at<2951020) {
            CHECK(!replay.triggered()&&!replay.complete()&&replay.rearmCount()==1);
            sawBlockedCarrier=true;
        }
        if(event.at>2954101&&replay.triggered())sawRealTrigger=true;
        if(replay.terminal())break;
    }
    CHECK(sawBlockedCarrier&&sawRealTrigger&&replay.complete());
    CHECK(replay.rearmCount()==1&&replay.postTriggerPulses()>=20&&replay.rssiQualified());
    CHECK(replay.size()>=48&&replay.dropped()==0&&replay.rejected()==0);
    // Decode the first24 independent 1:3/3:1 OOK bit ratios. This recorded frame
    // is the known synthetic key, not a copied production encoder waveform.
    uint32_t decoded=0;
    for(unsigned bit=0;bit<24;++bit) {
        CHECK(out[2*bit]>0&&out[2*bit+1]<0);
        decoded=(decoded<<1)|(uint32_t(out[2*bit])>uint32_t(-out[2*bit+1]));
    }
    CHECK(decoded==0xA53A53);

    // Four fresh complete intervals are needed after the new partial LOW.
    // Old context, repeated RSSI and a long carrier lead-in do not count.
    uint32_t now=0xFFFF0000u;TriggeredCapture fresh(out,256,-70,now);
    feed(fresh,now,-100);fresh.rssi(-30,now);fresh.rssi(-30,now+1000);
    now+=7000;fresh.poll(now);CHECK(fresh.rearmCount()==1&&!fresh.triggered());
    for(unsigned i=0;i<42;++i) {now+=1000;fresh.rssi(-30,now);CHECK(!fresh.triggered()&&fresh.rearmCount()==1);}
    feed(fresh,now,-49000); // Partial interval at the beginning of the real train.
    for(unsigned i=0;i<3;++i) {feed(fresh,now,i&1?-390:390);fresh.rssi(-30,now);CHECK(!fresh.triggered());}
    feed(fresh,now,-390);fresh.rssi(-30,now);
    CHECK(fresh.triggered()&&fresh.postTriggerPulses()==0&&fresh.size()==4);
    for(unsigned i=0;i<20;++i)feed(fresh,now,i&1?-390:390);
    fresh.rssi(-30,now);CHECK(fresh.rssiQualified()&&fresh.postTriggerPulses()==20);
    fresh.poll(now+7000);CHECK(fresh.complete());

    // Sparse unrelated edges separated by a new quiet gap cannot accumulate
    // into the four-edge release, and faults are never treated as clean data.
    now=0;TriggeredCapture sparse(out,256,-70,now);feed(sparse,now,-100);sparse.rssi(-30,now);
    now+=7000;sparse.poll(now);feed(sparse,now,-49000);
    feed(sparse,now,300);feed(sparse,now,-300);feed(sparse,now,300);
    feed(sparse,now,-8000);sparse.rssi(-30,now);CHECK(!sparse.triggered());
    feed(sparse,now,300);feed(sparse,now,-300);feed(sparse,now,300);
    sparse.rssi(-30,now);CHECK(!sparse.triggered());
    sparse.integrity(0,1);feed(sparse,now,-300);sparse.rssi(-30,now);
    CHECK(!sparse.triggered()&&!sparse.complete()&&sparse.rejected()==1);
    return true;
}
static bool history_groups_repeat_guard(void) {
    int32_t out[256];uint32_t now=0xFFFFF000u;
    TriggeredCapture grouped(out,256,-70,now,30000);
    feed(grouped,now,-17);grouped.rssi(-30,now);
    for(unsigned repeat=0;repeat<2;++repeat) {
        for(unsigned bit=0;bit<24;++bit) {
            const bool one=(0xA53A53u>>(23-bit))&1u;
            feed(grouped,now,one?1170:390);feed(grouped,now,one?-390:-1170);
            if((bit&1)==1)grouped.rssi(-30,now);
        }
        feed(grouped,now,390); // Ended sync HIGH; LOW guard ends at the next frame.
        if(!repeat) {
            grouped.rssi(-90,now);grouped.poll(now+7000);
            CHECK(!grouped.complete()&&grouped.triggered()&&grouped.size()==49);
            feed(grouped,now,-11700);grouped.rssi(-30,now);
            CHECK(!grouped.complete()&&grouped.size()==50&&out[49]==-11700);
        }
    }
    CHECK(grouped.size()==99&&grouped.postTriggerPulses()==99&&grouped.rssiQualified());
    grouped.rssi(-90,now);grouped.poll(now+29999);CHECK(!grouped.complete());
    grouped.poll(now+30000);CHECK(grouped.complete()&&grouped.rearmCount()==0);
    for(unsigned repeat=0;repeat<2;++repeat) {
        uint32_t decoded=0;const size_t start=repeat*50;
        for(unsigned bit=0;bit<24;++bit) {
            CHECK(out[start+2*bit]>0&&out[start+2*bit+1]<0);
            decoded=(decoded<<1)|(out[start+2*bit]>-out[start+2*bit+1]);
        }
        CHECK(decoded==0xA53A53);
    }
    // The same grouping also applies to strong-carrier edge quiet. Advanced
    // callers which omit the new argument retain the original7ms behavior.
    int32_t originalOut[256];uint32_t originalNow=100;
    TriggeredCapture original(originalOut,256,-90,originalNow);startBurst(original,originalNow);
    now=100;TriggeredCapture strong(out,256,-90,now,30000);startBurst(strong,now);
    original.poll(originalNow+7000);strong.poll(now+7000);
    CHECK(original.complete()&&!strong.complete());
    strong.poll(now+29999);CHECK(!strong.complete());strong.poll(now+30000);CHECK(strong.complete());
    now=100;TriggeredCapture shortHistory(out,256,-90,now,30000);startBurst(shortHistory,now,6);
    shortHistory.poll(now+7000);CHECK(shortHistory.triggered()&&shortHistory.rearmCount()==0);
    shortHistory.poll(now+30000);CHECK(!shortHistory.triggered()&&shortHistory.rearmCount()==1);
    TriggeredCapture armed(out,256,-90,0,30000);armed.integrity(0,1);
    armed.rssi(-110,0);armed.rssi(-110,7000);
    CHECK(armed.rejected()==0&&armed.discardedArmingRejected()==1); // still7ms, not30ms
    return true;
}
int main() {
    if(!noise_inactive_and_pretrigger() || !minimum_gap_and_wrap() || !short_rssi_dip_and_tail() ||
       !deferred_overflow_and_stop() || !full_and_corruption() || !startup_gate() ||
       !clean_armed_recovery() || !short_candidate_rearms() || !post_gate_qualification() ||
       !carrier_leadin_recovers() || !history_groups_repeat_guard()) return 1;
    printf("triggered_capture: PASS (%u checks; startup median/deadline, post-gate/RSSI qualification, armed recovery, bounds, corruption)\n", checks);
    return 0;
}
