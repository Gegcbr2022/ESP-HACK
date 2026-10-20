#pragma once
#include <stdint.h>
#include <stddef.h>
namespace research {
struct DecodedSignal { const char* protocol; uint32_t payload; uint16_t bits; uint32_t unitUs; bool valid; };
inline uint8_t testCrc8(uint16_t payload) {
    uint8_t crc = 0;
    for(int i = 15; i >= 0; --i) {
        const bool mix = ((crc >> 7) ^ (payload >> i)) & 1;
        crc <<= 1; if(mix) crc ^= 0x07;
    }
    return crc;
}
inline bool nearPulse(int32_t p, int32_t target, uint32_t tolerance = 100) {
    return p >= target - int32_t(tolerance) && p <= target + int32_t(tolerance);
}
// Public laboratory format LAB-OOK24: +1000,-5000 then 24 MSB-first symbols.
// 0=+300,-300; 1=+900,-300. Upper16 payload, lower8 CRC-8/SMBUS(poly07,init00).
// Encoder adds +300,-5000 guard after payload. Its rising edge completes the
// final -300 data interval for an edge-driven receiver before TX idles LOW.
inline bool decodeLabOok(const int32_t* p, size_t count, DecodedSignal& out) {
    out = {"LAB-OOK24", 0, 24, 300, false};
    if(!p) return false;
    for(size_t start = 0; start + 50 <= count; ++start) {
        if(!nearPulse(p[start], 1000, 200) || !nearPulse(p[start+1], -5000, 500)) continue;
        uint32_t word = 0; bool ok = true;
        for(size_t b = 0; b < 24; ++b) {
            int32_t hi = p[start+2+b*2], lo = p[start+3+b*2];
            if(!nearPulse(lo, -300) || (!nearPulse(hi, 300) && !nearPulse(hi, 900))) { ok = false; break; }
            word = (word << 1) | (hi > 600);
        }
        if(ok && testCrc8(word >> 8) == uint8_t(word)) {
            out.payload = word >> 8; out.valid = true; return true;
        }
    }
    return false;
}
typedef bool (*PulseDecoder)(const int32_t*, size_t, DecodedSignal&);
struct DecoderEntry { const char* name; PulseDecoder decode; };
inline const DecoderEntry* decoderAt(size_t index) {
    static const DecoderEntry registry[] = {{"LAB-OOK24", decodeLabOok}};
    return index < sizeof(registry)/sizeof(registry[0]) ? &registry[index] : nullptr;
}
inline size_t encodeLabOok(uint16_t payload, int32_t* output, size_t capacity) {
    if(!output || capacity < 52) return 0;
    output[0] = 1000; output[1] = -5000;
    uint32_t word = (uint32_t(payload) << 8) | testCrc8(payload);
    for(size_t i = 0; i < 24; ++i) {
        output[2+i*2] = (word & (1UL << (23-i))) ? 900 : 300;
        output[3+i*2] = -300;
    }
    output[50] = 300; output[51] = -5000;
    return 52;
}
}
