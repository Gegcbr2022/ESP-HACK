#pragma once
#include <stdint.h>
struct FakeClockRadio {
    bool spiReady() const;
    uint8_t SpiReadReg(uint8_t address);
    uint8_t SpiReadStatus(uint8_t address);
    void SpiWriteReg(uint8_t address,uint8_t value);
    void SpiStrobe(uint8_t command);
};
extern FakeClockRadio ELECHOUSE_cc1101;
