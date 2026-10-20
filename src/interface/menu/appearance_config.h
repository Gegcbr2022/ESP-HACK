#pragma once
#include "menu_model.h"

// Keep the EEPROM32..39 layout, leaving GPIO and boot-logo storage untouched.
struct AppearanceStorage {
  uint32_t signature;
  uint8_t menu, submenu, color, standby;
};
static_assert(sizeof(AppearanceStorage) == 8, "Appearance EEPROM layout must stay eight bytes");
static constexpr uint32_t LEGACY_APPEARANCE_SIGNATURE = 0x45535041UL;
static constexpr uint32_t APPEARANCE_STORAGE_SIGNATURE = 0x45535042UL;

inline bool normalizeAppearance(AppearanceStorage& stored, uint8_t standbyOptions) {
  const bool legacy = stored.signature == LEGACY_APPEARANCE_SIGNATURE;
  const uint8_t styleLimit = legacy ? uint8_t(MENU_STYLE_POCKET) : MAIN_MENU_STYLE_COUNT;
  if((!legacy && stored.signature != APPEARANCE_STORAGE_SIGNATURE) ||
     stored.menu >= styleLimit ||
     stored.submenu > 1 || stored.color > 1 || stored.standby >= standbyOptions)
    return false;
  if(legacy) { stored.menu = MENU_STYLE_POCKET; stored.signature = APPEARANCE_STORAGE_SIGNATURE; }
  return true;
}

inline AppearanceStorage migrateLegacySDStyle(uint8_t oldStyle, uint8_t submenu,
                                              uint8_t color, uint8_t standby) {
  (void)oldStyle; // Every pre-Pocket SD style gets the new default exactly once.
  return {APPEARANCE_STORAGE_SIGNATURE, MENU_STYLE_POCKET, submenu, color, standby};
}
