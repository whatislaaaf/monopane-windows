#pragma once
#include <windows.h>

#include "../external/keychord/keychord.h"

struct Settings {
    // When activating a window, bring every window of the same app forward
    // (the selected one gets focus). Off = activate only the selected window.
    bool activateAllOfApp = false;
    // Preselect the previously active window when the switcher opens.
    bool preselectPrevious = true;
    // Fuzzy-match against app names in addition to window titles.
    bool matchAppName = true;
    // The chord that opens the switcher, captured by pressing it in Settings.
    keychord::KeyChord hotkey;
};

extern Settings g_settings;

// The out-of-the-box hotkey: MacKeys' injected left Ctrl, then Tab. The
// injected origin is what leaves a genuine physical Ctrl+Tab alone, so in-app
// tab switching keeps working.
void DefaultHotkey(keychord::KeyChord& out);

// Persisted under HKCU\Software\Monopane.
void LoadSettings();
void SaveSettings();

bool IsAutoStartEnabled();
void SetAutoStart(bool enable);

// Modal settings dialog; updates g_settings and persists on OK.
void ShowSettingsDialog(HWND owner, HINSTANCE instance);
