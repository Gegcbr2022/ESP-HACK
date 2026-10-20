#pragma once
#include <stdint.h>

// Preserve every existing action ID; Chat is reachable by UP from Read.
static constexpr uint8_t SUBGHZ_MENU_ITEM_COUNT = 8;
static const char* const subghzMenuItems[] = {
  "Read", "Read RAW", "Send", "Analyzer", "Jammer", "Brute", "Radio Lab", "Chat"
};
static_assert(sizeof(subghzMenuItems) / sizeof(subghzMenuItems[0]) == SUBGHZ_MENU_ITEM_COUNT,
              "Every SubGHz action must have a menu label");
