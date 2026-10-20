#pragma once
#include "lab_limits.h"
#include <stdint.h>

namespace research {
static const int32_t kRadioTrimLimitPpm = 250;
inline bool validRadioTrim(int32_t ppm) {
    return ppm >= -kRadioTrimLimitPpm && ppm <= kRadioTrimLimitPpm;
}
// Strict signed decimal, at most ten digits. No spaces or partial parses.
// Accumulate only while inside +/-250, so arbitrarily large values cannot wrap.
inline bool parseRadioTrimPpm(const char* text, int32_t& ppm) {
    if(!text || !*text) return false;
    const bool negative = *text == '-';
    if(negative || *text == '+') ++text;
    if(!*text) return false;
    uint32_t magnitude = 0;
    unsigned digits = 0;
    for(; *text; ++text) {
        if(*text < '0' || *text > '9' || ++digits > 10) return false;
        magnitude = magnitude * 10 + uint32_t(*text - '0');
        if(magnitude > uint32_t(kRadioTrimLimitPpm)) return false;
    }
    ppm = negative ? -int32_t(magnitude) : int32_t(magnitude);
    return true;
}
// Positive trim raises the programmed frequency. Logical channel/policy values
// stay nominal. Round Hz first, then let cc1101FrequencyWord quantize to the PLL.
// This two-stage calculation avoids multiplying Hz * 65536 * 1000000.
inline bool trimmedFrequencyHz(uint32_t nominalHz, int32_t ppm, uint32_t& programmedHz) {
    if(!validRadioTrim(ppm) || !cc1101FrequencyWord(nominalHz)) return false;
    const uint64_t scaled = uint64_t(nominalHz) * uint32_t(1000000 + ppm);
    const uint64_t corrected = (scaled + 500000UL) / 1000000UL;
    if(corrected > UINT32_MAX || !cc1101FrequencyWord(uint32_t(corrected))) return false;
    programmedHz = uint32_t(corrected);
    return true;
}
}
