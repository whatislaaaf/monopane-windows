#include "keychord.h"

namespace keychord {
namespace {

struct Entry {
    KeyId id;
    const wchar_t* name;
};

// Names rather than keycap legends: "Semicolon" reads better than ";" inside a
// sentence, and left/right pairs have to be distinguishable at a glance since
// telling them apart is the point of scancode identity.
const Entry kNames[] = {
    { 0x01, L"Esc" },
    { 0x02, L"1" },          { 0x03, L"2" },          { 0x04, L"3" },
    { 0x05, L"4" },          { 0x06, L"5" },          { 0x07, L"6" },
    { 0x08, L"7" },          { 0x09, L"8" },          { 0x0A, L"9" },
    { 0x0B, L"0" },
    { 0x0C, L"Minus" },      { 0x0D, L"Equals" },     { 0x0E, L"Backspace" },
    { 0x0F, L"Tab" },
    { 0x10, L"Q" },          { 0x11, L"W" },          { 0x12, L"E" },
    { 0x13, L"R" },          { 0x14, L"T" },          { 0x15, L"Y" },
    { 0x16, L"U" },          { 0x17, L"I" },          { 0x18, L"O" },
    { 0x19, L"P" },
    { 0x1A, L"Left Bracket" }, { 0x1B, L"Right Bracket" }, { 0x1C, L"Enter" },
    { 0x1D, L"Left Ctrl" },
    { 0x1E, L"A" },          { 0x1F, L"S" },          { 0x20, L"D" },
    { 0x21, L"F" },          { 0x22, L"G" },          { 0x23, L"H" },
    { 0x24, L"J" },          { 0x25, L"K" },          { 0x26, L"L" },
    { 0x27, L"Semicolon" },  { 0x28, L"Quote" },      { 0x29, L"Backtick" },
    { 0x2A, L"Left Shift" }, { 0x2B, L"Backslash" },
    { 0x2C, L"Z" },          { 0x2D, L"X" },          { 0x2E, L"C" },
    { 0x2F, L"V" },          { 0x30, L"B" },          { 0x31, L"N" },
    { 0x32, L"M" },
    { 0x33, L"Comma" },      { 0x34, L"Period" },     { 0x35, L"Slash" },
    { 0x36, L"Right Shift" },
    { 0x37, L"Numpad *" },   { 0x38, L"Left Alt" },   { 0x39, L"Space" },
    { 0x3A, L"Caps Lock" },
    { 0x3B, L"F1" },         { 0x3C, L"F2" },         { 0x3D, L"F3" },
    { 0x3E, L"F4" },         { 0x3F, L"F5" },         { 0x40, L"F6" },
    { 0x41, L"F7" },         { 0x42, L"F8" },         { 0x43, L"F9" },
    { 0x44, L"F10" },
    { 0x45, L"Pause" },      { 0x46, L"Scroll Lock" },
    { 0x47, L"Numpad 7" },   { 0x48, L"Numpad 8" },   { 0x49, L"Numpad 9" },
    { 0x4A, L"Numpad -" },   { 0x4B, L"Numpad 4" },   { 0x4C, L"Numpad 5" },
    { 0x4D, L"Numpad 6" },   { 0x4E, L"Numpad +" },   { 0x4F, L"Numpad 1" },
    { 0x50, L"Numpad 2" },   { 0x51, L"Numpad 3" },   { 0x52, L"Numpad 0" },
    { 0x53, L"Numpad ." },
    { 0x56, L"ISO Backslash" },
    { 0x57, L"F11" },        { 0x58, L"F12" },
    { 0x64, L"F13" },        { 0x65, L"F14" },        { 0x66, L"F15" },
    { 0x67, L"F16" },        { 0x68, L"F17" },        { 0x69, L"F18" },
    { 0x6A, L"F19" },        { 0x6B, L"F20" },        { 0x6C, L"F21" },
    { 0x6D, L"F22" },        { 0x6E, L"F23" },        { 0x76, L"F24" },

    { 0xE01C, L"Numpad Enter" }, { 0xE01D, L"Right Ctrl" },
    { 0xE035, L"Numpad /" },     { 0xE037, L"Print Screen" },
    { 0xE038, L"Right Alt" },    { 0xE045, L"Num Lock" },
    { 0xE047, L"Home" },         { 0xE048, L"Up" },
    { 0xE049, L"Page Up" },      { 0xE04B, L"Left" },
    { 0xE04D, L"Right" },        { 0xE04F, L"End" },
    { 0xE050, L"Down" },         { 0xE051, L"Page Down" },
    { 0xE052, L"Insert" },       { 0xE053, L"Delete" },
    { 0xE05B, L"Left Win" },     { 0xE05C, L"Right Win" },
    { 0xE05D, L"Menu" },
};

} // namespace

const wchar_t* KeyName(KeyId id)
{
    for (const Entry& e : kNames)
        if (e.id == id)
            return e.name;

    // No word for it — show the scancode rather than inventing a name.
    // Rotating buffers so several names can be built for one sentence without
    // the later ones clobbering the earlier.
    static wchar_t buffers[4][16];
    static int next = 0;
    wchar_t* buf = buffers[next];
    next = (next + 1) % 4;
    if (KeyIsExtended(id))
        wsprintfW(buf, L"Key E0%02X", id & 0xFF);
    else
        wsprintfW(buf, L"Key %02X", id & 0xFF);
    return buf;
}

std::string KeyNameAscii(KeyId id)
{
    std::string out;
    for (const wchar_t* p = KeyName(id); *p; ++p)
        out += (*p >= 32 && *p < 127) ? static_cast<char>(*p) : '?';
    return out;
}

} // namespace keychord
