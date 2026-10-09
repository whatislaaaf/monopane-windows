#pragma once
#include <windows.h>

// Guards the live settings — g_settings (settings.h) and g_config (config.h)
// — between the UI thread and the hook thread. The hook takes it shared to
// copy out what it needs; the settings dialog takes it exclusive while it
// replaces them on OK. Defined in main.cpp.
extern SRWLOCK g_settingsLock;
