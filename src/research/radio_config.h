#pragma once

#include <stddef.h>
#include <stdint.h>

// Pure C++11: no Arduino dependencies, allocation, or hardware access.
namespace research {

static const uint32_t kCrystalHz = 26000000UL;
static const size_t kRegisterCount = 47;
static const size_t kMaxPresetBytes = kRegisterCount * 2 + 10;

enum class Modulation : uint8_t { Fsk2 = 0, Gfsk = 1, AskOok = 3, Fsk4 = 4, Msk = 7 };
enum class PacketFormat : uint8_t { Fifo = 0, Async = 3 };

struct RegisterImage {
    uint8_t value[kRegisterCount];
    bool present[kRegisterCount];
    uint8_t pa[8];
    bool paPresent;
    RegisterImage();
    void set(uint8_t address, uint8_t data);
};

struct RadioPreset {
    const char* name;
    const char* flipperName;
    const uint8_t* data;
    size_t size;
};

struct RadioSettings {
    uint32_t frequencyHz;
    Modulation modulation;
    PacketFormat packetFormat;
    double bandwidthHz;
    double dataRateBaud;  // Symbol rate, not the two-bits/symbol 4-FSK bit rate.
    double deviationHz;  // Ignored for ASK/OOK; MSK uses mskPhase (not deviation).
    uint8_t mskPhase;
    uint8_t agc2, agc1, agc0;
    uint16_t syncWord;
    uint8_t syncMode;
    bool crc;
    bool whitening;
    bool manchester;
    bool fec;
    uint8_t packetLength; // 1..60: entire variable packet and status fit 64-byte FIFO.
    int16_t rssiThresholdDbm; // Host/UI threshold, not an absolute CC1101 register.
    uint32_t rxTimeoutMs;    // Host deadline, not WOR hardware timeout.
    uint32_t txLimitMs;      // Host deadline; application must enforce it.
    RadioSettings();
};

const RadioPreset* presetAt(size_t index);
size_t presetCount();
const RadioPreset* findPreset(const char* name);
// Exact source arrays include original PA values. This never authorizes TX.
bool loadPreset(const char* name, RegisterImage& image, const char** error = 0);
bool decodeCustomPreset(const uint8_t* data, size_t length, RegisterImage& image,
                        const char** error = 0);
size_t encodeCustomPreset(const RegisterImage& image, uint8_t* output, size_t capacity);
bool validateRegisterImage(const RegisterImage& image, const char** error = 0);
bool supportedFrequency(uint32_t hz); // Hardware bands only, not regional permission.
const char* modulationName(Modulation modulation);
double registerBandwidth(uint8_t mdmcfg4);
double registerDataRate(uint8_t mdmcfg4, uint8_t mdmcfg3);
double registerDeviation(uint8_t deviatn);
bool describeRadio(const RegisterImage& image, uint32_t frequencyHz, RadioSettings& settings,
                   const char** error = 0);
// Settings are rounded to nearest representable values. actual reports those values.
// Apply after chip reset; write only present[] and set frequency from actual.frequencyHz.
// PA table is intentionally zeroed: TX policy must install a permitted power table.
bool encodeRadioSettings(const RadioSettings& requested, RegisterImage& image,
                         RadioSettings* actual = 0, const char** error = 0);
bool validateSettings(const RadioSettings& settings, const char** error = 0);

} // namespace research
