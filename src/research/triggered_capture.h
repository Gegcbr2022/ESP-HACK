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

    TriggeredCapture(int32_t* output, size_t capacity, int16_t threshold, uint32_t startedUs)
        : output_(output), capacity_(capacity), used_(0), threshold_(threshold),
          phase_(output && capacity ? Phase::Armed : Phase::Corrupt), head_(0), queued_(0),
          pendingTotal_(0), pendingLost_(0), discardedTail_(0), rawDropped_(0),
          rawRejected_(0), badSamples_(0), lostSamples_(0), lastEdge_(startedUs), weakSince_(0),
          first_(true), previousPositive_(false), strong_(false), triggered_(false) {}

    void integrity(uint32_t dropped, uint32_t rejected) {
        rawDropped_ = dropped; rawRejected_ = rejected;
        if((dropped || rejected) && (phase_ == Phase::Capturing || phase_ == Phase::Complete))
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
        if(phase_ == Phase::Armed || !strong_) remember(value);
        else append(value);
    }

    void rssi(int16_t dbm, uint32_t nowUs) {
        if(terminal()) return;
        // Honor a quiet window even if a later RSSI sample arrives after the
        // worker was delayed; returning energy starts a different burst.
        poll(nowUs);
        if(terminal()) return;
        const bool strong = dbm >= threshold_;
        if(phase_ == Phase::Armed) {
            if(!strong) return;
            triggered_ = true;
            if(rawDropped_ || rawRejected_ || badSamples_) { phase_ = Phase::Corrupt; return; }
            phase_ = Phase::Capturing; strong_ = true;
            flush(); // Preserve up to64 preceding ended intervals, in time order.
            return;
        }
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
        if(phase_ != Phase::Capturing || used_ < kMinimumPulses) return;
        if(!strong_) {
            if(uint32_t(nowUs - weakSince_) < kQuietUs) return;
            // RSSI stayed below threshold for the entire guard interval. This
            // explicitly excludes the deferred demodulator-noise tail, even if
            // its ring wrapped. Count all exclusions, rather than hide them.
            discardedTail_ += pendingTotal_; clearPending(); phase_ = Phase::Complete;
        } else if(uint32_t(nowUs - lastEdge_) >= kQuietUs) {
            phase_ = Phase::Complete;
        }
    }

    // Manual stop / configured receive deadline. Ended intervals survive a
    // partial quiet window; a deferred overflow cannot be called complete.
    void stop(uint32_t nowUs) {
        poll(nowUs);
        if(phase_ == Phase::Capturing) {
            if(pendingLost_) { lostSamples_ += pendingLost_; phase_ = Phase::Corrupt; return; }
            flush();
            if(phase_ == Phase::Capturing)
                phase_ = used_ >= kMinimumPulses ? Phase::Complete : Phase::Stopped;
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
    uint32_t dropped() const { return rawDropped_ + lostSamples_; }
    uint32_t rejected() const { return rawRejected_ + badSamples_; }
    uint32_t discardedTail() const { return discardedTail_; }

private:
    void reject() {
        ++badSamples_;
        if(phase_ == Phase::Capturing) phase_ = Phase::Corrupt;
    }
    void remember(int32_t value) {
        pre_[head_] = value; head_ = (head_ + 1) % kPretrigger;
        if(queued_ < kPretrigger) ++queued_;
        else if(phase_ == Phase::Capturing) ++pendingLost_;
        ++pendingTotal_;
    }
    void append(int32_t value) {
        if(phase_ != Phase::Capturing) return;
        if(!used_ && value < 0) return; // Flipper RAW begins with a HIGH interval.
        if(used_ && (output_[used_ - 1] > 0) == (value > 0)) { reject(); return; }
        if(used_ >= capacity_) { phase_ = Phase::Full; return; }
        output_[used_++] = value;
        if(used_ == capacity_) phase_ = Phase::Full;
    }
    void clearPending() { head_ = queued_ = 0; pendingTotal_ = pendingLost_ = 0; }
    void flush() {
        const size_t start = (head_ + kPretrigger - queued_) % kPretrigger;
        for(size_t i = 0; i < queued_ && phase_ == Phase::Capturing; ++i)
            append(pre_[(start + i) % kPretrigger]);
        clearPending();
    }

    int32_t* output_;
    size_t capacity_, used_;
    int16_t threshold_;
    Phase phase_;
    int32_t pre_[kPretrigger];
    size_t head_, queued_;
    uint32_t pendingTotal_, pendingLost_, discardedTail_;
    uint32_t rawDropped_, rawRejected_, badSamples_, lostSamples_, lastEdge_, weakSince_;
    bool first_, previousPositive_, strong_, triggered_;
};
}
