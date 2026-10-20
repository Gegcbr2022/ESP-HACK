#include "interface/interface.h"
#include "interface/menu/appearance_config.h"
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
static void renderTests(const char* directory) {
  DisplayType display;
  for(unsigned raw=0;raw<256;++raw)for(unsigned sd=0;sd<2;++sd) {
    sdCardReady=sd;displayPocketMenu(display,uint8_t(raw));const auto window=pocketMenuWindow(raw);
    CHECK(display.clipped==0);CHECK(display.rounded.size()==1);CHECK(display.rounded[0].x==1&&display.rounded[0].w==120);
    CHECK(display.rounded[0].y==12+(window.selected-window.first)*14);CHECK(display.rounded[0].color==1);
    CHECK(hasLabel(display,sd?"SD":"--"));CHECK(hasLabel(display,"UP/DN move   OK open"));
    char position[6];snprintf(position,sizeof position,"%02u/08",unsigned(window.selected+1));CHECK(hasLabel(display,position));
    for(unsigned row=0;row<3;++row) {
      const char* label=menuItems[window.first+row];CHECK(hasLabel(display,label));
      for(const auto& call:display.labels)if(call.value==label)CHECK(call.color==int(window.first+row!=window.selected));
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
    CHECK(display.frames>before);CHECK(hasLabel(display,subghzMenuItems[item]));
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
  appearanceTests();navigationTests();renderTests(argv[1]);
  printf("Pocket UI: %u checks PASS (all8 categories/all5 styles, pixel bounds, wrap, SD states, one-time migration); 9 PGM snapshots\n",checks);
}
