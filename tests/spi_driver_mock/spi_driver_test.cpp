#include "SmartRC_CC1101.h"
#include <stdio.h>
#include <string.h>

static uint32_t clock_us, yielded, transfers, begins, ends, bad_transactions;
static uint32_t high_after_transfer, ready_at;
static bool miso_high, transaction;
static uint8_t pins[64], transferred[512];
static unsigned checks;
#define CHECK(x) do { ++checks; if(!(x)) { fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x); return 1; } } while(0)

uint32_t micros() { return clock_us++; }
uint32_t millis() { return clock_us / 1000u; }
void delay(uint32_t value) { clock_us += value * 1000u; }
void delayMicroseconds(uint32_t value) { clock_us += value; }
void yield() { ++yielded; clock_us += 250; }
int digitalRead(uint8_t pin) {
    if(pin == 19) {
        if(pins[5] == HIGH) return HIGH; // SO high-impedance with external pull-up
        return miso_high && (!ready_at || (int32_t)(clock_us - ready_at) < 0);
    }
    return pin < 64 ? pins[pin] : 0;
}
int gpio_get_level(gpio_num_t pin) { return digitalRead((uint8_t)pin); }
void digitalWrite(uint8_t pin, uint8_t level) { if(pin < 64) pins[pin] = level; }
void pinMode(uint8_t, uint8_t) {}
void MockSPI::begin(int, int, int, int) {}
void MockSPI::beginTransaction(const SPISettings&) {
    if(transaction) ++bad_transactions;
    transaction = true; ++begins;
}
void MockSPI::endTransaction() {
    if(!transaction) ++bad_transactions;
    transaction = false; ++ends;
}
uint8_t MockSPI::transfer(uint8_t data) {
    if(!transaction || pins[5] != LOW) ++bad_transactions;
    if(transfers < sizeof(transferred)) transferred[transfers] = data;
    ++transfers;
    if(high_after_transfer && transfers >= high_after_transfer) miso_high = true;
    return 0xA5;
}
MockSPI SPI;

static void reset(bool high = false) {
    clock_us = yielded = transfers = begins = ends = bad_transactions = 0;
    high_after_transfer = ready_at = 0; miso_high = high; transaction = false;
    memset(pins, 0, sizeof(pins)); memset(transferred, 0, sizeof(transferred)); pins[5] = HIGH;
}

int main() {
    SmartRC_CC1101 radio;
    radio.setSpiPin(18, 19, 23, 5);
    reset(true);
    uint32_t start = clock_us;
    CHECK(radio.SpiReadReg(CC1101_VERSION) == 0);
    CHECK(clock_us - start >= 5000 && clock_us - start < 5500);
    CHECK(yielded > 0 && transfers == 0 && begins == 1 && ends == 1);
    CHECK(pins[5] == HIGH && !transaction && !bad_transactions);
    CHECK(radio.spiTimeouts() == 1 && !radio.spiReady());

    byte buffer[8]; memset(buffer, 0xCC, sizeof(buffer));
    radio.SpiReadBurstReg(CC1101_RXFIFO, buffer, sizeof(buffer));
    for(size_t i = 0; i < sizeof(buffer); ++i) CHECK(buffer[i] == 0);
    CHECK(radio.spiTimeouts() == 2 && transfers == 0 && begins == ends && pins[5] == HIGH);
    radio.SpiWriteReg(1, 2); radio.SpiStrobe(CC1101_SRX); radio.SpiWriteBurstReg(CC1101_PATABLE, buffer, sizeof(buffer));
    CHECK(radio.spiTimeouts() == 5 && transfers == 0 && begins == ends && pins[5] == HIGH);

    // Fault is sticky for diagnostics, but later independent transactions can recover.
    miso_high = false;
    CHECK(radio.SpiReadStatus(CC1101_MARCSTATE) == 0xA5);
    CHECK(transfers == 2 && transferred[0] == (CC1101_MARCSTATE | 0xC0));
    CHECK(radio.spiTimeouts() == 5 && !radio.spiReady());
    memset(buffer, 0xCC, sizeof(buffer)); radio.SpiReadBurstReg(CC1101_RXFIFO, buffer, sizeof(buffer));
    for(size_t i = 0; i < sizeof(buffer); ++i) CHECK(buffer[i] == 0xA5);
    CHECK(begins == ends && !bad_transactions);

    // microsecond counter wrap cannot extend the5ms deadline.
    reset(true); clock_us = 0xFFFFFF00u; start = clock_us;
    CHECK(radio.SpiReadReg(1) == 0);
    CHECK(uint32_t(clock_us - start) >= 5000 && uint32_t(clock_us - start) < 5500);
    CHECK(pins[5] == HIGH && begins == ends && !bad_transactions);

    // Ready becoming low just before the deadline permits a normal transaction.
    reset(true); ready_at = 4750;
    CHECK(radio.SpiReadReg(1) == 0xA5); CHECK(transfers == 2 && clock_us >= 4750 && clock_us < 5000);
    CHECK(pins[5] == HIGH && begins == ends && !bad_transactions);

    // First reset-ready failure aborts Init without register configuration.
    SmartRC_CC1101 absent; absent.setSpiPin(18, 19, 23, 5); reset(true);
    absent.Init();
    CHECK(absent.spiTimeouts() == 1 && !absent.spiReady());
    CHECK(transfers == 0 && yielded > 0 && pins[5] == HIGH && !transaction);
    CHECK(clock_us >= 205000 && clock_us < 206000); // retained upstream200ms startup delay + one5ms wait

    // The post-SRES ready wait is bounded and releases the active transaction.
    reset(); high_after_transfer = 1;
    absent.Init();
    CHECK(absent.spiTimeouts() == 2 && !absent.spiReady());
    CHECK(transfers == 1 && begins == ends && !bad_transactions && pins[5] == HIGH);

    // Disconnect after reset+first configwrite: subsequent nested setters must skip SPI.
    SmartRC_CC1101 disconnected; disconnected.setSpiPin(18, 19, 23, 5); reset(); high_after_transfer = 3;
    disconnected.Init();
    CHECK(disconnected.spiTimeouts() == 1 && !disconnected.spiReady());
    CHECK(transfers == 3 && begins == ends && !bad_transactions && pins[5] == HIGH);
    CHECK(clock_us < 206000);

    // A successful explicit Init clears the sticky fault, preserving error history.
    reset(); disconnected.Init();
    CHECK(disconnected.spiReady() && disconnected.spiTimeouts() == 1);
    CHECK(transfers > 30 && begins == ends && !bad_transactions && pins[5] == HIGH);
    printf("spi_driver: PASS (%u checks; 5ms deadlines, wrap, yield, CS release, zeroed reads, Init abort)\n", checks);
    return 0;
}
