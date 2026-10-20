#ifndef LIST_MENU_H
#define LIST_MENU_H

#include "display.h"
#include "menu_model.h"

struct MenuIcon {
  uint8_t width;
  uint8_t height;
  uint8_t frameCount;
  const uint8_t* const* frames;
};

extern const MenuIcon* const menuIcons[];

unsigned long getListMenuInitialRepeatDelay();
unsigned long getListMenuRepeatDelay();
unsigned long getMenuSubmenuRepeatDelay(bool isListRootSubmenu);

void displayListMenu(DisplayType& display, byte menuIndex, int previousIndex = -1);
void resetListMenuAnimation(byte menuIndex);
#endif
