#pragma once
#include <stdint.h>

namespace research {
// Host RSSI gate: hardware CS can remain asserted on the local noise floor.
// Always defer during a synchronized packet, even below the RSSI threshold.
inline bool chatChannelBusy(int16_t rssiDbm, int16_t thresholdDbm, bool sync) {
    return sync || rssiDbm >= thresholdDbm;
}
}
