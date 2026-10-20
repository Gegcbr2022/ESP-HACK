// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "sub_file.h"
#include <stddef.h>
#include <stdint.h>

namespace research {
enum class StaticSubError : uint8_t {
    None, Dynamic, Protocol, Key, Timing, Radio, Capacity, Duration
};
struct StaticSubWaveform {
    size_t count;
    uint32_t durationUs;
    uint8_t repeats;
    bool phaseRotated;
};
// Exact static encoders only: Princeton24 with fileTE/Guard_time, CAME12/24
// fixed320us. Always6periods, <=2048signedintervals and <=5seconds. No allocation.
// On failure the output buffer and result remain unchanged. No TX authorization.
StaticSubError encodeStaticSub(const SubInfo& info, int32_t* output, size_t capacity,
                              StaticSubWaveform& result);
bool dynamicSubProtocol(const char* protocol);
const char* staticSubErrorRu(StaticSubError error);
}
