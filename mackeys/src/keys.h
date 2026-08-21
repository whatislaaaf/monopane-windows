#pragma once
#include <windows.h>
#include <stdint.h>

#include "../external/keychord/keychord.h"

// Key identity, chords and capture are shared with Monopane through the
// keychord submodule; what stays here is what only MacKeys does with them.
using keychord::ChordValid;
using keychord::ChordMatches;
using keychord::FormatChord;
using keychord::ParseChord;
using keychord::KeyChord;
using keychord::KeyId;
using keychord::KeyIsExtended;
using keychord::KeyOrigin;
using keychord::KeySlot;
using keychord::kKeySlots;
using keychord::kMaxChordMods;
using keychord::kNoKey;
using keychord::MakeKeyId;

enum class Action : uint8_t {
    None = 0,     // unbound — passes through untouched
    Key,          // sends another key (see Bind::target)
    Layer,        // hold to activate the nav layer; swallowed when tapped alone
    WinKey,       // acts as a real Windows key
    CtrlSwap,     // acts as Left Ctrl, and gates the Mac cmd+arrow chords
    DesktopPrev,  // Ctrl+Win+Left
    DesktopNext,  // Ctrl+Win+Right
};

struct Bind {
    Action action = Action::None;
    KeyId target = kNoKey; // Action::Key only
};

// Scancodes the kernel Scancode Map can divert a Win key onto. F23/F24 are
// used because nothing else emits them, so no application can be confused by
// one arriving. See scancodemap.h.
constexpr KeyId kSpareF23 = 0x6E;
constexpr KeyId kSpareF24 = 0x76;
constexpr KeyId kScLeftWin = 0xE05B;
constexpr KeyId kScRightWin = 0xE05C;

// ---------------------------------------------------------------- hotkeys

enum class ChordAction : uint8_t {
    None = 0,
    StartMenu,     // tap Win — the Spotlight stand-in
    InputLanguage, // Win+Space
    SendChord,     // send ChordBinding::to
};

struct ChordBinding {
    KeyChord from;
    ChordAction action = ChordAction::None;
    KeyChord to; // SendChord only
};

constexpr size_t kMaxChords = 16;
