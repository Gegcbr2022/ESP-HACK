#include "interface/interface.h"
#include "interface/menu/appearance_config.h"
#include "interface/ui_text.h"
#include "menu/subghz_items.h"
#include "U8g2_for_Adafruit_GFX.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
const uint8_t u8g2_font_5x7_tr[]={0};
const uint8_t u8g2_font_haxrcorp4089_tr[]={0};
const uint8_t u8g2_font_helvB08_tr[]={0};
const char* menuItems[]={"WiFi","Bluetooth","SubGHz","Infrared","GPIO","Games","Settings","SD Apps"};
byte menu=MENU_STYLE_POCKET,submenu=1;
bool sdCardReady=true;
static unsigned checks=0;
static unsigned long now=1000;
unsigned long millis(){return now;}
void delay(unsigned long ms){now+=ms;}
#define CHECK(value) do{++checks;if(!(value)){fprintf(stderr,"FAIL line%d: %s\n",__LINE__,#value);exit(1);}}while(0)
static bool hasLabel(const DisplayType& display,const char* label) {
  for(const auto& call:display.labels)if(call.value==label)return true;
  return false;
}
static void appearanceTests() {
  for(uint8_t style=0;style<4;++style)for(uint8_t sub=0;sub<2;++sub)
    for(uint8_t color=0;color<2;++color)for(uint8_t standby=0;standby<8;++standby) {
      AppearanceStorage old={LEGACY_APPEARANCE_SIGNATURE,style,sub,color,standby};
      CHECK(normalizeAppearance(old,8));CHECK(old.menu==MENU_STYLE_POCKET);
      CHECK(old.signature==APPEARANCE_STORAGE_SIGNATURE);CHECK(old.submenu==sub&&old.color==color&&old.standby==standby);
      const AppearanceStorage migrated=old;CHECK(normalizeAppearance(old,8));CHECK(!memcmp(&old,&migrated,sizeof old));
      const AppearanceStorage sd=migrateLegacySDStyle(style,sub,color,standby);
      CHECK(sd.signature==APPEARANCE_STORAGE_SIGNATURE&&sd.menu==MENU_STYLE_POCKET);
      CHECK(sd.submenu==sub&&sd.color==color&&sd.standby==standby);
      // A later deliberate choice, including Pocket, must survive future boots.
      for(uint8_t chosen=0;chosen<MAIN_MENU_STYLE_COUNT;++chosen) {
        old.menu=chosen;const AppearanceStorage saved=old;
        CHECK(normalizeAppearance(old,8));CHECK(!memcmp(&old,&saved,sizeof old));
      }
    }
  const char* names[]={"pages","list","wii","dsi","pocket","flipper"};
  const uint8_t expected[]={0,1,2,3,4,1};
  for(unsigned i=0;i<6;++i)CHECK(menuStyleFromConfig(names[i])==expected[i]);
  CHECK(menuStyleFromConfig(nullptr)==MENU_STYLE_PAGES);CHECK(menuStyleFromConfig("invalid")==MENU_STYLE_PAGES);
  AppearanceStorage invalid={0,0,0,0,0};CHECK(!normalizeAppearance(invalid,8));
  invalid={LEGACY_APPEARANCE_SIGNATURE,4,0,0,0};CHECK(!normalizeAppearance(invalid,8));
  invalid={APPEARANCE_STORAGE_SIGNATURE,5,0,0,0};CHECK(!normalizeAppearance(invalid,8));
  invalid={APPEARANCE_STORAGE_SIGNATURE,4,2,0,0};CHECK(!normalizeAppearance(invalid,8));
  invalid={APPEARANCE_STORAGE_SIGNATURE,4,0,2,0};CHECK(!normalizeAppearance(invalid,8));
  invalid={APPEARANCE_STORAGE_SIGNATURE,4,0,0,8};CHECK(!normalizeAppearance(invalid,8));
  // The exact eight-byte record cannot alter adjacent GPIO/boot-logo storage.
  uint8_t eeprom[128];memset(eeprom,0xA5,sizeof eeprom);
  const AppearanceStorage record={APPEARANCE_STORAGE_SIGNATURE,4,0,1,7};memcpy(eeprom+32,&record,sizeof record);
  for(unsigned i=0;i<128;++i)if(i<32||i>=40)CHECK(eeprom[i]==0xA5);
}
static void navigationTests() {
  CHECK(mainMenuStep(0,-1)==7);CHECK(mainMenuStep(7,1)==0);
  uint8_t selected=0;
  for(unsigned step=0;step<800;++step){selected=mainMenuStep(selected,1);CHECK(selected==(step+1)%8);}
  for(unsigned value=0;value<256;++value) {
    const auto window=pocketMenuWindow(value);
    CHECK(window.selected<8);CHECK(window.count==3);CHECK(window.first+window.count<=8);
    CHECK(window.selected>=window.first&&window.selected<window.first+window.count);
  }
  CHECK(SUBGHZ_MENU_ITEM_COUNT==8);CHECK(!strcmp(subghzMenuItems[0],"Read"));
  CHECK(!strcmp(subghzMenuItems[6],"Radio Lab"));CHECK(!strcmp(subghzMenuItems[7],"Chat"));
  CHECK((0+SUBGHZ_MENU_ITEM_COUNT-1)%SUBGHZ_MENU_ITEM_COUNT==7);
}
static void textTests(const char* directory) {
  size_t glyphs=0;
  CHECK(ui_text::validate("АЯаяЁё ABC",strlen("АЯаяЁё ABC"),&glyphs));CHECK(glyphs==10);
  CHECK(ui_text::prefixBytes("ЯЁabc",1)==2);CHECK(ui_text::prefixBytes("ЯЁabc",2)==4);
  CHECK(ui_text::prefixBytes("ЯЁabc",3)==5);CHECK(ui_text::prefixBytes("ЯЁabc",0)==0);
  CHECK(!ui_text::validate("\xd0",1));CHECK(!ui_text::validate("\xd1\x90",2));
  CHECK(!ui_text::validate("\xc0\xaf",2));CHECK(!ui_text::validate("A\n",2));
  CHECK(!ui_text::validate("A\0B",3));CHECK(!ui_text::validate("\xf0\x9f\x90\xac",4));
  char out[8];CHECK(ui_text::copyGlyphs(out,3,"ЯЁ",2)==2);CHECK(!strcmp(out,"Я"));
  CHECK(ui_text::copyGlyphs(out,2,"ЯЁ",2)==0);CHECK(out[0]==0);
  DisplayType display;
  ui_text::draw(display,0,0,"ЖЁё",SH110X_WHITE,3);
  // Known Cyrillic pixels, including both dotted letters; '?' cannot pass.
  CHECK(display.pixels[0][0]&&display.pixels[0][2]&&display.pixels[0][4]);
  CHECK(!display.pixels[0][1]&&!display.pixels[0][3]);CHECK(display.pixels[3][2]);
  CHECK(display.pixels[0][7]&&display.pixels[0][9]);CHECK(display.pixels[0][13]&&display.pixels[0][15]);
  for(unsigned x=6;x<11;++x)CHECK(display.pixels[2][x]);
  CHECK(!display.pixels[2][12]&&!display.pixels[2][16]);
  CHECK(display.pixels[2][13]&&display.pixels[2][14]&&display.pixels[2][15]);
  CHECK(display.getCursorX()==18&&display.getCursorY()==0);CHECK(display.clipped==0);
  display.fillRect(0,0,128,64,1);ui_text::draw(display,0,0,"Ж",SH110X_BLACK,1);
  CHECK(!display.pixels[0][0]&&!display.pixels[0][2]&&!display.pixels[0][4]);
  CHECK(display.pixels[0][1]&&display.pixels[0][3]);
  display.clearDisplay();ui_text::draw(display,0,0,"Ё",SH110X_WHITE,1,2);
  CHECK(display.pixels[0][2]&&display.pixels[1][3]&&display.pixels[0][6]&&display.pixels[1][7]);
  for(unsigned x=0;x<10;++x)CHECK(display.pixels[4][x]&&display.pixels[5][x]);
  CHECK(display.getCursorX()==12&&display.clipped==0);
  display.clearDisplay();display.setTextSize(2);display.setCursor(0,0);const unsigned changes=display.fontChanges;
  ui_text::print(display,"AZ");CHECK(display.getCursorX()==24);CHECK(display.fontChanges==changes);
  ui_text::draw(display,0,0,"Я",SH110X_WHITE,1);display.print("A");CHECK(display.getCursorX()==18);
  display.clearDisplay();display.setTextSize(1);display.setCursor(0,0);display.print("AZ");
  uint8_t ascii[64][128];memcpy(ascii,display.pixels,sizeof ascii);
  display.clearDisplay();ui_text::draw(display,0,0,"AZ");CHECK(!memcmp(ascii,display.pixels,sizeof ascii));
  display.clearDisplay();ui_text::draw(display,0,0,"ЖЁё",SH110X_WHITE,2);CHECK(display.getCursorX()==12);
  for(int x=-12;x<140;x+=7)for(int y=-8;y<72;y+=5) {
    display.clearDisplay();ui_text::draw(display,x,y,"ЁжЯ A",SH110X_WHITE,21,2);CHECK(display.clipped==0);
  }
  const char* alphabet[]={"АБВГДЕЁЖЗИЙКЛМНОП","РСТУФХЦЧШЩЪЫЬЭЮЯ","абвгдеёжзийклмноп","рстуфхцчшщъыьэюя"};
  display.clearDisplay();for(unsigned row=0;row<4;++row)ui_text::draw(display,0,row*16,alphabet[row]);
  char path[1024];snprintf(path,sizeof path,"%s/russian_alphabet.pgm",directory);CHECK(display.writePgm(path));
}
static void basicMenuTests(const char* directory) {
  const char* source[]={"Interface","Restart","Reset","Update","About","Menu","Submenu","Color","Standby","Boot Logo","Pages","List","White","Black","Disable","15s","1m","Back","SD Apps","Read","Read RAW","Send","Analyzer","Radio Lab","Chat"};
  const char* russian[]={"Интерфейс","Перезапуск","Сброс настроек","Обновление","О программе","Главное меню","Подменю","Цвет экрана","Таймер экрана","Логотип запуска","Страницы","Список","Белый","Чёрный","Выключено","15 сек","1 мин","Назад","Приложения SD","Чтение","Запись RAW","Передача файла","Анализатор","Радио","Чат"};
  CHECK(sizeof source==sizeof russian);
  for(unsigned i=0;i<sizeof source/sizeof source[0];++i) {
    CHECK(!strcmp(ui_menu::label(source[i]),russian[i]));size_t glyphs=0;
    CHECK(ui_text::validate(ui_menu::label(source[i]),strlen(ui_menu::label(source[i])),&glyphs));
    CHECK(glyphs>0&&glyphs<=19);
  }
  const char* identifiers[]={"FuriHalSubGhzPresetOok650Async","CAME","Princeton","/subghz/Read.sub","Wii","DSi","Pocket","Jammer","Brute"};
  for(const char* identifier:identifiers)CHECK(ui_menu::label(identifier)==identifier);
  CHECK(!strcmp(ui_menu::label(nullptr),""));
  DisplayType display;
  const char* settings[]={"Interface","Restart","Reset","Update","About"};
  const char* appearance[]={"Menu","Submenu","Color","Standby","Boot Logo"};
  for(submenu=0;submenu<2;++submenu)for(unsigned i=0;i<5;++i) {
    displaySubmenu(display,settings,5,i);CHECK(display.clipped==0);
    displaySubmenu(display,appearance,5,i,(i+4)%5);CHECK(display.clipped==0);
  }
  // Real submenu renderers must draw Cyrillic Ч, not UTF-8 bytes or '?'.
  submenu=1;displaySubmenu(display,subghzMenuItems,SUBGHZ_MENU_ITEM_COUNT,0);
  CHECK(!display.pixels[4][6]&&display.pixels[4][8]&&!display.pixels[4][10]);
  CHECK(display.pixels[7][6]&&!display.pixels[7][7]&&!display.pixels[7][10]);
  submenu=0;displaySubmenu(display,subghzMenuItems,SUBGHZ_MENU_ITEM_COUNT,0);
  CHECK(display.pixels[25][28]&&!display.pixels[25][32]&&display.pixels[25][36]);
  CHECK(!display.pixels[31][28]&&display.pixels[31][30]&&display.pixels[31][36]);
  char path[1024];
  for(submenu=0;submenu<2;++submenu) {
    displaySubmenu(display,settings,5,0);
    snprintf(path,sizeof path,"%s/settings_%u.pgm",directory,unsigned(submenu));CHECK(display.writePgm(path));
    displaySubmenu(display,appearance,5,4);
    snprintf(path,sizeof path,"%s/interface_%u.pgm",directory,unsigned(submenu));CHECK(display.writePgm(path));
  }
}
static void renderTests(const char* directory) {
  DisplayType display;
  for(unsigned raw=0;raw<256;++raw)for(unsigned sd=0;sd<2;++sd) {
    sdCardReady=sd;displayPocketMenu(display,uint8_t(raw));const auto window=pocketMenuWindow(raw);
    CHECK(display.clipped==0);CHECK(display.rounded.size()==1);CHECK(display.rounded[0].x==1&&display.rounded[0].w==120);
    CHECK(display.rounded[0].y==12+(window.selected-window.first)*14);CHECK(display.rounded[0].color==1);
    CHECK(hasLabel(display,sd?"SD":"--"));
    char position[6];snprintf(position,sizeof position,"%02u/08",unsigned(window.selected+1));CHECK(hasLabel(display,position));
    for(unsigned row=0;row<3;++row) {
      const char* label=pocketMenuLabel(window.first+row);size_t glyphs=0;
      CHECK(ui_text::validate(label,strlen(label),&glyphs));CHECK(glyphs>0&&glyphs<=14);
    }
    for(const auto& call:display.labels) {
      CHECK(call.x>=0&&call.y>=0);CHECK(call.x+int(call.value.size())*6<=128);CHECK(call.y+8<=64);
    }
  }
  sdCardReady=true;
  // Exercise actual dispatcher and every existing renderer with every category.
  for(menu=0;menu<MAIN_MENU_STYLE_COUNT;++menu)for(byte item=0;item<8;++item) {
    resetMainMenuAnimation(item);const unsigned before=display.frames;OLED_printMenu(display,item);
    CHECK(display.frames>before);OLED_printMenuAnimated(display,mainMenuStep(item,1),item);
    OLED_printMenuAnimated(display,item,mainMenuStep(item,1));
  }
  menu=MENU_STYLE_POCKET;
  for(submenu=0;submenu<2;++submenu)for(uint8_t item=0;item<SUBGHZ_MENU_ITEM_COUNT;++item) {
    const unsigned before=display.frames;
    displaySubmenu(display,subghzMenuItems,SUBGHZ_MENU_ITEM_COUNT,item);
    CHECK(display.frames>before);CHECK(display.clipped==0);
  }
  for(byte item=0;item<8;++item) {
    char path[1024];snprintf(path,sizeof path,"%s/pocket_%u.pgm",directory,unsigned(item));
    OLED_printMenu(display,item);CHECK(display.writePgm(path));
  }
  sdCardReady=false;OLED_printMenu(display,7);char path[1024];
  snprintf(path,sizeof path,"%s/pocket_no_sd.pgm",directory);CHECK(display.writePgm(path));
  display.clearDisplay();pocket_draw::listRow(display,12,"A very long application title",true,7);
  CHECK(display.clipped==0);CHECK(hasLabel(display,"A very long..."));
}
int main(int argc,char** argv) {
  if(argc!=2){fprintf(stderr,"Pass a snapshot directory\n");return 2;}
  appearanceTests();navigationTests();textTests(argv[1]);basicMenuTests(argv[1]);renderTests(argv[1]);
  printf("Pocket UI: %u checks PASS (Russian glyph pixels/UTF8, settings/submenu labels, all8 categories/all5 styles, bounds, navigation, migration); 14 PGM snapshots\n",checks);
}
