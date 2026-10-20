#pragma once
#include <stdint.h>
#define GPIO_MATRIX_CONST_ZERO_INPUT 0x30
void esp_rom_gpio_connect_in_signal(uint32_t,uint32_t,bool);
