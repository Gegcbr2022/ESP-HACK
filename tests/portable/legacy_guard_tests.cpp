#include "research/legacy_guard.h"
#include <stdio.h>
#include <stdlib.h>
#define CHECK(condition) do { if (!(condition)) { \
    fprintf(stderr, "legacy guard check failed line %d: %s\n", __LINE__, #condition); abort(); \
} } while(0)

int main() {
    uint8_t packed[8];
    memset(packed, 0xA5, sizeof(packed));
    CHECK(research::packLegacyDecodedKey(0x123456, 24, packed));
    for (unsigned int i = 0; i < 5; ++i) CHECK(packed[i] == 0);
    CHECK(packed[5] == 0x12 && packed[6] == 0x34 && packed[7] == 0x56);
    CHECK(research::packLegacyDecodedKey(0xABC, 12, packed));
    for (unsigned int i = 0; i < 6; ++i) CHECK(packed[i] == 0);
    CHECK(packed[6] == 0x0A && packed[7] == 0xBC);
    CHECK(!research::packLegacyDecodedKey(0x1234, 8, packed));
    CHECK(!research::packLegacyDecodedKey(1, 0, packed));
    CHECK(!research::packLegacyDecodedKey(1, 33, packed));
    research::SubInfo info;
    info.kind = research::SubKind::Key;
    info.frequencyHz = 433920000;
    info.bits = 24;
    info.teUs = 350;
    info.keyBytes = 8;
    info.key[7] = 0x5A;
    strcpy(info.protocol, "Princeton");
    strcpy(info.preset, "FuriHalSubGhzPresetOok270Async");
    CHECK(research::loadPreset(info.preset, info.radio));
    research::LegacyDecodedPlan plan;
    CHECK(research::legacyDecodedPlan(info, plan));
    CHECK(plan.protocol == 1 && plan.bits == 24 && plan.key == 0x5A);
    CHECK(plan.repeats == 6 && plan.durationBoundMs < 5000);
    CHECK(plan.radio.value[0x10] == 0x67);
    strcpy(info.preset, "FuriHalSubGhzPresetOok650Async");
    CHECK(research::loadPreset(info.preset, info.radio));
    CHECK(research::legacyDecodedPlan(info, plan) && plan.radio.value[0x10] == 0x17);
    strcpy(info.preset, "FuriHalSubGhzPreset2FSKDev238Async");
    CHECK(research::loadPreset(info.preset, info.radio));
    CHECK(!research::legacyDecodedPlan(info, plan));
    strcpy(info.preset, "AM270");
    CHECK(research::loadPreset(info.preset, info.radio));
    strcpy(info.preset, "FuriHalSubGhzPresetCustom");
    CHECK(research::legacyOokFile(info));
    CHECK(!research::legacyDecodedPlan(info, plan));
    strcpy(info.preset, "UnknownPreset");
    CHECK(!research::legacyDecodedPlan(info, plan));
    strcpy(info.preset, "AM270");
    strcpy(info.protocol, "UnknownProtocol");
    CHECK(!research::legacyDecodedPlan(info, plan));
    strcpy(info.protocol, "Princeton");
    info.bits = 33;
    CHECK(!research::legacyDecodedPlan(info, plan));
    info.bits = 4;
    CHECK(!research::legacyDecodedPlan(info, plan));
    info.bits = 32;
    info.teUs = 0;
    CHECK(!research::legacyDecodedPlan(info, plan));
    info.teUs = 10000;
    CHECK(!research::legacyDecodedPlan(info, plan));
    strcpy(info.protocol, "CAME");
    info.teUs = 270;
    CHECK(research::legacyDecodedPlan(info, plan) && plan.protocol == 11);
    puts("legacy guard: first/changed key length packing, exact OOK presets, rejection, payload/timing bounds passed");
}
