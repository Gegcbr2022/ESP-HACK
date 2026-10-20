#include "pocket_menu.h"
#include <stdio.h>

extern const char* menuItems[];
extern bool sdCardReady;

namespace {
void text(DisplayType& display, int16_t x, int16_t y, const char* value, uint8_t maximum) {
  char clipped[22]; uint8_t count = 0;
  while(value && value[count] && count < maximum) {
    const unsigned char c = value[count]; clipped[count++] = c >= 32 && c <= 126 ? c : '?';
  }
  if(value && value[count] && count >= 3) clipped[count - 3] = clipped[count - 2] = clipped[count - 1] = '.';
  clipped[count] = 0; display.setCursor(x, y); display.print(clipped);
}
void prepareText(DisplayType& display) {
  display.setFont(nullptr); display.setTextSize(1); display.setTextWrap(false);
  display.setTextColor(SH110X_WHITE);
}
}

namespace pocket_draw {
void header(DisplayType& display, const char* title, uint8_t position, uint8_t total, bool sdReady) {
  prepareText(display);
  // Original tiny terminal mark, not a device mascot or borrowed icon.
  display.drawRect(1, 1, 9, 8, SH110X_WHITE);
  display.drawLine(3, 3, 5, 5, SH110X_WHITE); display.drawLine(5, 5, 3, 7, SH110X_WHITE);
  display.drawFastHLine(6, 7, 2, SH110X_WHITE);
  text(display, 13, 1, title, 10);
  display.drawRect(77, 0, 16, 10, SH110X_WHITE);
  text(display, 79, 1, sdReady ? "SD" : "--", 2);
  char counter[6];
  snprintf(counter, sizeof(counter), "%02u/%02u", unsigned(position > 99 ? 99 : position), unsigned(total > 99 ? 99 : total));
  text(display, 97, 1, counter, 5);
  display.drawFastHLine(0, 10, 128, SH110X_WHITE);
}

void icon(DisplayType& display, uint8_t category, int16_t x, int16_t y, uint16_t color) {
  // Original 12x12 line drawings; all coordinates stay inside the glyph cell.
  switch(category) {
    case 0: // WiFi, three short wave crests.
      display.drawLine(x, y+3, x+3, y, color); display.drawFastHLine(x+3, y, 6, color);
      display.drawLine(x+8, y, x+11, y+3, color);
      display.drawLine(x+2, y+5, x+4, y+3, color); display.drawFastHLine(x+4, y+3, 4, color);
      display.drawLine(x+7, y+3, x+9, y+5, color);
      display.drawLine(x+4, y+7, x+5, y+6, color); display.drawLine(x+6, y+6, x+7, y+7, color);
      display.fillRect(x+5, y+9, 2, 2, color); break;
    case 1: // Bluetooth rune drawn on this cell's own grid.
      display.drawFastVLine(x+5, y, 12, color);
      display.drawLine(x+5, y, x+9, y+4, color); display.drawLine(x+9, y+4, x+2, y+9, color);
      display.drawLine(x+2, y+2, x+9, y+7, color); display.drawLine(x+9, y+7, x+5, y+11, color); break;
    case 2: // Antenna and two pairs of RF wave strokes.
      display.drawFastVLine(x+5, y+4, 7, color); display.drawFastHLine(x+2, y+11, 7, color);
      display.fillRect(x+4, y+2, 3, 3, color);
      display.drawLine(x+2, y+1, x, y+3, color); display.drawLine(x, y+3, x+2, y+6, color);
      display.drawLine(x+9, y+1, x+11, y+3, color); display.drawLine(x+11, y+3, x+9, y+6, color); break;
    case 3: // Remote with a single emitter.
      display.drawRoundRect(x+3, y+3, 6, 9, 1, color); display.drawFastVLine(x+5, y, 2, color);
      display.drawPixel(x+1, y+1, color); display.drawPixel(x+10, y+1, color);
      display.drawPixel(x+5, y+5, color); display.drawFastHLine(x+5, y+8, 2, color); break;
    case 4: // Chip pins and center die.
      display.drawRect(x+2, y+2, 8, 8, color); display.fillRect(x+4, y+4, 4, 4, color);
      for(uint8_t pin=3; pin<=8; pin+=3) {
        display.drawFastVLine(x+pin, y, 2, color); display.drawFastVLine(x+pin, y+10, 2, color);
        display.drawFastHLine(x, y+pin, 2, color); display.drawFastHLine(x+10, y+pin, 2, color);
      } break;
    case 5: // Game pad.
      display.drawRoundRect(x, y+3, 12, 8, 2, color);
      display.drawFastHLine(x+2, y+6, 4, color); display.drawFastVLine(x+3, y+5, 4, color);
      display.drawPixel(x+8, y+5, color); display.drawPixel(x+9, y+8, color); break;
    case 6: // Three compact settings sliders.
      for(uint8_t row=1; row<=9; row+=4) display.drawFastHLine(x, y+row+1, 12, color);
      display.fillRect(x+2, y+1, 3, 3, color); display.fillRect(x+7, y+5, 3, 3, color);
      display.fillRect(x+4, y+9, 3, 3, color); break;
    default: // SD application card with terminal prompt.
      display.drawLine(x+4, y, x+10, y, color); display.drawLine(x+1, y+3, x+4, y, color);
      display.drawFastVLine(x+1, y+3, 9, color); display.drawFastVLine(x+10, y, 12, color);
      display.drawFastHLine(x+1, y+11, 10, color); display.drawFastHLine(x+5, y+2, 4, color);
      display.drawLine(x+3, y+5, x+5, y+7, color); display.drawLine(x+5, y+7, x+3, y+9, color);
      display.drawFastHLine(x+6, y+9, 3, color); break;
  }
}

void listRow(DisplayType& display, int16_t y, const char* label, bool selected, uint8_t category) {
  prepareText(display);
  if(selected) display.fillRoundRect(1, y, 120, 13, 2, SH110X_WHITE);
  const uint16_t ink = selected ? SH110X_BLACK : SH110X_WHITE;
  icon(display, category, 4, y+1, ink);
  display.setTextColor(ink); text(display, 22, y+3, label, 14);
  if(selected) {
    display.drawLine(114, y+4, 117, y+6, ink); display.drawLine(117, y+6, 114, y+9, ink);
  }
}

void footer(DisplayType& display, const char* hint) {
  prepareText(display); display.drawFastHLine(0, 54, 128, SH110X_WHITE); text(display, 1, 56, hint, 21);
}
}

void displayPocketMenu(DisplayType& display, byte menuIndex) {
  const PocketMenuWindow window = pocketMenuWindow(menuIndex);
  display.clearDisplay();
  pocket_draw::header(display, "ESP-HACK", window.selected+1, MAIN_MENU_ITEM_COUNT, sdCardReady);
  for(uint8_t row=0; row<window.count; ++row) {
    const uint8_t category = window.first+row;
    pocket_draw::listRow(display, 12+row*14, menuItems[category], category==window.selected, category);
  }
  display.drawFastVLine(125, 12, 41, SH110X_WHITE);
  const uint8_t thumbHeight = 41*window.count/MAIN_MENU_ITEM_COUNT;
  const uint8_t thumbY = 12+window.first*(41-thumbHeight)/(MAIN_MENU_ITEM_COUNT-window.count);
  display.fillRect(124, thumbY, 3, thumbHeight, SH110X_WHITE);
  pocket_draw::footer(display, "UP/DN move   OK open");
  display.display();
}
