#pragma once
#include <stddef.h>
#include <stdint.h>

namespace research {
// A physical snapshot, not the most recently requested preset. Backend I/O
// reports new SPI failures without clearing the driver's sticky fault flag.
// No allocation, worker/ISR access, frequency arithmetic, or TX strobe here.
template<class Backend> class RawRadioRestore {
public:
    explicit RawRadioRestore(Backend& backend): b_(backend) {}
    ~RawRadioRestore() { if(dirty_ && !finished_) finish(); }
    bool capture() {
        if(!b_.ready() || !b_.readState(previous_)) return false;
        if(previous_!=1 && previous_!=13) {
            b_.idle(); // Never resume a pre-existing TX or unknown transition.
            return false;
        }
        b_.savePin(pin_);
        for(size_t i=0;i<47;++i) if(!b_.readReg(uint8_t(i),regs_[i])) return false;
        if(!b_.readPa(pa_)) return false;
        uint8_t after;
        if(!b_.readState(after) || after!=previous_) return false;
        captured_=true;
        return true;
    }
    void mutating() { dirty_=captured_; }
    bool finish() {
        if(finished_) return restored_;
        finished_=true;
        if(!dirty_) return captured_;
        bool ok=b_.stopRmt();
        b_.safePin();
        bool inIdle=b_.idle();
        if(!inIdle) {
            ok=false;
            inIdle=b_.idle(); // One bounded retry; config is never written in TX.
        }
        const uint8_t zero[8]={};
        if(!b_.writePa(zero)) ok=false;
        // Disable entry autocal temporarily: saved FSCAL and the already
        // trimmed frequency word must survive RX re-entry byte-for-byte.
        if(inIdle) for(size_t i=0;i<47;++i) {
            const uint8_t value=i==0x18?uint8_t(regs_[i]&~0x30):regs_[i];
            if(!b_.writeReg(uint8_t(i),value)) ok=false;
        }
        if(ok && previous_==13 && !b_.enterRx()) ok=false;
        if(inIdle && !b_.writeReg(0x18,regs_[0x18])) ok=false;
        for(size_t i=0;i<47;++i) {
            uint8_t value;
            if(!b_.readReg(uint8_t(i),value) || value!=regs_[i]) ok=false;
        }
        // PA remains zero throughout rearm/readback. Only restore it after
        // successful non-transmitting RX/IDLE entry and exact config checks.
        if(ok && !b_.writePa(pa_)) ok=false;
        uint8_t actualPa[8]={}, actualState=0;
        if(ok && !b_.readPa(actualPa)) ok=false;
        if(ok) for(size_t i=0;i<8;++i) if(actualPa[i]!=pa_[i]) ok=false;
        if(ok && (!b_.readState(actualState) || actualState!=previous_)) ok=false;
        if(!ok) {
            b_.writePa(zero);
            b_.idle();
        }
        b_.restorePin(pin_);
        restored_=ok;
        b_.restored(ok);
        return ok;
    }
private:
    Backend& b_;
    typename Backend::PinState pin_{};
    uint8_t regs_[47]={}, pa_[8]={}, previous_=0;
    bool captured_=false, dirty_=false, finished_=false, restored_=false;
    RawRadioRestore(const RawRadioRestore&)=delete;
    RawRadioRestore& operator=(const RawRadioRestore&)=delete;
};
}
