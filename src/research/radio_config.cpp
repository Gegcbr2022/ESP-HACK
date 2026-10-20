#include "radio_config.h"
#include <math.h>
#include <string.h>

namespace research {
namespace {
#include "flipper_presets.inc"
const RadioPreset presets[] = {
    {"AM270", "FuriHalSubGhzPresetOok270Async", am270, sizeof am270},
    {"AM650", "FuriHalSubGhzPresetOok650Async", am650, sizeof am650},
    {"FM238", "FuriHalSubGhzPreset2FSKDev238Async", fm238, sizeof fm238},
    {"FM476", "FuriHalSubGhzPreset2FSKDev476Async", fm476, sizeof fm476},
    {"FM12K", "FuriHalSubGhzPreset2FSKDev12KAsync", fm12k, sizeof fm12k},
};
bool invalid(const char** error, const char* why) {
    if(error) *error = why;
    return false;
}
uint8_t valueOr(const RegisterImage& image, uint8_t addr, uint8_t reset) {
    return image.present[addr] ? image.value[addr] : reset;
}
bool validModulation(Modulation modulation) {
    return modulation == Modulation::Fsk2 || modulation == Modulation::Gfsk ||
           modulation == Modulation::AskOok || modulation == Modulation::Fsk4 ||
           modulation == Modulation::Msk;
}
double maxBaud(Modulation modulation) {
    if(modulation == Modulation::Gfsk || modulation == Modulation::AskOok) return 250000.0;
    if(modulation == Modulation::Fsk4) return 300000.0;
    return 500000.0;
}
bool finiteNumber(double number) { return isfinite(number); }
}

RegisterImage::RegisterImage() : paPresent(false) {
    memset(value, 0, sizeof value);
    memset(present, 0, sizeof present);
    memset(pa, 0, sizeof pa);
}
void RegisterImage::set(uint8_t address, uint8_t data) {
    if(address < kRegisterCount) { value[address] = data; present[address] = true; }
}
RadioSettings::RadioSettings()
    : frequencyHz(433920000UL), modulation(Modulation::Fsk2), packetFormat(PacketFormat::Fifo),
      bandwidthHz(203125.0), dataRateBaud(9600.0), deviationHz(19042.96875), mskPhase(7),
      agc2(0x07), agc1(0x00), agc0(0x91), syncWord(0xE5A7), syncMode(2), crc(true),
      whitening(false), manchester(false), fec(false), packetLength(60),
      rssiThresholdDbm(-90), rxTimeoutMs(5000), txLimitMs(1000) {}

size_t presetCount() { return sizeof presets / sizeof presets[0]; }
const RadioPreset* presetAt(size_t index) { return index < presetCount() ? &presets[index] : 0; }
const RadioPreset* findPreset(const char* name) {
    if(!name) return 0;
    for(size_t i = 0; i < presetCount(); ++i)
        if(!strcmp(name, presets[i].name) || !strcmp(name, presets[i].flipperName)) return &presets[i];
    return 0;
}
bool loadPreset(const char* name, RegisterImage& image, const char** error) {
    const RadioPreset* preset = findPreset(name);
    if(!preset) return invalid(error, "Unknown radio preset; no substitution");
    return decodeCustomPreset(preset->data, preset->size, image, error);
}
bool supportedFrequency(uint32_t hz) {
    return (hz >= 300000000UL && hz <= 348000000UL) ||
           (hz >= 387000000UL && hz <= 464000000UL) ||
           (hz >= 779000000UL && hz <= 928000000UL);
}
const char* modulationName(Modulation modulation) {
    switch(modulation) {
    case Modulation::Fsk2: return "2-FSK";
    case Modulation::Gfsk: return "GFSK";
    case Modulation::AskOok: return "ASK/OOK";
    case Modulation::Fsk4: return "4-FSK";
    case Modulation::Msk: return "MSK";
    }
    return "Unsupported";
}
double registerBandwidth(uint8_t cfg) {
    return double(kCrystalHz) / (8.0 * (4 + ((cfg >> 4) & 3)) * (1U << (cfg >> 6)));
}
double registerDataRate(uint8_t cfg4, uint8_t cfg3) {
    return double(kCrystalHz) * (256.0 + cfg3) * (1UL << (cfg4 & 15)) / 268435456.0;
}
double registerDeviation(uint8_t dev) {
    return double(kCrystalHz) * (8 + (dev & 7)) * (1U << ((dev >> 4) & 7)) / 131072.0;
}

bool decodeCustomPreset(const uint8_t* data, size_t length, RegisterImage& image, const char** error) {
    if(!data || length < 10 || length > kMaxPresetBytes || (length & 1))
        return invalid(error, "Custom preset length is invalid");
    RegisterImage candidate;
    size_t at = 0;
    bool terminated = false;
    while(at + 1 < length) {
        const uint8_t address = data[at++], value = data[at++];
        if(address == 0 && value == 0) { terminated = true; break; }
        if(address == 0) return invalid(error, "Flipper reserves address zero as preset terminator");
        if(address >= kRegisterCount) return invalid(error, "Preset writes a strobe/status/FIFO address");
        if(candidate.present[address]) return invalid(error, "Duplicate custom register");
        candidate.set(address, value);
    }
    if(!terminated || length - at != 8) return invalid(error, "Preset requires terminator plus eight PA bytes");
    memcpy(candidate.pa, data + at, 8);
    candidate.paPresent = true;
    if(!validateRegisterImage(candidate, error)) return false;
    image = candidate;
    if(error) *error = 0;
    return true;
}
size_t encodeCustomPreset(const RegisterImage& image, uint8_t* output, size_t capacity) {
    if(!output || !validateRegisterImage(image)) return 0;
    size_t need = 10;
    for(size_t i = 0; i < kRegisterCount; ++i) if(image.present[i]) need += 2;
    if(capacity < need || image.present[0]) return 0;
    size_t at = 0;
    for(size_t i = 0; i < kRegisterCount; ++i) if(image.present[i]) {
        output[at++] = uint8_t(i); output[at++] = image.value[i];
    }
    output[at++] = 0; output[at++] = 0;
    memcpy(output + at, image.pa, 8);
    return at + 8;
}

bool validateRegisterImage(const RegisterImage& image, const char** error) {
    if(!image.present[0x08] || !image.present[0x10] || !image.present[0x11] || !image.present[0x12])
        return invalid(error, "Preset lacks packet/modem registers");
    if(!image.paPresent) return invalid(error, "Preset lacks PA table");
    // On the shared SPI bus GDO1/SO must release MISO while CSn is high.
    if(image.present[1] && (image.value[1] & 0x3F) != 0x2E)
        return invalid(error, "GDO1 must be high impedance on shared SPI");
    if((image.present[0x0A] && image.value[0x0A]) || (image.present[0x0C] && image.value[0x0C]))
        return invalid(error, "Channel/frequency offsets are unsupported; use Frequency header");
    const uint8_t cfg0 = image.value[0x08], cfg2 = image.value[0x12];
    if(cfg0 & 0x88) return invalid(error, "Reserved packet control bits are set");
    const Modulation mod = Modulation((cfg2 >> 4) & 7);
    if(!validModulation(mod)) return invalid(error, "Reserved modulation format");
    const uint8_t format = (cfg0 >> 4) & 3;
    if(format != 0 && format != 3) return invalid(error, "Only FIFO or asynchronous packet formats supported");
    if((cfg0 & 3) == 3) return invalid(error, "Reserved packet length mode");
    const bool fec = (valueOr(image, 0x13, 0x22) & 0x80) != 0;
    const bool manchester = (cfg2 & 8) != 0;
    if(format == 3) {
        if(mod == Modulation::Msk) return invalid(error, "MSK is unavailable in asynchronous mode");
        if(mod == Modulation::Fsk4) return invalid(error, "4-FSK requires FIFO mode in this driver");
        if(fec || manchester || (cfg0 & 0x44))
            return invalid(error, "Asynchronous mode cannot use CRC, whitening, Manchester or FEC");
        if(!image.present[2] || image.value[2] != 0x0D)
            return invalid(error, "Async preset must route serial data to GDO0");
    } else {
        if(!image.present[2] || image.value[2] != 0x06)
            return invalid(error, "FIFO preset must route sync/end-of-packet to GDO0");
        if((cfg0 & 3) != 1 || !image.present[6] || !image.value[6] || image.value[6] > 60)
            return invalid(error, "This FIFO driver requires variable packets of 1..60 bytes");
        if(!(cfg2 & 3) || fec)
            return invalid(error, "FIFO driver requires sync and disables FEC");
    }
    if(manchester && (fec || mod == Modulation::Msk || mod == Modulation::Fsk4))
        return invalid(error, "Invalid Manchester/FEC/modulation combination");
    const double rate = registerDataRate(image.value[0x10], image.value[0x11]);
    if(rate < 600.0 || rate > maxBaud(mod)) return invalid(error, "Symbol rate exceeds modulation limits");
    if(mod == Modulation::Msk && rate <= 26000.0) return invalid(error, "MSK requires over 26 kBaud");
    if((cfg2 & 0x80) && rate > 250000.0)
        return invalid(error, "DC filter cannot be disabled above 250 kBaud");
    if(error) *error = 0;
    return true;
}

bool describeRadio(const RegisterImage& image, uint32_t frequency, RadioSettings& settings, const char** error) {
    if(!supportedFrequency(frequency)) return invalid(error, "Frequency outside CC1101 hardware bands");
    if(!validateRegisterImage(image, error)) return false;
    RadioSettings result;
    result.frequencyHz = frequency;
    result.modulation = Modulation((image.value[0x12] >> 4) & 7);
    result.packetFormat = PacketFormat((image.value[0x08] >> 4) & 3);
    result.bandwidthHz = registerBandwidth(image.value[0x10]);
    result.dataRateBaud = registerDataRate(image.value[0x10], image.value[0x11]);
    result.deviationHz = result.modulation == Modulation::AskOok || result.modulation == Modulation::Msk
        ? 0.0 : registerDeviation(valueOr(image, 0x15, 0x47));
    result.mskPhase = valueOr(image, 0x15, 0x47) & 7;
    result.agc2 = valueOr(image, 0x1B, 0x03);
    result.agc1 = valueOr(image, 0x1C, 0x40);
    result.agc0 = valueOr(image, 0x1D, 0x91);
    result.syncWord = uint16_t(valueOr(image, 4, 0xD3)) << 8 | valueOr(image, 5, 0x91);
    result.syncMode = image.value[0x12] & 7;
    result.crc = (image.value[0x08] & 4) != 0;
    result.whitening = (image.value[0x08] & 0x40) != 0;
    result.manchester = (image.value[0x12] & 8) != 0;
    result.fec = (valueOr(image, 0x13, 0x22) & 0x80) != 0;
    result.packetLength = valueOr(image, 6, 60);
    settings = result;
    return true;
}

bool validateSettings(const RadioSettings& s, const char** error) {
    if(!supportedFrequency(s.frequencyHz)) return invalid(error, "Frequency outside CC1101 hardware bands");
    if(!validModulation(s.modulation)) return invalid(error, "Reserved modulation format");
    if(s.packetFormat != PacketFormat::Fifo && s.packetFormat != PacketFormat::Async)
        return invalid(error, "Unsupported packet format");
    if(!finiteNumber(s.bandwidthHz) || s.bandwidthHz < registerBandwidth(0xF0) || s.bandwidthHz > 812500.0)
        return invalid(error, "RX bandwidth must be 58035.7..812500 Hz");
    if(!finiteNumber(s.dataRateBaud) || s.dataRateBaud < 600.0 || s.dataRateBaud > maxBaud(s.modulation))
        return invalid(error, "Symbol rate outside modulation limits");
    if(s.modulation == Modulation::Msk && (s.dataRateBaud <= 26000.0 || s.mskPhase > 7))
        return invalid(error, "MSK requires over 26 kBaud and phase 0..7");
    if(s.modulation != Modulation::AskOok && s.modulation != Modulation::Msk) {
        if(!finiteNumber(s.deviationHz) || s.deviationHz < registerDeviation(0) || s.deviationHz > registerDeviation(0x77))
            return invalid(error, "Deviation outside CC1101 register range");
        if(s.bandwidthHz < 2 * s.deviationHz + s.dataRateBaud)
            return invalid(error, "RX bandwidth too narrow for selected rate and deviation");
    }
    if(s.manchester && (s.fec || s.modulation == Modulation::Msk || s.modulation == Modulation::Fsk4))
        return invalid(error, "Invalid Manchester/FEC/modulation combination");
    if(s.syncMode > 7 || (s.agc1 & 0x80)) return invalid(error, "Reserved sync/AGC setting");
    if(s.packetFormat == PacketFormat::Async) {
        if(s.modulation == Modulation::Msk || s.modulation == Modulation::Fsk4)
            return invalid(error, "Selected modulation requires FIFO mode in this driver");
        if(s.crc || s.whitening || s.fec || s.manchester || (s.syncMode & 3))
            return invalid(error, "Async mode requires no packet processing or sync");
    } else {
        if(!(s.syncMode & 3)) return invalid(error, "FIFO mode requires sync detection");
        if(!s.packetLength || s.packetLength > 60) return invalid(error, "FIFO packet length must be 1..60");
        if(s.fec) return invalid(error, "FEC unsupported for bounded variable-length FIFO packets");
    }
    if(s.rssiThresholdDbm < -130 || s.rssiThresholdDbm > 0)
        return invalid(error, "Host RSSI threshold must be -130..0 dBm");
    if(!s.rxTimeoutMs || s.rxTimeoutMs > 3600000UL || !s.txLimitMs || s.txLimitMs > 10000UL)
        return invalid(error, "RX deadline 1..3600000ms; TX deadline 1..10000ms");
    if(error) *error = 0;
    return true;
}

bool encodeRadioSettings(const RadioSettings& s, RegisterImage& image, RadioSettings* actual, const char** error) {
    if(!validateSettings(s, error)) return false;
    RegisterImage candidate;
    if(!loadPreset(s.modulation == Modulation::AskOok ? "AM650" : "FM476", candidate, error)) return false;
    double best = 1e30;
    uint8_t bwBits = 0, rateE = 0, rateM = 0, devBits = 0;
    for(uint8_t e = 0; e < 4; ++e) for(uint8_t m = 0; m < 4; ++m) {
        const uint8_t bits = uint8_t((e << 6) | (m << 4));
        const double delta = fabs(registerBandwidth(bits) - s.bandwidthHz);
        if(delta < best) { best = delta; bwBits = bits; }
    }
    best = 1e30;
    for(uint8_t e = 0; e < 16; ++e) for(unsigned m = 0; m < 256; ++m) {
        const double rate = registerDataRate(e, uint8_t(m));
        if(rate < 600.0 || rate > maxBaud(s.modulation) || (s.modulation == Modulation::Msk && rate <= 26000.0)) continue;
        const double delta = fabs(rate - s.dataRateBaud);
        if(delta < best) { best = delta; rateE = e; rateM = uint8_t(m); }
    }
    best = 1e30;
    if(s.modulation == Modulation::Msk) devBits = s.mskPhase;
    else if(s.modulation != Modulation::AskOok)
        for(uint8_t e = 0; e < 8; ++e) for(uint8_t m = 0; m < 8; ++m) {
            const uint8_t bits = uint8_t((e << 4) | m);
            const double delta = fabs(registerDeviation(bits) - s.deviationHz);
            if(delta < best) { best = delta; devBits = bits; }
        }
    uint32_t word = uint32_t(((uint64_t(s.frequencyHz) << 16) + kCrystalHz / 2) / kCrystalHz);
    uint32_t frequency = uint32_t((uint64_t(word) * kCrystalHz + 32768UL) >> 16);
    // Keep rounded values inside the hardware band even at an inclusive band edge.
    if(!supportedFrequency(frequency)) {
        word = frequency < s.frequencyHz ? word + 1 : word - 1;
        frequency = uint32_t((uint64_t(word) * kCrystalHz + 32768UL) >> 16);
    }
    candidate.set(0x0D, uint8_t(word >> 16));
    candidate.set(0x0E, uint8_t(word >> 8));
    candidate.set(0x0F, uint8_t(word));
    // Address zero is the Flipper custom-preset terminator, not an exportable write.
    candidate.set(0x01, 0x2E);
    candidate.set(0x02, s.packetFormat == PacketFormat::Async ? 0x0D : 0x06);
    candidate.set(0x03, registerBandwidth(bwBits) < 325000.0 ? 0x47 : 0x07);
    candidate.set(0x04, uint8_t(s.syncWord >> 8)); candidate.set(0x05, uint8_t(s.syncWord));
    candidate.set(0x06, s.packetLength); candidate.set(0x07, 0x04);
    candidate.set(0x08, uint8_t((uint8_t(s.packetFormat) << 4) | (s.crc ? 4 : 0) |
                               (s.whitening ? 0x40 : 0) | (s.packetFormat == PacketFormat::Async ? 2 : 1)));
    candidate.set(0x09, 0); candidate.set(0x0A, 0); candidate.set(0x0C, 0);
    candidate.set(0x10, bwBits | rateE); candidate.set(0x11, rateM);
    candidate.set(0x12, uint8_t((uint8_t(s.modulation) << 4) | (s.manchester ? 8 : 0) | s.syncMode));
    candidate.set(0x13, s.packetFormat == PacketFormat::Async ? 0 : 0x22);
    candidate.set(0x15, devBits);
    candidate.set(0x16, 0x07); // Continuous RX; host enforces its explicit deadline.
    candidate.set(0x17, 0x3C); // CCA on, RX returns to RX, TX returns to IDLE.
    candidate.set(0x18, 0x18);
    candidate.set(0x1B, s.agc2); candidate.set(0x1C, s.agc1); candidate.set(0x1D, s.agc0);
    memset(candidate.pa, 0, sizeof candidate.pa);
    RadioSettings rounded;
    if(!describeRadio(candidate, frequency, rounded, error)) return false;
    rounded.rssiThresholdDbm = s.rssiThresholdDbm;
    rounded.rxTimeoutMs = s.rxTimeoutMs; rounded.txLimitMs = s.txLimitMs;
    if(!validateSettings(rounded, error)) return false;
    image = candidate;
    if(actual) *actual = rounded;
    return true;
}

} // namespace research
