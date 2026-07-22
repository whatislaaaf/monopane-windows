#pragma once
#include <windows.h>

struct Settings {
    // When activating a window, bring every window of the same app forward
    // (the selected one gets focus). Off = activate only the selected window.
    bool activateAllOfApp = false;
    // Preselect the previously active window when the switcher opens.
    bool preselectPrevious = true;
    // Fuzzy-match against app names in addition to window titles.
    bool matchAppName = true;
};

extern Settings g_settings;

// Persisted under HKCU\Software\Monopane.
void LoadSettings();
void SaveSettings();

bool IsAutoStartEnabled();
void SetAutoStart(bool enable);

// Modal settings dialog; updates g_settings and persists on OK.
void ShowSettingsDialog(HWND owner, HINSTANCE instance);
