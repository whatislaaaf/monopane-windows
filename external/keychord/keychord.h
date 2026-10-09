#pragma once
// keychord — physical key identity, chords, and hotkey capture for Win32
// keyboard-hook apps.
//
// Shared by MacKeys and Monopane. Everything here works in terms of set-1
// scancodes rather than virtual-key codes, so left and right modifiers stay
// distinct and bindings do not move when the input language does.

#include <windows.h>
#include <stdint.h>

#include <string>

namespace keychord {

// ------------------------------------------------------------------ key ids

// A physical key, identified by its set-1 scancode, with extended keys
// carrying the 0xE0 prefix (right ctrl = 0xE01D).
typedef uint16_t KeyId;

constexpr KeyId kNoKey = 0;
constexpr int kKeySlots = 512;

inline KeyId MakeKeyId(DWORD scanCode, bool extended)
{
    const KeyId sc = static_cast<KeyId>(scanCode & 0xFF);
    return extended ? static_cast<KeyId>(0xE000 | sc) : sc;
}

inline bool KeyIsExtended(KeyId id) { return (id & 0xFF00) == 0xE000; }

// Dense index for 512-entry lookup tables.
inline int KeySlot(KeyId id) { return (id & 0xFF) | (KeyIsExtended(id) ? 0x100 : 0); }

// Printable name for a key ("Caps Lock", "Right Alt", "H"), falling back to
// "Key E05B" for anything without a word for it.
const wchar_t* KeyName(KeyId id);
std::string KeyNameAscii(KeyId id);

// ------------------------------------------------------------------- chords

// Where a key event came from. A remapper like MacKeys swallows the physical
// key and injects a replacement stamped with its own marker, so the two are
// worth telling apart: it is what lets a hotkey on the remapped Ctrl leave a
// genuine physical Ctrl+Tab alone.
enum class KeyOrigin : uint8_t {
    Any = 0,  // matches however the key arrives
    Physical, // a real keypress only
    Injected, // synthesized by another tool only
};

constexpr int kMaxChordMods = 4;

struct KeyChord {
    KeyId mods[kMaxChordMods] = {};
    KeyOrigin modOrigin[kMaxChordMods] = {};
    uint8_t modCount = 0;
    KeyId trigger = kNoKey;
    KeyOrigin triggerOrigin = KeyOrigin::Any;
};

inline bool ChordValid(const KeyChord& c) { return c.trigger != kNoKey; }

// Text form: keys joined by '+', the last one the trigger. A key may carry an
// origin prefix — "injected:1D+0F" is a MacKeys-injected left ctrl, then Tab.
bool ParseChord(const std::string& text, KeyChord& out);
std::string FormatChord(const KeyChord& c);

std::wstring DescribeChord(const KeyChord& c);      // "Left Ctrl + Tab"
std::string DescribeChordAscii(const KeyChord& c);

// True when every listed modifier is held. `held` is indexed by KeySlot() and
// reports the origin each key is currently down as, or KeyOrigin::Any for keys
// that are up.
//
// `exact` also requires that nothing else is held, which is what separates
// Left Ctrl + Space from Ctrl + Shift + Space. Leave it off for a hotkey that
// must tolerate extra modifiers — Cmd+Shift+Tab reaching a Cmd+Tab switcher.
bool ChordMatches(const KeyChord& c, const KeyOrigin* held, int heldCount, bool exact);

// ------------------------------------------------------------------ capture

// Posted to the notify window when a chord completes or is abandoned.
constexpr UINT WM_CHORD_CAPTURED = WM_USER + 110;
constexpr UINT WM_CHORD_CANCELLED = WM_USER + 111;

// Arms capture. Every keystroke is swallowed until the last key comes back up,
// so the timeout matters: it is the backstop that stops an abandoned capture
// eating the keyboard. Esc as the first key cancels.
bool BeginChordCapture(HWND notify, UINT timeoutMs = 15000);
void CancelChordCapture();
bool ChordCaptureActive();

// Feed from the app's WH_KEYBOARD_LL callback, ahead of its own handling.
// Returns true when the event was consumed and must not be passed on.
bool FeedChordKey(KeyId id, bool down, KeyOrigin origin);

// Valid after WM_CHORD_CAPTURED.
KeyChord CapturedChord();

} // namespace keychord
