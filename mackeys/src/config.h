#pragma once
#include <string>
#include <vector>

#include "keys.h"

enum class BoardLayout : uint8_t { Auto = 0, Ansi, Iso };

struct Config {
    // Indexed by KeySlot(). `base` applies always; `nav` applies only while a
    // Action::Layer key is held.
    Bind base[kKeySlots];
    Bind nav[kKeySlots];

    // Hotkeys, which span several keys and so can't be drawn on the picker.
    // Matched exactly: every listed modifier must be held and nothing else,
    // which is what makes left Ctrl distinguishable from right.
    std::vector<ChordBinding> chords;

    BoardLayout layout = BoardLayout::Auto;
};

extern Config g_config;

// %APPDATA%\MacKeys and the mackeys.ini inside it.
std::wstring ConfigDir();
std::wstring ConfigPath();

// Loads mackeys.ini, falling back to defaults (and migrating the legacy HKCU
// settings) when it is missing. Always leaves g_config usable.
void LoadConfig();
bool SaveConfig();

// Writes the file if needed, then opens Explorer with mackeys.ini selected.
void OpenConfigFolder();

// A sensible starting point for a standard Windows-layout board: Caps sends
// Backspace, right Alt is the nav layer, ijkl/h/; navigate, u/o switch
// desktops. The bottom-row modifiers are left alone for the picker to assign.
void ResetToDefaults(Config& cfg);

// ANSI vs ISO, resolving BoardLayout::Auto by probing the active layout.
BoardLayout ResolvedLayout();
// Same, but for a config being edited rather than the live one.
BoardLayout ResolvedLayoutFor(const Config& cfg);
