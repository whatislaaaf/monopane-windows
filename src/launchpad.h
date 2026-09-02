#pragma once
#include <windows.h>

// The launchpad: a centred grid of the apps you pin, opened by its own hotkey.
// Type to filter, arrows to move, Enter to open (Shift+Enter for a fresh
// instance even if it is already running). The pencil in the search row
// switches to edit mode: drag tiles to reorder, the cross on a tile removes
// it, clicking its name renames it, and the "+" tile adds an installed app or
// any file.

HWND CreateLaunchpadWindow(HINSTANCE instance);
void ShowLaunchpad();
void HideLaunchpad();
bool LaunchpadVisible();

// Explorer on launchpad.ini, for the Settings button.
void OpenLaunchpadConfigFolder();

// Frees fonts, brushes and icons. Call once at shutdown.
void DestroyLaunchpadResources();
