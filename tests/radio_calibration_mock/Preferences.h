#pragma once
#include <stddef.h>
#include <stdint.h>
enum PreferenceType { PT_I32, PT_STR, PT_INVALID };
class Preferences {
public:
    bool begin(const char* name, bool readOnly = false);
    void end();
    bool isKey(const char* key);
    PreferenceType getType(const char* key);
    int32_t getInt(const char* key, int32_t fallback = 0);
    size_t putInt(const char* key, int32_t value);
};
