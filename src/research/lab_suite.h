#pragma once
#include <stdint.h>
void runRadioLab();
void runSubGhzChat();
void runRadioLabFile(const char* path);
void runRadioLabFiles();
bool radioLabPermitTx(uint32_t frequencyHz, uint32_t durationMs);
void radioLabBootDiagnostics();
void radioLabSerialPoll();
// Main loop only: call after radioLabSerialPoll() returns, never from chat input.
void radioLabHandlePending();
void ensureRadioLabFixture();
