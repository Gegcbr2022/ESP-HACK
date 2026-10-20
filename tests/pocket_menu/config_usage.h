#pragma once
#include "CONFIG.h"
// Real CONFIG.h defines a static version pointer in every translation unit.
// Reference it without changing production headers or relaxing GCC warnings.
inline const char* pocketMockFirmwareVersion(){return FIRMWARE;}
