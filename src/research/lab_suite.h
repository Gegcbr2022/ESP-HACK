#pragma once
#include <stdint.h>
void runRadioLab();
void runRadioLabFile(const char* path);
bool radioLabPermitTx(uint32_t frequencyHz, uint32_t durationMs);
void radioLabBootDiagnostics();
void radioLabSerialPoll();
void ensureRadioLabFixture();
