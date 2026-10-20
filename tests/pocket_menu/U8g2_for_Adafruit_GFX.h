#pragma once
#include "Adafruit_GFX.h"
#define U8X8_PROGMEM
extern const uint8_t u8g2_font_5x7_tr[];
extern const uint8_t u8g2_font_haxrcorp4089_tr[];
extern const uint8_t u8g2_font_helvB08_tr[];
// The legacy styles get an API/text smoke test; their U8g2 font metrics are not
// pixel-identical. Pocket uses the actual bundled GFX5x7 font data below.
class U8G2_FOR_ADAFRUIT_GFX {
  Adafruit_GFX* output=nullptr;
public:
  void begin(Adafruit_GFX& display){output=&display;}
  void setFontMode(int){}
  void setFont(const uint8_t*){}
  void setForegroundColor(int ink){output->setTextColor(ink);}
  void setBackgroundColor(int){}
  void setCursor(int x,int baseline){output->setCursor(x,baseline-7);}
  int16_t getUTF8Width(const char* text){return strlen(text)*6;}
  void print(const char* text){output->print(text);}
};
