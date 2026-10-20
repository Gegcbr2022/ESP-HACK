#pragma once
#include <stdint.h>

namespace research {
struct PacketMetadata {
    int16_t rssiHalfDbm;
    int32_t frequencyErrorHz;
    uint8_t lqi, statusLqi;
    bool crcOk, statusCrcOk;
};
// CC1101 datasheet17.3/17.6 and FREQEST register: nominal74dB RSSI offset,
// signed frequency error at26MHz/2^14. Preserve half-dBm RSSI precision.
inline PacketMetadata packetMetadata(uint8_t rssi, uint8_t appendedLqi,
                                     uint8_t frequencyEstimate, uint8_t statusLqi) {
    const int16_t signedRssi = rssi < 128 ? int16_t(rssi) : int16_t(rssi) - 256;
    const int16_t signedFrequency = frequencyEstimate < 128 ? int16_t(frequencyEstimate) : int16_t(frequencyEstimate) - 256;
    PacketMetadata result;
    result.rssiHalfDbm = signedRssi - 148;
    result.frequencyErrorHz = int32_t(int64_t(signedFrequency) * 26000000 / 16384);
    result.lqi = appendedLqi & 0x7F;
    result.statusLqi = statusLqi & 0x7F;
    result.crcOk = (appendedLqi & 0x80) != 0;
    result.statusCrcOk = (statusLqi & 0x80) != 0;
    return result;
}
}
