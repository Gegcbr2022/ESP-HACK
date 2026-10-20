#include "radio_calibration.h"
#include "trim_math.h"
#include "lab_service.h"
#include <Preferences.h>

namespace research {
namespace {
int32_t calibrationPpm;
const char* const kNamespace = "esphack_rf";
const char* const kKey = "ppm";
// Keep the sizable snapshot out of the Preferences call frame.
__attribute__((noinline)) bool calibrationIdle() { return !labSnapshot().active; }
}
int32_t radioCalibrationPpm() { return calibrationPpm; }
bool loadRadioCalibration() {
    if(!calibrationIdle()) return false;
    Preferences preferences;
    // Opening read/write permits a first-boot empty namespace. No value is
    // written by load; a missing key is the uncalibrated zero setting.
    if(!preferences.begin(kNamespace, false)) return false;
    const bool present = preferences.isKey(kKey);
    const bool correctType = !present || preferences.getType(kKey) == PT_I32;
    const int32_t stored = correctType ? preferences.getInt(kKey, 0) : 0;
    preferences.end();
    if(!correctType || !validRadioTrim(stored)) return false;
    calibrationPpm = stored;
    return true;
}
bool saveRadioCalibration(int32_t ppm) {
    if(!validRadioTrim(ppm) || !calibrationIdle()) return false;
    Preferences preferences;
    if(!preferences.begin(kNamespace, false)) return false;
    const bool saved = preferences.putInt(kKey, ppm) == sizeof(int32_t);
    preferences.end();
    if(!saved) return false;
    calibrationPpm = ppm;
    return true;
}
}
