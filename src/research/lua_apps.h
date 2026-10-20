#pragma once
// Native menu entry. New /apps/*.lua scripts need no further firmware rebuild.
void runSdApps();
// Serial/main-menu diagnostic: real VM, LCD draw, instruction quota smoke test.
void sdAppsSelfTest();
