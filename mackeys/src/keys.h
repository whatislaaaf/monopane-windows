#pragma once
#include <windows.h>
#include <stdint.h>

// A physical key is identified by its set-1 scancode, with extended keys
// carrying the 0xE0 prefix (right ctrl = 0xE01D). Virtual-key codes are
// deliberately avoided as identities: they move with the keyboard layout and
// merge left/right pairs, both of which matter here.
typedef uint16_t KeyId;

constexpr KeyId kNoKey = 0;

inline KeyId MakeKeyId(DWORD scanCode, bool extended)
{
    const KeyId sc = static_cast<KeyId>(scanCode & 0xFF);
    return extended ? static_cast<KeyId>(0xE000 | sc) : sc;
}

inline bool KeyIsExtended(KeyId id) { return (id & 0xFF00) == 0xE000; }

// Dense index into the 512-entry bind and state tables.
inline int KeySlot(KeyId id) { return (id & 0xFF) | (KeyIsExtended(id) ? 0x100 : 0); }

constexpr int kKeySlots = 512;

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

// ---------------------------------------------------------------- chords

// A hotkey: some physical modifier keys held, then a trigger key. Modifiers
// are physical keys (scancodes), not the virtual keys they may be remapped
// into, so "Left Ctrl" means the key in the Ctrl position whatever it now
// sends — the same rule the picker uses.
constexpr int kMaxChordMods = 4;

struct KeyChord {
    KeyId mods[kMaxChordMods] = {};
    uint8_t modCount = 0;
    KeyId trigger = kNoKey;
};

inline bool ChordValid(const KeyChord& c) { return c.trigger != kNoKey; }

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
