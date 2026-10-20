#pragma once
#include <stddef.h>
#include <stdint.h>

namespace research {
// A physical snapshot, not the most recently requested preset. Backend I/O
// reports new SPI failures without clearing the driver's sticky fault flag.
// No allocation, worker/ISR access, frequency arithmetic, or TX strobe here.
// TI SWRS061I p91 forbids writes to FSTEST(29)/AGCTEST(2B). AGCTEST also
// changes while RX is active, so snapshot it for context but never restore or
// compare those two test registers. All45 remaining config bytes stay strict.
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
        if(!ok)b_.failure("rmt",0,0,0);
        b_.safePin();
        bool inIdle=b_.idle();
        if(!inIdle) {
            b_.failure("idle",0x35,1,0);
            ok=false;
            inIdle=b_.idle(); // One bounded retry; config is never written in TX.
        }
        const uint8_t zero[8]={};
        if(!b_.writePa(zero)) { b_.failure("pa-zero",0x3E,0,0);ok=false; }
        // Disable entry autocal temporarily: saved FSCAL and the already
        // trimmed frequency word must survive RX re-entry byte-for-byte.
        if(inIdle) for(size_t i=0;i<47;++i) {
            if(i==0x29 || i==0x2B)continue;
            const uint8_t value=i==0x18?uint8_t(regs_[i]&~0x30):regs_[i];
            if(!b_.writeReg(uint8_t(i),value)) { b_.failure("reg-write",uint8_t(i),value,0);ok=false; }
        }
        if(ok && previous_==13 && !b_.enterRx()) { b_.failure("rx-entry",0x35,13,0);ok=false; }
        if(inIdle && !b_.writeReg(0x18,regs_[0x18])) { b_.failure("mcsm0",0x18,regs_[0x18],0);ok=false; }
        for(size_t i=0;i<47;++i) {
            if(i==0x29 || i==0x2B)continue;
            uint8_t value=0;
            if(!b_.readReg(uint8_t(i),value)) { b_.failure("reg-read",uint8_t(i),regs_[i],value);ok=false; }
            else if(value!=regs_[i]) { b_.failure("reg-mismatch",uint8_t(i),regs_[i],value);ok=false; }
        }
        // PA remains zero throughout rearm/readback. Only restore it after
        // successful non-transmitting RX/IDLE entry and exact config checks.
        if(ok && !b_.writePa(pa_)) { b_.failure("pa-write",0x3E,pa_[0],0);ok=false; }
        uint8_t actualPa[8]={}, actualState=0;
        if(ok && !b_.readPa(actualPa)) { b_.failure("pa-read",0x3E,0,0);ok=false; }
        if(ok) for(size_t i=0;i<8;++i) if(actualPa[i]!=pa_[i]) { b_.failure("pa-mismatch",uint8_t(i),pa_[i],actualPa[i]);ok=false; }
        if(ok && (!b_.readState(actualState) || actualState!=previous_)) { b_.failure("state",0x35,previous_,actualState);ok=false; }
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
