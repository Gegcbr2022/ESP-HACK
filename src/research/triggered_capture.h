#pragma once
#include <stddef.h>
#include <stdint.h>

namespace research {

// Task-context policy only. ISR timestamps/intervals and RSSI readings are inputs;
// this class performs no GPIO, SPI, allocation, threading or wall-clock reads.
class TriggeredCapture {
public:
    enum class Phase : uint8_t { Armed, Capturing, Complete, Full, Corrupt, Stopped };
    static const size_t kPretrigger = 64;
    static const size_t kMinimumPulses = 20;
    static const uint32_t kQuietUs = 7000;
    static const uint32_t kStrongSpanUs = 1000;
    static const uint8_t kFreshEdgesToRearm = 4;

    TriggeredCapture(int32_t* output, size_t capacity, int16_t threshold, uint32_t startedUs,
                     uint32_t endQuietUs = kQuietUs)
        : output_(output), capacity_(capacity), used_(0), postTriggerPulses_(0), threshold_(threshold),
          phase_(output && capacity ? Phase::Armed : Phase::Corrupt), head_(0), queued_(0),
          pendingTotal_(0), pendingLost_(0), discardedTail_(0), rawDropped_(0),
          rawRejected_(0), badSamples_(0), lostSamples_(0), droppedBase_(0), rejectedBase_(0),
          discardedArmingDropped_(0), discardedArmingRejected_(0), discardedShort_(0),
          rearms_(0), lastEdge_(startedUs), weakSince_(0), preQuietSince_(0), triggeredAt_(startedUs),
          endQuietUs_(endQuietUs), freshEdges_(0),
          first_(true), previousPositive_(false), strong_(false), triggered_(false),
          preQuiet_(false), waitForWeak_(false), rssiQualified_(false) {}

    void integrity(uint32_t dropped, uint32_t rejected) {
        const uint32_t nextDropped = dropped - droppedBase_;
        const uint32_t nextRejected = rejected - rejectedBase_;
        if(phase_ == Phase::Armed && (nextDropped != rawDropped_ || nextRejected != rawRejected_)) {
            preQuiet_ = false; freshEdges_ = 0;
        }
        rawDropped_ = nextDropped; rawRejected_ = nextRejected;
        if((rawDropped_ || rawRejected_) && (phase_ == Phase::Capturing || phase_ == Phase::Complete))
            phase_ = Phase::Corrupt;
    }

    void pulse(int32_t value, uint32_t edgeUs) {
        if(terminal()) return;
        lastEdge_ = edgeUs;
        if(value == 0 || value == INT32_MIN) { reject(); return; }
        const bool positive = value > 0;
        if(first_) {
            // At arming, the currently running HIGH or LOW interval is partial.
            first_ = false; previousPositive_ = positive; return;
        }
        if(positive == previousPositive_) { reject(); return; }
        previousPositive_ = positive;
        if(phase_ == Phase::Armed && waitForWeak_) {
            const uint32_t duration = positive ? uint32_t(value) : uint32_t(-value);
            if(duration > kQuietUs) { freshEdges_ = 0; clearPending(); }
            else if(++freshEdges_ >= kFreshEdgesToRearm) waitForWeak_ = false;
        }
        const bool afterTrigger = phase_ == Phase::Capturing &&
                                  uint32_t(edgeUs - triggeredAt_) < 0x80000000UL;
        if(phase_ == Phase::Armed || !strong_) remember(value, afterTrigger);
        else append(value, afterTrigger);
    }

    void rssi(int16_t dbm, uint32_t nowUs) {
        if(terminal()) return;
        // Honor a quiet window even if a later RSSI sample arrives after the
        // worker was delayed; returning energy starts a different burst.
        poll(nowUs);
        if(terminal()) return;
        const bool strong = dbm >= threshold_;
        if(phase_ == Phase::Armed) {
            if(!strong) {
                if(!preQuiet_) { preQuiet_ = true; preQuietSince_ = nowUs; }
                else if(uint32_t(nowUs - preQuietSince_) >= kQuietUs &&
                        (rawDropped_ || rawRejected_ || badSamples_ || waitForWeak_))
                    cleanArmedBoundary();
                return;
            }
            preQuiet_ = false;
            if(waitForWeak_) return;
            triggered_ = true;
            if(rawDropped_ || rawRejected_ || badSamples_) { phase_ = Phase::Corrupt; return; }
            phase_ = Phase::Capturing; strong_ = true; triggeredAt_ = nowUs;
            flush(false); // Prehistory is context, never evidence of a new signal.
            return;
        }
        // A single RSSI spike must not validate even a dense demodulator-noise
        // burst. Require a later strong observation at least 1ms after the gate.
        if(strong && uint32_t(nowUs - triggeredAt_) >= kStrongSpanUs) rssiQualified_ = true;
        if(strong && !strong_) {
            // A short RSSI dip is part of the same burst. Every deferred interval
            // must survive; otherwise the capture is explicitly damaged.
            if(pendingLost_) {
                lostSamples_ += pendingLost_; phase_ = Phase::Corrupt; return;
            }
            strong_ = true; flush();
        } else if(!strong && strong_) {
            strong_ = false; weakSince_ = nowUs;
        }
    }

    void poll(uint32_t nowUs) {
        if(phase_ != Phase::Capturing) return;
        if(!strong_) {
            if(uint32_t(nowUs - weakSince_) < endQuietUs_) return;
            if(!qualified()) { rearmShort(nowUs, true); return; }
            // RSSI stayed below threshold for the entire guard interval. This
            // explicitly excludes the deferred demodulator-noise tail, even if
            // its ring wrapped. Count all exclusions, rather than hide them.
            discardedTail_ += pendingTotal_; clearPending(); phase_ = Phase::Complete;
        } else if(uint32_t(nowUs - lastEdge_) >= endQuietUs_) {
            if(!qualified()) rearmShort(nowUs, false);
            else phase_ = Phase::Complete;
        }
    }

    // Manual stop / configured receive deadline. Ended intervals survive a
    // partial quiet window; a deferred overflow cannot be called complete.
    void stop(uint32_t nowUs) {
        poll(nowUs);
        if(phase_ == Phase::Capturing) {
            if(pendingLost_) { lostSamples_ += pendingLost_; phase_ = Phase::Corrupt; return; }
            flush(false); // Unconfirmed weak-RSSI tail cannot qualify a signal.
            if(phase_ == Phase::Capturing)
                phase_ = qualified() ? Phase::Complete : Phase::Stopped;
        } else if(phase_ == Phase::Armed) phase_ = Phase::Stopped;
    }

    Phase phase() const { return phase_; }
    bool terminal() const {
        return phase_ != Phase::Armed && phase_ != Phase::Capturing;
    }
    bool triggered() const { return triggered_; }
    bool complete() const { return phase_ == Phase::Complete; }
    bool full() const { return phase_ == Phase::Full; }
    size_t size() const { return used_; }
    size_t postTriggerPulses() const { return postTriggerPulses_; }
    bool rssiQualified() const { return rssiQualified_; }
    uint32_t dropped() const { return rawDropped_ + lostSamples_; }
    uint32_t rejected() const { return rawRejected_ + badSamples_; }
    uint32_t discardedTail() const { return discardedTail_; }
    uint32_t discardedArmingDropped() const { return discardedArmingDropped_; }
    uint32_t discardedArmingRejected() const { return discardedArmingRejected_; }
    uint32_t discardedShort() const { return discardedShort_; }
    uint32_t rearmCount() const { return rearms_; }

private:
    bool qualified() const { return postTriggerPulses_ >= kMinimumPulses && rssiQualified_; }
    void reject() {
        ++badSamples_;
        if(phase_ == Phase::Capturing) phase_ = Phase::Corrupt;
        else if(phase_ == Phase::Armed) { preQuiet_ = false; freshEdges_ = 0; }
    }
    void cleanArmedBoundary() {
        // Only an RSSI-quiet, loss-free armed window may retire stale noise.
        // Active capture damage never reaches this method.
        discardedArmingDropped_ += rawDropped_;
        discardedArmingRejected_ += rawRejected_ + badSamples_;
        droppedBase_ += rawDropped_; rejectedBase_ += rawRejected_;
        rawDropped_ = rawRejected_ = badSamples_ = 0;
        clearPending(); first_ = true; previousPositive_ = false; waitForWeak_ = false; freshEdges_ = 0;
    }
    void rearmShort(uint32_t nowUs, bool weakQuiet) {
        if(pendingLost_) { lostSamples_ += pendingLost_; phase_ = Phase::Corrupt; return; }
        discardedShort_ += uint32_t(used_) + pendingTotal_; ++rearms_;
        used_ = postTriggerPulses_ = 0; rssiQualified_ = false;
        clearPending(); first_ = true; previousPositive_ = false; freshEdges_ = 0;
        phase_ = Phase::Armed; strong_ = triggered_ = false;
        lastEdge_ = nowUs; preQuiet_ = weakQuiet; preQuietSince_ = nowUs;
        // An unmodulated strong carrier cannot continually retrigger an empty
        // candidate. A weak boundary OR a new four-edge train can rearm: a
        // transmitter may hold its carrier before the actual data starts.
        waitForWeak_ = !weakQuiet;
    }
    void remember(int32_t value, bool afterTrigger) {
        pre_[head_] = value; preQualifies_[head_] = afterTrigger;
        head_ = (head_ + 1) % kPretrigger;
        if(queued_ < kPretrigger) ++queued_;
        else if(phase_ == Phase::Capturing) ++pendingLost_;
        ++pendingTotal_;
    }
    void append(int32_t value, bool qualifies) {
        if(phase_ != Phase::Capturing) return;
        if(!used_ && value < 0) return; // Flipper RAW begins with a HIGH interval.
        if(used_ && (output_[used_ - 1] > 0) == (value > 0)) { reject(); return; }
        if(used_ >= capacity_) { phase_ = Phase::Full; return; }
        output_[used_++] = value;
        if(qualifies) ++postTriggerPulses_;
        if(used_ == capacity_) phase_ = Phase::Full;
    }
    void clearPending() { head_ = queued_ = 0; pendingTotal_ = pendingLost_ = 0; }
    void flush(bool allowQualification = true) {
        const size_t start = (head_ + kPretrigger - queued_) % kPretrigger;
        for(size_t i = 0; i < queued_ && phase_ == Phase::Capturing; ++i) {
            const size_t index = (start + i) % kPretrigger;
            append(pre_[index], allowQualification && preQualifies_[index]);
        }
        clearPending();
    }

    int32_t* output_;
    size_t capacity_, used_, postTriggerPulses_;
    int16_t threshold_;
    Phase phase_;
    int32_t pre_[kPretrigger];
    bool preQualifies_[kPretrigger];
    size_t head_, queued_;
    uint32_t pendingTotal_, pendingLost_, discardedTail_;
    uint32_t rawDropped_, rawRejected_, badSamples_, lostSamples_, droppedBase_, rejectedBase_;
    uint32_t discardedArmingDropped_, discardedArmingRejected_, discardedShort_, rearms_;
    uint32_t lastEdge_, weakSince_, preQuietSince_, triggeredAt_;
    uint32_t endQuietUs_; // Session burst grouping only; armed recovery stays7ms.
    uint8_t freshEdges_;
    bool first_, previousPositive_, strong_, triggered_, preQuiet_, waitForWeak_, rssiQualified_;
};
}
