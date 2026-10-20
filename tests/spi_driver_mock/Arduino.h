#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>
typedef uint8_t byte;
typedef int gpio_num_t;
#define HIGH 1
#define LOW 0
#define INPUT 0
#define OUTPUT 1
#define bitRead(value, bit) (((value) >> (bit)) & 1u)
uint32_t micros();
uint32_t millis();
void delay(uint32_t milliseconds);
void delayMicroseconds(uint32_t microseconds);
void yield();
int digitalRead(uint8_t pin);
int gpio_get_level(gpio_num_t pin);
void digitalWrite(uint8_t pin, uint8_t level);
void pinMode(uint8_t pin, uint8_t mode);
inline long map(long value, long low, long high, long output_low, long output_high) {
    return (value - low) * (output_high - output_low) / (high - low) + output_low;
}
