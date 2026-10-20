#include "ui_text.h"
#include "cyrillic5x7.h"
#include <string.h>

namespace ui_text {
size_t glyphCount(const char* text) {
  if(!text)return 0;
  size_t glyphs=0;return validate(text,strlen(text),&glyphs)?glyphs:0;
}
size_t copyGlyphs(char* output,size_t capacity,const char* text,size_t maxGlyphs) {
  if(!output||!capacity)return 0;
  size_t bytes=prefixBytes(text,maxGlyphs);
  if(bytes>=capacity) {
    bytes=0;
    while(text&&text[bytes]) {
      uint16_t codepoint;const size_t available=uint8_t(text[bytes])<0x80?1:(text[bytes+1]?2:1);
      const size_t width=decodeGlyph(text+bytes,available,codepoint);
      if(!width||bytes+width>=capacity)break;
      bytes+=width;
    }
  }
  if(bytes)memcpy(output,text,bytes);
  output[bytes]=0;return bytes;
}
void draw(DisplayType& display,int16_t x,int16_t y,const char* text,uint16_t color,size_t maxGlyphs,uint8_t scale) {
  if(!text)return;
  if(scale!=2)scale=1;
  if(maxGlyphs>21)maxGlyphs=21;
  const size_t end=prefixBytes(text,maxGlyphs);
  size_t at=0;int32_t cursor=x;
  display.setFont(nullptr);
  while(at<end) {
    uint16_t codepoint;const size_t width=decodeGlyph(text+at,end-at,codepoint);
    if(!width)break;
    if(codepoint<128) {
      if(cursor>=-12&&cursor<display.width()&&y>=-16&&y<display.height())
        display.drawChar(int16_t(cursor),y,uint8_t(codepoint),color,color,scale);
    } else {
      const uint8_t* glyph=codepoint==0x401?cyrillic5x7[64]:codepoint==0x451?cyrillic5x7[65]:cyrillic5x7[codepoint-0x410];
      for(uint8_t col=0;col<5;++col)for(uint8_t row=0;row<7;++row)if(glyph[col]&(1<<row)) {
        const int32_t px=cursor+col*scale,py=int32_t(y)+row*scale;
        for(uint8_t dx=0;dx<scale;++dx)for(uint8_t dy=0;dy<scale;++dy)
          if(px+dx>=0&&px+dx<display.width()&&py+dy>=0&&py+dy<display.height())
            display.drawPixel(int16_t(px+dx),int16_t(py+dy),color);
      }
    }
    at+=width;cursor+=6*scale;
  }
  display.setCursor(int16_t(cursor),y);
}
void print(DisplayType& display,const char* text,uint16_t color) {
  if(!text)return;
  bool ascii=true;for(const unsigned char* p=reinterpret_cast<const unsigned char*>(text);*p;++p)
    if(*p>=128){ascii=false;break;}
  if(ascii){display.setTextColor(color);display.print(text);return;}
  draw(display,display.getCursorX(),display.getCursorY(),text,color,21,1);
}
}
