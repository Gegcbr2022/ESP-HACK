#pragma once
#include <stddef.h>
#include <stdint.h>

namespace research {
struct RawTraceEntry {
    uint32_t atUs;
    int32_t value; // kind1=pulse us,2=RSSI dBm,3=phase/rearm count
    uint32_t used:12,post:12,kind:2,phase:3,rssiQualified:1,reserved:2;
};
static_assert(sizeof(RawTraceEntry)==12,"RAW debug trace must remain12 bytes");
struct RawTraceInfo {
    bool enabled=false,started=false,frozen=false;
    uint16_t count=0;
    uint32_t dropped=0; // overwritten oldest entries, never hidden
};
// Task-only bounded ring; caller owns optional internal bank and synchronization.
class RawTrace {
public:
    void bind(RawTraceEntry* bank,size_t capacity) {
        bank_=bank;capacity_=capacity;info_=RawTraceInfo{};head_=0;
    }
    void enable(bool enabled) {
        if(enabled) { info_=RawTraceInfo{};head_=0;info_.enabled=bank_&&capacity_; }
        else { info_.enabled=false;info_.frozen=true; }
    }
    void reset() { if(info_.enabled)enable(true); }
    void freeze() { info_.frozen=true; }
    RawTraceInfo info() const { return info_; }
    void record(uint8_t kind,uint32_t atUs,int32_t value,uint8_t phase,
                size_t used,size_t post,bool qualified,bool candidate) {
        if(!info_.enabled||info_.frozen||!bank_||!capacity_)return;
        if(candidate)info_.started=true;
        RawTraceEntry& entry=bank_[head_];
        entry.atUs=atUs;entry.value=value;entry.kind=kind;entry.phase=phase;
        entry.used=used>4095?4095:used;entry.post=post>4095?4095:post;
        entry.rssiQualified=qualified;entry.reserved=0;
        head_=(head_+1)%capacity_;
        if(info_.count<capacity_)++info_.count;else ++info_.dropped;
    }
    bool copy(size_t index,RawTraceEntry& entry) const {
        if(!info_.frozen||!bank_||index>=info_.count)return false;
        const size_t first=(head_+capacity_-info_.count)%capacity_;
        entry=bank_[(first+index)%capacity_];return true;
    }
private:
    RawTraceEntry* bank_=nullptr;
    size_t capacity_=0,head_=0;
    RawTraceInfo info_{};
};
}
