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
    // The chord that rotates the monitor under the cursor, likewise captured.
    keychord::KeyChord rotateHotkey;
    // The chord that opens the launchpad, likewise captured.
    keychord::KeyChord launchpadHotkey;
};

extern Settings g_settings;

// The keyboard hook runs on its own thread and reads the three chords while
// the settings dialog may be replacing them; both sides hold this around them.
extern SRWLOCK g_hotkeyLock;

// The out-of-the-box hotkey: MacKeys' injected left Ctrl, then Tab. The
// injected origin is what leaves a genuine physical Ctrl+Tab alone, so in-app
// tab switching keeps working.
void DefaultHotkey(keychord::KeyChord& out);

// The out-of-the-box rotate hotkey: Ctrl+Alt+R, taken however the keys arrive,
// so it works under MacKeys (where it reads as Cmd+Alt+R) and without it alike.
void DefaultRotateHotkey(keychord::KeyChord& out);

// The out-of-the-box launchpad hotkey: Left Ctrl+Space, however the keys
// arrive — the Spotlight position.
void DefaultLaunchpadHotkey(keychord::KeyChord& out);

// Persisted under HKCU\Software\Monopane.
void LoadSettings();
void SaveSettings();

bool IsAutoStartEnabled();
void SetAutoStart(bool enable);

// Modal settings dialog; updates g_settings and persists on OK.
void ShowSettingsDialog(HWND owner, HINSTANCE instance);
