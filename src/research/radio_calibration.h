#pragma once
#include <stdint.h>

namespace research {
// Call load/save from the foreground only, while the radio worker is idle.
// Own NVS namespace; no radio/SPI access. Missing saved value means zero trim.
// Positive ppm raises the programmed RX/TX frequency; accepted range +/-250.
bool loadRadioCalibration();
bool saveRadioCalibration(int32_t ppm);
int32_t radioCalibrationPpm();
}
