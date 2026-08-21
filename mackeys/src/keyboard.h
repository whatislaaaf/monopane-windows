#pragma once
#include <string>

#include "config.h"
#include "keys.h"

// ------------------------------------------------------------ key naming

// Printable name for a physical key ("Caps Lock", "Right Alt", "H").
const wchar_t* KeyName(KeyId id);
// "Caps Lock -> Backspace" style, ASCII, for the comments in mackeys.ini.
std::string DescribeBindAscii(KeyId id, const Bind& bind);
// What a bind does, for the picker's status line.
std::wstring DescribeBind(const Bind& bind);

// ------------------------------------------------------------ board layout

enum KeyCapFlags : uint8_t {
    KCF_NONE = 0,
    KCF_ANSI_ONLY = 1 << 0,
    KCF_ISO_ONLY = 1 << 1,
    // Never reaches a keyboard hook (keyboard-firmware Fn) or arrives as a
    // multi-scancode sequence we refuse to bind (PrtSc, Pause).
    KCF_UNBINDABLE = 1 << 2,
};

// Geometry is in centi-units: 100 = one standard key width.
struct KeyCap {
    KeyId id;
    const wchar_t* label;
    short x, y, w, h;
    uint8_t flags;
};

constexpr int kBoardW = 1825;
constexpr int kBoardH = 650;

const KeyCap* BoardKeys(BoardLayout layout, int& count);

// ------------------------------------------------------- the picker control

extern const wchar_t kKeyboardClass[];

void RegisterKeyboardControl(HINSTANCE instance);

// wParam: Config*. The control edits it in place.
constexpr UINT KBM_SETCONFIG = WM_USER + 100;
// wParam: 0 = base layer, 1 = nav layer.
constexpr UINT KBM_SETVIEW = WM_USER + 101;
// Sent to the parent whenever a bind changes or the selection moves, so it can
// refresh its status text. lParam: the control HWND.
constexpr UINT KBM_CHANGED = WM_USER + 102;
// Sent to the parent when the picker wants a line of status text shown.
constexpr UINT KBM_STATUS = WM_USER + 103;

// Current status/prompt line for the parent to display.
std::wstring KeyboardStatusText(HWND control);

// True while the picker is armed and waiting for a keystroke. The hook routes
// raw keys here and swallows them so nothing else in Windows reacts to the
// keypress being captured.
bool CaptureActive();
bool ForwardCaptureKey(KeyId id, bool down);

// ------------------------------------------------------------------ chords

std::wstring DescribeChord(const KeyChord& chord);         // "Left Ctrl + Space"
std::wstring DescribeChordAction(const ChordBinding& b); // "Start menu"
std::wstring DescribeChordBinding(const ChordBinding& b);
std::string DescribeChordBindingAscii(const ChordBinding& b);

// Chord capture for the Options tab. Arming swallows every keystroke; the
// chord completes when the last key is released, so the trigger is whichever
// key was pressed last. Esc as the first key cancels, as does a click, focus
// loss, or the same 15-second timeout the picker uses.
constexpr UINT CHM_CAPTURED = WM_USER + 110;
constexpr UINT CHM_CANCELLED = WM_USER + 111;

bool BeginChordCapture(HWND notify);
void CancelChordCapture();
KeyChord CapturedChord();
