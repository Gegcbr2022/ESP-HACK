#pragma once
#include "lab_limits.h"
#include <stddef.h>
#include <stdint.h>
#include <string.h>

namespace research {
namespace capture_history {

static const size_t kSlots = 4;
static const size_t kPulsesPerSlot = 2048;
static const size_t kBankPulses = kSlots * kPulsesPerSlot;

struct CandidateStatus {
    bool complete;
    bool full;
    uint32_t dropped;
    uint32_t rejected;
};

struct Entry {
    bool valid;
    size_t count;
    uint32_t frequencyHz;
    uint64_t durationUs;
    int16_t rssiDbm; // Highest observed RSSI among this entry's duplicate captures.
    uint32_t hash;  // FNV-1a of the original signed intervals, little-endian bytes.
    uint32_t observations;
};

enum class AddResult : uint8_t { Added, Duplicate, Full, NoBank, Invalid };

// Bounded borrowed-memory model. The owner allocates a bank lazily and keeps it
// alive while bound. This class allocates nothing and performs no hardware I/O.
// The owner must serialize access; radio/preset snapshots belong to that owner,
// indexed by the same slot. Accepted payloads are immutable until clear/unbind.
class History {
public:
    History() : bank_(nullptr), used_(0), entries_{} {}

    bool bind(int32_t* bank, size_t intervalCapacity) {
        if(!bank || intervalCapacity < kBankPulses ||
           reinterpret_cast<uintptr_t>(bank) % alignof(int32_t)) return false;
        if(used_ && bank != bank_) return false;
        bank_ = bank; return true;
    }

    void clear() {
        used_ = 0;
        for(size_t i = 0; i < kSlots; ++i) entries_[i] = Entry{};
    }
    void unbind() { clear(); bank_ = nullptr; }
    size_t size() const { return used_; }
    bool bound() const { return bank_ != nullptr; }
    const Entry* entry(size_t index) const { return index < used_ ? &entries_[index] : nullptr; }
    const int32_t* pulses(size_t index) const {
        return bank_ && index < used_ ? bank_ + index * kPulsesPerSlot : nullptr;
    }

    AddResult add(const int32_t* input, size_t count, uint32_t frequencyHz,
                  int16_t rssiDbm, const CandidateStatus& status, size_t* index = nullptr) {
        if(!input || !count || count > kPulsesPerSlot || !status.complete || status.full ||
           status.dropped || status.rejected || !cc1101FrequencyWord(frequencyHz))
            return AddResult::Invalid;
        uint64_t duration = 0; uint32_t hash = 2166136261UL;
        for(size_t i = 0; i < count; ++i) {
            const int32_t value = input[i];
            if(!value || value == INT32_MIN || (value > 0) != !(i & 1))
                return AddResult::Invalid;
            duration += value > 0 ? uint32_t(value) : uint32_t(-value);
            const uint32_t bits = uint32_t(value);
            for(unsigned byte = 0; byte < 4; ++byte) {
                hash ^= uint8_t(bits >> (byte * 8)); hash *= 16777619UL;
            }
        }
        if(!bank_) return AddResult::NoBank;
        for(size_t slot = 0; slot < used_; ++slot) {
            Entry& previous = entries_[slot];
            if(previous.frequencyHz != frequencyHz || previous.count != count ||
               !sameWave(bank_ + slot * kPulsesPerSlot, input, count)) continue;
            if(previous.observations != UINT32_MAX) ++previous.observations;
            if(rssiDbm > previous.rssiDbm) previous.rssiDbm = rssiDbm;
            if(index) *index = slot;
            return AddResult::Duplicate;
        }
        if(used_ == kSlots) return AddResult::Full;
        const size_t slot = used_;
        // memmove also permits an owner to submit an existing borrowed span.
        memmove(bank_ + slot * kPulsesPerSlot, input, count * sizeof(*input));
        Entry value = {true, count, frequencyHz, duration, rssiDbm, hash, 1};
        entries_[slot] = value; ++used_;
        if(index) *index = slot;
        return AddResult::Added;
    }

private:
    static bool sameWave(const int32_t* a, const int32_t* b, size_t count) {
        for(size_t i = 0; i < count; ++i) {
            if((a[i] > 0) != (b[i] > 0)) return false;
            const uint32_t first = a[i] > 0 ? uint32_t(a[i]) : uint32_t(-a[i]);
            const uint32_t second = b[i] > 0 ? uint32_t(b[i]) : uint32_t(-b[i]);
            const uint32_t smaller = first < second ? first : second;
            const uint32_t difference = first > second ? first - second : second - first;
            // Symmetric absolute + relative tolerance; hash differences do not
            // reject jittered repeats. Different count/frequency is not a repeat.
            if(difference > 40UL + smaller / 5) return false;
        }
        return true;
    }

    int32_t* bank_;
    size_t used_;
    Entry entries_[kSlots];
};
}
}
