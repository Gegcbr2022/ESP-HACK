#pragma once

#include "sub_file.h"
#include <string.h>

namespace research {

inline bool packLegacyDecodedKey(uint32_t value, unsigned int bits, uint8_t output[8]) {
    if (!output || !bits || bits > 32 || (bits < 32 && (value >> bits))) return false;
    memset(output, 0, 8);
    for (uint8_t i = 0; i < 4; ++i) output[7 - i] = (value >> (i * 8)) & 0xFF;
    return true;
}

struct LegacyDecodedPlan {
    RegisterImage radio;
    uint32_t frequencyHz;
    uint32_t key;
    uint32_t pulseUs;
    uint32_t durationBoundMs;
    uint8_t bits, protocol, repeats;
};

inline bool legacyOokFile(const SubInfo& info, const char** error = nullptr) {
    RadioSettings actual;
    if (!describeRadio(info.radio, info.frequencyHz, actual) ||
        actual.modulation != Modulation::AskOok || actual.packetFormat != PacketFormat::Async ||
        actual.manchester || actual.fec || actual.crc || actual.whitening || actual.syncMode) {
        if (error) *error = "Legacy requires async OOK; use Radio Lab inspection";
        return false;
    }
    return true;
}

// Preserve the legacy protocol-1 and protocol-11 encoders only. Never fall back
// to a guessed preset or protocol. RAW goes to the bounded Radio Lab/RMT path.
inline bool legacyDecodedPlan(const SubInfo& info, LegacyDecodedPlan& plan,
                              const char** error = nullptr) {
    if (!legacyOokFile(info, error)) return false;
    const RadioPreset* preset = findPreset(info.preset);
    if (!preset || (strcmp(preset->name, "AM270") && strcmp(preset->name, "AM650"))) {
        if (error) *error = "Custom shaping unsupported by legacy decoded Send";
        return false;
    }
    uint8_t protocol;
    if (!strcmp(info.protocol, "Princeton") || !strcmp(info.protocol, "RcSwitch")) protocol = 1;
    else if (!strcmp(info.protocol, "CAME")) protocol = 11;
    else {
        if (error) *error = "Protocol has no supported legacy encoder";
        return false;
    }
    if (info.kind != SubKind::Key || !info.bits || info.bits > 32 || !info.keyBytes || info.keyBytes > 8) {
        if (error) *error = "Legacy payload must contain 1..32 bits and a key";
        return false;
    }
    uint64_t key = 0;
    for (uint8_t i = 0; i < info.keyBytes; ++i) key = (key << 8) | info.key[i];
    if (key > UINT32_MAX || (info.bits < 32 && (key >> info.bits))) {
        if (error) *error = "Key has set bits outside the declared bit length";
        return false;
    }
    if (!info.teUs || info.teUs > 10000) {
        if (error) *error = "Missing or unsupported TE (1..10000 us)";
        return false;
    }
    const uint8_t repeats = 6; // Same cap as the existing RCSwitch_send wrapper.
    const uint32_t factors = protocol == 1 ? 4U * info.bits + 32U : 3U * info.bits + 37U;
    // Exact configured delay sum plus a conservative GPIO/scheduling margin.
    // This bounds parameters; physical completion time still needs measurement.
    const uint64_t boundUs = uint64_t(factors) * info.teUs * repeats +
                             uint64_t(2U * info.bits + 2U) * repeats * 20U + 50000U;
    if (boundUs > 5000000U) {
        if (error) *error = "Legacy waveform exceeds 5 second laboratory cap";
        return false;
    }
    if (!loadPreset(preset->name, plan.radio, error)) return false;
    plan.frequencyHz = info.frequencyHz;
    plan.key = static_cast<uint32_t>(key);
    plan.pulseUs = info.teUs;
    plan.durationBoundMs = static_cast<uint32_t>((boundUs + 999U) / 1000U);
    plan.bits = static_cast<uint8_t>(info.bits);
    plan.protocol = protocol;
    plan.repeats = repeats;
    return true;
}

} // namespace research
