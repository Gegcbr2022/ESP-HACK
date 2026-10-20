#pragma once
#include "radio_config.h"
#include "../../shared/flipper_chat_compat.h"

namespace research {
// A valid preset includes all eight PA bytes even when receive-only/zeroed.
// Power authorization and the selected TX table are installed by the worker.
inline bool makeFlipperChatProfile(RegisterImage& image, RadioSettings& settings,
                                   const char** error = 0) {
    image = RegisterImage();
    for(size_t i = 0; i < FLIPPER_CHAT_REGISTER_BYTES; i += 2)
        image.set(flipper_chat_registers[i], flipper_chat_registers[i + 1]);
    image.paPresent = true;
    if(!describeRadio(image, 433920000, settings, error)) return false;
    settings.txLimitMs = 1000;
    // Host clear-channel gate; the hardware CS bit can assert on background noise.
    settings.rssiThresholdDbm = -75;
    // An interactive chat runs until the user/serial close requests a stop.
    settings.rxTimeoutMs = 0;
    return true;
}
}
