#pragma once
#include <stdint.h>
#define MSBFIRST 1
#define SPI_MODE0 0
struct SPISettings { SPISettings(uint32_t, uint8_t, uint8_t) {} };
struct MockSPI {
    void begin(int = -1, int = -1, int = -1, int = -1);
    void beginTransaction(const SPISettings&);
    void endTransaction();
    uint8_t transfer(uint8_t data);
};
extern MockSPI SPI;
