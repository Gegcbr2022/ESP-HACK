#pragma once
#include <stdint.h>
#include <stddef.h>
#include <algorithm>
typedef uint8_t byte;
#define PROGMEM
#define LOW 0
#define HIGH 1
using std::max;
using std::min;
unsigned long millis();
void delay(unsigned long ms);
inline int digitalRead(int) { return HIGH; }
