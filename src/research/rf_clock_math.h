#pragma once
#include <stdint.h>

namespace research {
// PCNT signed16-bit bank, XOSC/192, finite1..200ms interval. No result
// modification on invalid count/timing/overflow. Count quantization remains;
// this distinguishes26/27MHz, it is not a ppm-grade crystal calibration.
inline bool rfClockEstimate(uint32_t edges, uint32_t elapsedUs, uint32_t& hz) {
    if(!edges || edges >= 32767 || elapsedUs < 1000 || elapsedUs > 200000) return false;
    const uint64_t scaled = uint64_t(edges) * 192 * 1000000;
    const uint64_t rounded = (scaled + elapsedUs / 2) / elapsedUs;
    if(rounded > UINT32_MAX) return false;
    hz = uint32_t(rounded); return true;
}
inline bool rfClockEstimateInterval(uint32_t edges, uint32_t startUs,
                                    uint32_t endUs, uint32_t& hz) {
    return rfClockEstimate(edges, uint32_t(endUs-startUs), hz);
}
inline uint32_t rfClockMedian(uint32_t a, uint32_t b, uint32_t c) {
    if(a>b) { const uint32_t t=a;a=b;b=t; }
    if(b>c) { const uint32_t t=b;b=c;c=t; }
    return a>b?a:b;
}
}
