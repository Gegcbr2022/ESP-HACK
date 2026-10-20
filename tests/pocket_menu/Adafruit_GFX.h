#pragma once
#include "Arduino.h"
#include "font5x7.h"
#include <string>
#include <vector>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
struct TextCall { int x,y,color; std::string value; };
struct BoxCall { int x,y,w,h,color; };
class Adafruit_GFX {
  int cursorX=0,cursorY=0,textSize=1,ink=1;
public:
  uint8_t pixels[64][128] = {};
  unsigned clipped=0,frames=0,fontChanges=0;
  std::vector<TextCall> labels;
  std::vector<BoxCall> rounded;
  int width()const{return 128;}
  int height()const{return 64;}
  void clearDisplay(){memset(pixels,0,sizeof pixels);clipped=0;labels.clear();rounded.clear();}
  void display(){++frames;}
  void drawPixel(int x,int y,int color) {
    if(x<0||x>=128||y<0||y>=64){++clipped;return;}
    pixels[y][x]=color?255:0;
  }
  void drawLine(int x0,int y0,int x1,int y1,int color) {
    const int dx=abs(x1-x0),sx=x0<x1?1:-1,dy=-abs(y1-y0),sy=y0<y1?1:-1;
    int error=dx+dy;
    while(true) {
      drawPixel(x0,y0,color);if(x0==x1&&y0==y1)break;
      const int twice=2*error;
      if(twice>=dy){error+=dy;x0+=sx;}
      if(twice<=dx){error+=dx;y0+=sy;}
    }
  }
  void drawFastHLine(int x,int y,int w,int color){for(int n=0;n<w;++n)drawPixel(x+n,y,color);}
  void drawFastVLine(int x,int y,int h,int color){for(int n=0;n<h;++n)drawPixel(x,y+n,color);}
  void fillRect(int x,int y,int w,int h,int color){for(int row=0;row<h;++row)drawFastHLine(x,y+row,w,color);}
  void drawRect(int x,int y,int w,int h,int color) {
    if(w<=0||h<=0)return;
    drawFastHLine(x,y,w,color);drawFastHLine(x,y+h-1,w,color);
    drawFastVLine(x,y,h,color);drawFastVLine(x+w-1,y,h,color);
  }
  static bool roundedPixel(int x,int y,int w,int h,int radius) {
    if(x<0||y<0||x>=w||y>=h)return false;
    const int cx=x<radius?radius:(x>=w-radius?w-radius-1:x);
    const int cy=y<radius?radius:(y>=h-radius?h-radius-1:y);
    const int dx=x-cx,dy=y-cy;return dx*dx+dy*dy<=radius*radius;
  }
  void fillRoundRect(int x,int y,int w,int h,int radius,int color) {
    rounded.push_back({x,y,w,h,color});radius=std::min(radius,std::min(w,h)/2);
    for(int row=0;row<h;++row)for(int col=0;col<w;++col)
      if(roundedPixel(col,row,w,h,radius))drawPixel(x+col,y+row,color);
  }
  void drawRoundRect(int x,int y,int w,int h,int radius,int color) {
    radius=std::min(radius,std::min(w,h)/2);
    for(int row=0;row<h;++row)for(int col=0;col<w;++col)
      if(roundedPixel(col,row,w,h,radius)&&
         (!roundedPixel(col-1,row,w,h,radius)||!roundedPixel(col+1,row,w,h,radius)||
          !roundedPixel(col,row-1,w,h,radius)||!roundedPixel(col,row+1,w,h,radius)))drawPixel(x+col,y+row,color);
  }
  void drawBitmap(int x,int y,const uint8_t* bits,int w,int h,int color) {
    const int stride=(w+7)/8;
    for(int row=0;row<h;++row)for(int col=0;col<w;++col)
      if(bits[row*stride+col/8]&(0x80>>(col&7)))drawPixel(x+col,y+row,color);
  }
  void setFont(const void*){++fontChanges;}
  void setTextColor(int value){ink=value;}
  void setTextColor(int value,int){ink=value;}
  void setTextSize(int size){textSize=size;}
  void setTextWrap(bool){}
  void setCursor(int x,int y){cursorX=x;cursorY=y;}
  int16_t getCursorX()const{return cursorX;}
  int16_t getCursorY()const{return cursorY;}
  void drawChar(int x,int y,unsigned char c,int color,int,uint8_t scale) {
    const std::string value(1,char(c));
    if(!labels.empty()&&scale==1&&labels.back().y==y&&labels.back().color==color&&
       labels.back().x+int(labels.back().value.size())*6==x)labels.back().value+=value;
    else labels.push_back({x,y,color,value});
    for(int col=0;col<5;++col)for(int row=0;row<8;++row)if(font[c*5+col]&(1<<row))
      for(int dx=0;dx<scale;++dx)for(int dy=0;dy<scale;++dy) {
        const int px=x+col*scale+dx,py=y+row*scale+dy;
        if(px>=0&&px<128&&py>=0&&py<64)drawPixel(px,py,color);
      }
  }
  void print(const char* text) {
    labels.push_back({cursorX,cursorY,ink,text?text:""});
    if(!text)return;
    for(const unsigned char* p=reinterpret_cast<const unsigned char*>(text);*p;++p) {
      for(int col=0;col<5;++col)for(int row=0;row<8;++row)
        if(font[*p*5+col]&(1<<row))fillRect(cursorX+col*textSize,cursorY+row*textSize,textSize,textSize,ink);
      cursorX+=6*textSize;
    }
  }
  void getTextBounds(const char* text,int,int,int16_t* x,int16_t* y,uint16_t* w,uint16_t* h) {
    *x=0;*y=0;*w=strlen(text)*6*textSize;*h=8*textSize;
  }
  bool writePgm(const char* path)const {
    FILE* file=fopen(path,"wb");if(!file)return false;
    fprintf(file,"P5\n128 64\n255\n");const bool written=fwrite(pixels,1,sizeof pixels,file)==sizeof pixels;
    return fclose(file)==0&&written;
  }
};
