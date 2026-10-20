#pragma once
#include <stddef.h>
#include <stdint.h>

namespace research {

// Task-context startup measurement only; no GPIO, SPI, allocation or waiting.
// The caller samples RSSI before attaching the waveform interrupt. A ready gate
// reports the measured noise floor and a separate threshold for this session.
class RawStartupGate {
public:
    static const uint8_t kSamples = 64;
    static const uint32_t kSampleIntervalUs = 4000;
    static const uint32_t kDeadlineUs = 512000;

    explicit RawStartupGate(uint32_t startedUs)
        : started_(startedUs), last_(startedUs), count_(0), ready_(false),
          failed_(false), noise_(-127), threshold_(-90) {}

    bool due(uint32_t nowUs) const {
        return !ready_ && !failed_ &&
               (uint32_t(nowUs - started_) >= kDeadlineUs || !count_ ||
                uint32_t(nowUs - last_) >= kSampleIntervalUs);
    }

    void sample(int16_t dbm, uint32_t nowUs) {
        if(ready_ || failed_) return;
        if(uint32_t(nowUs - started_) >= kDeadlineUs) { failed_ = true; return; }
        if(!due(nowUs)) return;
        values_[count_++] = dbm; last_ = nowUs;
        if(count_ != kSamples) return;
        // Bounded insertion sort: 64 values, independent of signal amplitude.
        for(size_t i = 1; i < kSamples; ++i) {
            const int16_t value = values_[i]; size_t j = i;
            while(j && values_[j - 1] > value) { values_[j] = values_[j - 1]; --j; }
            values_[j] = value;
        }
        // Average the middle pair in widened arithmetic (round toward zero).
        noise_ = int16_t((int32_t(values_[31]) + values_[32]) / 2);
        const int32_t threshold = int32_t(noise_) + 10;
        threshold_ = int16_t(threshold < -100 ? -100 : threshold > -45 ? -45 : threshold);
        ready_ = true;
    }

    bool ready() const { return ready_; }
    bool failed() const { return failed_; }
    uint8_t samples() const { return count_; }
    int16_t noiseDbm() const { return noise_; }
    int16_t thresholdDbm() const { return threshold_; }

private:
    uint32_t started_, last_;
    uint8_t count_;
    bool ready_, failed_;
    int16_t noise_, threshold_, values_[kSamples];
};
}
