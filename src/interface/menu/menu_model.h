#pragma once
#include <stdint.h>

// Category IDs stay stable: WiFi..Settings=0..6, SD Apps=7.
static constexpr uint8_t MAIN_MENU_ITEM_COUNT = 8;
enum MainMenuStyle : uint8_t {
  MENU_STYLE_PAGES, MENU_STYLE_LIST, MENU_STYLE_WII, MENU_STYLE_DSI, MENU_STYLE_POCKET
};
static constexpr uint8_t MAIN_MENU_STYLE_COUNT = 5;

inline uint8_t mainMenuStep(uint8_t selected, int direction) {
  selected %= MAIN_MENU_ITEM_COUNT;
  return direction < 0 ? (selected ? selected - 1 : MAIN_MENU_ITEM_COUNT - 1)
                       : (selected + 1) % MAIN_MENU_ITEM_COUNT;
}

struct PocketMenuWindow { uint8_t selected, first, count; };
inline PocketMenuWindow pocketMenuWindow(uint8_t selected) {
  selected %= MAIN_MENU_ITEM_COUNT;
  const uint8_t visible = 3;
  uint8_t first = selected ? selected - 1 : 0;
  if(first > MAIN_MENU_ITEM_COUNT - visible) first = MAIN_MENU_ITEM_COUNT - visible;
  return {selected, first, visible};
}

inline uint8_t menuStyleFromConfig(const char* name) {
  // loadConfig has already normalized the ASCII setting to lower case.
  if(!name) return MENU_STYLE_PAGES;
  const char* const names[] = {"pages", "list", "wii", "dsi", "pocket"};
  for(uint8_t style = 0; style < MAIN_MENU_STYLE_COUNT; ++style) {
    const char* a = name; const char* b = names[style];
    while(*a && *a == *b) { ++a; ++b; }
    if(!*a && !*b) return style;
  }
  const char* a = name; const char* b = "flipper";
  while(*a && *a == *b) { ++a; ++b; }
  return !*a && !*b ? MENU_STYLE_LIST : MENU_STYLE_PAGES;
}
