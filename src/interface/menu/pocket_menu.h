#pragma once
#include "display.h"
#include "menu_model.h"

// Shared small primitives for 128x64 screens. No framebuffer or heap allocation.
namespace pocket_draw {
void header(DisplayType& display, const char* title, uint8_t position, uint8_t total, bool sdReady);
void icon(DisplayType& display, uint8_t category, int16_t x, int16_t y, uint16_t color);
void listRow(DisplayType& display, int16_t y, const char* label, bool selected, uint8_t category);
void footer(DisplayType& display, const char* hint);
}

void displayPocketMenu(DisplayType& display, byte menuIndex);
