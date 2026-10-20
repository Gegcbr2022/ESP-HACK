#pragma once
#include "display.h"
#include "utf8_text.h"

namespace ui_text {
// Native5x7 glyphs,6px advance. Draw clips at glyph boundaries and the viewport;
// scale1/2 does not change the display's saved text-size setting.
void draw(DisplayType& display, int16_t x, int16_t y, const char* text,
          uint16_t color=SH110X_WHITE, size_t maxGlyphs=21, uint8_t scale=1);
// ASCII-only strings retain the caller's GFX font/size/wrapping behavior.
// Cyrillic strings use native6px cells and update the GFX cursor.
void print(DisplayType& display, const char* text, uint16_t color=SH110X_WHITE);
size_t glyphCount(const char* text);
size_t copyGlyphs(char* output, size_t capacity, const char* text, size_t maxGlyphs);
}
