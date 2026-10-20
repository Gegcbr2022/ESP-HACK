#pragma once
#include <stdint.h>

#define IRAM_ATTR
#define DRAM_ATTR
#define INPUT 0
#define CHANGE 3
typedef int portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED 0
// These mocks check algorithmic behavior, not ESP32 interrupt mutual exclusion.
#define portENTER_CRITICAL(lock) ((void)(lock))
#define portEXIT_CRITICAL(lock) ((void)(lock))
#define portENTER_CRITICAL_ISR(lock) ((void)(lock))
#define portEXIT_CRITICAL_ISR(lock) ((void)(lock))

uint32_t micros();
int digitalRead(uint8_t pin);
void pinMode(uint8_t pin, int mode);
void attachInterruptArg(uint8_t pin, void (*callback)(void*), void* arg, int mode);
void detachInterrupt(uint8_t pin);
