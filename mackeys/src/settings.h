#pragma once
#include <windows.h>

struct Settings {
    // Caps Lock sends Backspace.
    bool capsAsBackspace = true;
    // Left cmd (Left Win) acts as Left Ctrl for Mac-style copy/paste.
    bool leftCmdAsCtrl = true;
    // Right option acts as a second nav-layer key instead of a Windows key.
    // (After the kernel Scancode Map remap, right option arrives as Right Win.)
    bool rightOptLayer = false;
    // Ctrl+Space taps the Win key to open the Start menu, like Spotlight.
    // Costs the raw Ctrl+Space chord (IDE autocomplete).
    bool ctrlSpaceStart = true;
    // Left cmd+Space sends Win+Space to switch input language, like the
    // Mac input-source switcher. Takes precedence over ctrlSpaceStart.
    bool cmdSpaceLang = true;
};

extern Settings g_settings;

// Persisted under HKCU\Software\MacKeys.
void LoadSettings();
void SaveSettings();

bool IsAutoStartEnabled();
void SetAutoStart(bool enable);
// Enables autostart once on the very first launch; the settings checkbox
// rules after that.
void EnsureFirstRunAutoStart();

// Modal settings dialog; updates g_settings and persists on OK.
void ShowSettingsDialog(HWND owner, HINSTANCE instance);
