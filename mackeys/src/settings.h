#pragma once
#include <windows.h>

bool IsAutoStartEnabled();
void SetAutoStart(bool enable);
// Enables autostart once on the very first launch; the settings checkbox rules
// after that.
void EnsureFirstRunAutoStart();

// Modal settings dialog. Edits a copy of g_config and commits it on OK.
void ShowSettingsDialog(HWND owner, HINSTANCE instance);

// Compares the installed kernel Scancode Map with the one g_config implies and,
// if they differ, offers to write or remove it. Returns true when a reboot is
// now pending.
bool SyncScancodeMap(HWND owner);

// Implemented in main.cpp: rebuild the hook's lookup tables after g_config
// changed.
void OnConfigChanged();
