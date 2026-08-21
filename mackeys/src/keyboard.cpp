#include "keyboard.h"

#include <windowsx.h>

#include <algorithm>

#include <vector>

const wchar_t kKeyboardClass[] = L"MacKeysKeyboard";

namespace {

// NOMINMAX is on, so these are the std versions, not the Windows macros.
using std::max;
using std::min;

// --------------------------------------------------------------- the board
//
// One table for both layouts; the ANSI/ISO-only entries carry their own
// geometry, so switching layout is a filter rather than a second table.
// Geometry is in centi-units — 100 is one standard key width.

const KeyCap kBoard[] = {
    // function row
    { 0x01,   L"Esc",     0,    0, 100, 100, KCF_NONE },
    { 0x3B,   L"F1",      200,  0, 100, 100, KCF_NONE },
    { 0x3C,   L"F2",      300,  0, 100, 100, KCF_NONE },
    { 0x3D,   L"F3",      400,  0, 100, 100, KCF_NONE },
    { 0x3E,   L"F4",      500,  0, 100, 100, KCF_NONE },
    { 0x3F,   L"F5",      650,  0, 100, 100, KCF_NONE },
    { 0x40,   L"F6",      750,  0, 100, 100, KCF_NONE },
    { 0x41,   L"F7",      850,  0, 100, 100, KCF_NONE },
    { 0x42,   L"F8",      950,  0, 100, 100, KCF_NONE },
    { 0x43,   L"F9",      1100, 0, 100, 100, KCF_NONE },
    { 0x44,   L"F10",     1200, 0, 100, 100, KCF_NONE },
    { 0x57,   L"F11",     1300, 0, 100, 100, KCF_NONE },
    { 0x58,   L"F12",     1400, 0, 100, 100, KCF_NONE },
    { 0xE037, L"PrtSc",   1525, 0, 100, 100, KCF_UNBINDABLE },
    { 0x46,   L"ScrLk",   1625, 0, 100, 100, KCF_NONE },
    { 0x45,   L"Pause",   1725, 0, 100, 100, KCF_UNBINDABLE },

    // number row
    { 0x29,   L"`",       0,    150, 100, 100, KCF_NONE },
    { 0x02,   L"1",       100,  150, 100, 100, KCF_NONE },
    { 0x03,   L"2",       200,  150, 100, 100, KCF_NONE },
    { 0x04,   L"3",       300,  150, 100, 100, KCF_NONE },
    { 0x05,   L"4",       400,  150, 100, 100, KCF_NONE },
    { 0x06,   L"5",       500,  150, 100, 100, KCF_NONE },
    { 0x07,   L"6",       600,  150, 100, 100, KCF_NONE },
    { 0x08,   L"7",       700,  150, 100, 100, KCF_NONE },
    { 0x09,   L"8",       800,  150, 100, 100, KCF_NONE },
    { 0x0A,   L"9",       900,  150, 100, 100, KCF_NONE },
    { 0x0B,   L"0",       1000, 150, 100, 100, KCF_NONE },
    { 0x0C,   L"-",       1100, 150, 100, 100, KCF_NONE },
    { 0x0D,   L"=",       1200, 150, 100, 100, KCF_NONE },
    { 0x0E,   L"Bksp",    1300, 150, 200, 100, KCF_NONE },
    { 0xE052, L"Ins",     1525, 150, 100, 100, KCF_NONE },
    { 0xE047, L"Home",    1625, 150, 100, 100, KCF_NONE },
    { 0xE049, L"PgUp",    1725, 150, 100, 100, KCF_NONE },

    // top letter row
    { 0x0F,   L"Tab",     0,    250, 150, 100, KCF_NONE },
    { 0x10,   L"Q",       150,  250, 100, 100, KCF_NONE },
    { 0x11,   L"W",       250,  250, 100, 100, KCF_NONE },
    { 0x12,   L"E",       350,  250, 100, 100, KCF_NONE },
    { 0x13,   L"R",       450,  250, 100, 100, KCF_NONE },
    { 0x14,   L"T",       550,  250, 100, 100, KCF_NONE },
    { 0x15,   L"Y",       650,  250, 100, 100, KCF_NONE },
    { 0x16,   L"U",       750,  250, 100, 100, KCF_NONE },
    { 0x17,   L"I",       850,  250, 100, 100, KCF_NONE },
    { 0x18,   L"O",       950,  250, 100, 100, KCF_NONE },
    { 0x19,   L"P",       1050, 250, 100, 100, KCF_NONE },
    { 0x1A,   L"[",       1150, 250, 100, 100, KCF_NONE },
    { 0x1B,   L"]",       1250, 250, 100, 100, KCF_NONE },
    { 0x2B,   L"\\",      1350, 250, 150, 100, KCF_ANSI_ONLY },
    { 0xE053, L"Del",     1525, 250, 100, 100, KCF_NONE },
    { 0xE04F, L"End",     1625, 250, 100, 100, KCF_NONE },
    { 0xE051, L"PgDn",    1725, 250, 100, 100, KCF_NONE },

    // home row
    { 0x3A,   L"Caps",    0,    350, 175, 100, KCF_NONE },
    { 0x1E,   L"A",       175,  350, 100, 100, KCF_NONE },
    { 0x1F,   L"S",       275,  350, 100, 100, KCF_NONE },
    { 0x20,   L"D",       375,  350, 100, 100, KCF_NONE },
    { 0x21,   L"F",       475,  350, 100, 100, KCF_NONE },
    { 0x22,   L"G",       575,  350, 100, 100, KCF_NONE },
    { 0x23,   L"H",       675,  350, 100, 100, KCF_NONE },
    { 0x24,   L"J",       775,  350, 100, 100, KCF_NONE },
    { 0x25,   L"K",       875,  350, 100, 100, KCF_NONE },
    { 0x26,   L"L",       975,  350, 100, 100, KCF_NONE },
    { 0x27,   L";",       1075, 350, 100, 100, KCF_NONE },
    { 0x28,   L"'",       1175, 350, 100, 100, KCF_NONE },
    { 0x1C,   L"Enter",   1275, 350, 225, 100, KCF_ANSI_ONLY },
    { 0x2B,   L"#",       1275, 350, 100, 100, KCF_ISO_ONLY },
    { 0x1C,   L"Enter",   1350, 250, 150, 200, KCF_ISO_ONLY },

    // bottom letter row
    { 0x2A,   L"Shift",   0,    450, 225, 100, KCF_ANSI_ONLY },
    { 0x2A,   L"Shift",   0,    450, 125, 100, KCF_ISO_ONLY },
    { 0x56,   L"\\",      125,  450, 100, 100, KCF_ISO_ONLY },
    { 0x2C,   L"Z",       225,  450, 100, 100, KCF_NONE },
    { 0x2D,   L"X",       325,  450, 100, 100, KCF_NONE },
    { 0x2E,   L"C",       425,  450, 100, 100, KCF_NONE },
    { 0x2F,   L"V",       525,  450, 100, 100, KCF_NONE },
    { 0x30,   L"B",       625,  450, 100, 100, KCF_NONE },
    { 0x31,   L"N",       725,  450, 100, 100, KCF_NONE },
    { 0x32,   L"M",       825,  450, 100, 100, KCF_NONE },
    { 0x33,   L",",       925,  450, 100, 100, KCF_NONE },
    { 0x34,   L".",       1025, 450, 100, 100, KCF_NONE },
    { 0x35,   L"/",       1125, 450, 100, 100, KCF_NONE },
    { 0x36,   L"Shift",   1225, 450, 275, 100, KCF_NONE },
    { 0xE048, L"\x2191",  1625, 450, 100, 100, KCF_NONE },

    // modifier row
    { 0x1D,   L"Ctrl",    0,    550, 125, 100, KCF_NONE },
    { 0xE05B, L"Win",     125,  550, 125, 100, KCF_NONE },
    { 0x38,   L"Alt",     250,  550, 125, 100, KCF_NONE },
    { 0x39,   L"Space",   375,  550, 625, 100, KCF_NONE },
    { 0xE038, L"Alt",     1000, 550, 125, 100, KCF_NONE },
    { kNoKey, L"Fn",      1125, 550, 125, 100, KCF_UNBINDABLE },
    { 0xE05D, L"Menu",    1250, 550, 125, 100, KCF_NONE },
    { 0xE01D, L"Ctrl",    1375, 550, 125, 100, KCF_NONE },
    { 0xE04B, L"\x2190",  1525, 550, 100, 100, KCF_NONE },
    { 0xE050, L"\x2193",  1625, 550, 100, 100, KCF_NONE },
    { 0xE04D, L"\x2192",  1725, 550, 100, 100, KCF_NONE },
};

std::vector<KeyCap> g_ansi;
std::vector<KeyCap> g_iso;

void BuildFilteredBoards()
{
    if (!g_ansi.empty())
        return;
    for (const KeyCap& k : kBoard) {
        if (!(k.flags & KCF_ISO_ONLY))
            g_ansi.push_back(k);
        if (!(k.flags & KCF_ANSI_ONLY))
            g_iso.push_back(k);
    }
}

// ------------------------------------------------------------------ naming

struct NameOverride {
    KeyId id;
    const wchar_t* name;
};

// Where the keycap legend alone would be ambiguous or unreadable in a
// sentence ("' -> Home" is worse than "Quote -> Home").
const NameOverride kNames[] = {
    { 0x1D,   L"Left Ctrl" },   { 0xE01D, L"Right Ctrl" },
    { 0x38,   L"Left Alt" },    { 0xE038, L"Right Alt" },
    { 0x2A,   L"Left Shift" },  { 0x36,   L"Right Shift" },
    { 0xE05B, L"Left Win" },    { 0xE05C, L"Right Win" },
    { 0xE05D, L"Menu" },        { 0x3A,   L"Caps Lock" },
    { 0x0E,   L"Backspace" },   { 0x1C,   L"Enter" },
    { 0x39,   L"Space" },       { 0x0F,   L"Tab" },
    { 0x01,   L"Esc" },
    { 0xE048, L"Up" },          { 0xE050, L"Down" },
    { 0xE04B, L"Left" },        { 0xE04D, L"Right" },
    { 0xE047, L"Home" },        { 0xE04F, L"End" },
    { 0xE049, L"Page Up" },     { 0xE051, L"Page Down" },
    { 0xE052, L"Insert" },      { 0xE053, L"Delete" },
    { 0x27,   L"Semicolon" },   { 0x28,   L"Quote" },
    { 0x29,   L"Backtick" },    { 0x33,   L"Comma" },
    { 0x34,   L"Period" },      { 0x35,   L"Slash" },
    { 0x2B,   L"Backslash" },   { 0x56,   L"ISO Backslash" },
    { 0x1A,   L"Left Bracket" },{ 0x1B,   L"Right Bracket" },
    { 0x0C,   L"Minus" },       { 0x0D,   L"Equals" },
    { 0x6E,   L"F23" },         { 0x76,   L"F24" },
};

std::string ToAscii(const std::wstring& s)
{
    std::string out;
    for (wchar_t c : s)
        out += (c >= 32 && c < 127) ? static_cast<char>(c) : '?';
    return out;
}

// -------------------------------------------------------------- the control

constexpr UINT kCaptureTimer = 1;
constexpr UINT kChordCaptureTimer = 2;
constexpr UINT kCaptureTimeoutMs = 15000;

constexpr UINT kMenuPressKey = 1;
constexpr UINT kMenuLayer = 2;
constexpr UINT kMenuWinKey = 3;
constexpr UINT kMenuCtrl = 4;
constexpr UINT kMenuDesktopPrev = 5;
constexpr UINT kMenuDesktopNext = 6;
constexpr UINT kMenuClear = 7;

struct KbState {
    Config* cfg = nullptr;
    int view = 0;   // 0 = base, 1 = nav
    int hot = -1;
    int selected = -1;
    bool capturing = false;
    KeyId pendingUp = kNoKey; // keyup of the captured key, still to swallow
    std::wstring status;
};

// Only one capture is ever armed (the settings dialog is modal), so the hook
// needs a single place to look.
enum class CaptureMode { None, SingleKey, KeyChord };
CaptureMode g_captureMode = CaptureMode::None;
HWND g_captureHwnd = nullptr;

// Chord capture: keys accumulate as they go down and the chord is finalised
// when the last one comes back up, so the trigger is whatever was pressed
// last and everything before it is a modifier.
KeyChord g_chord;
bool g_chordDown[kKeySlots] = {};
int g_chordHeld = 0;
bool g_chordGotKey = false;

KbState* State(HWND hwnd)
{
    return reinterpret_cast<KbState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
}

// The picker follows the layout of the config being edited, which is not the
// live one while the dialog is open.
// Roles change how the hook behaves; a plain Action::Key only changes which
// key arrives. Only a role can drive the Mac cmd+arrow chords or the layer.
bool BindIsRole(const Bind& b)
{
    switch (b.action) {
    case Action::Layer:
    case Action::WinKey:
    case Action::CtrlSwap:
    case Action::DesktopPrev:
    case Action::DesktopNext:
        return true;
    default:
        return false;
    }
}

const KeyCap* CurrentBoard(KbState* st, int& count)
{
    return BoardKeys(st && st->cfg ? ResolvedLayoutFor(*st->cfg) : ResolvedLayout(), count);
}

Bind* BindFor(KbState* st, KeyId id)
{
    if (!st->cfg || id == kNoKey)
        return nullptr;
    Bind* table = st->view == 0 ? st->cfg->base : st->cfg->nav;
    return &table[KeySlot(id)];
}

void Notify(HWND hwnd)
{
    HWND parent = GetParent(hwnd);
    if (parent)
        SendMessageW(parent, KBM_CHANGED, 0, reinterpret_cast<LPARAM>(hwnd));
}

void EndCapture(HWND hwnd, KbState* st)
{
    if (!st->capturing)
        return;
    st->capturing = false;
    g_captureMode = CaptureMode::None;
    st->pendingUp = kNoKey;
    g_captureHwnd = nullptr;
    KillTimer(hwnd, kCaptureTimer);
}

void BeginCapture(HWND hwnd, KbState* st)
{
    int count = 0;
    const KeyCap* board = CurrentBoard(st, count);
    if (st->selected < 0 || st->selected >= count)
        return;
    st->capturing = true;
    g_captureMode = CaptureMode::SingleKey;
    st->pendingUp = kNoKey;
    g_captureHwnd = hwnd;
    // Every keystroke is swallowed while armed, so it must not be possible to
    // get stuck here: focus loss, a click, Esc, or this timeout all release it.
    SetTimer(hwnd, kCaptureTimer, kCaptureTimeoutMs, nullptr);
    st->status = std::wstring(L"Press the key that ") + KeyName(board[st->selected].id) +
                 L" should send\x2026  (Esc cancels)";
    InvalidateRect(hwnd, nullptr, FALSE);
    Notify(hwnd);
}

void SetStatusForSelection(KbState* st)
{
    int count = 0;
    const KeyCap* board = CurrentBoard(st, count);
    if (st->selected < 0 || st->selected >= count) {
        int mapped = 0;
        const Bind* table = st->cfg ? (st->view == 0 ? st->cfg->base : st->cfg->nav) : nullptr;
        for (int i = 0; table && i < kKeySlots; ++i)
            mapped += table[i].action != Action::None ? 1 : 0;
        wchar_t buf[96];
        wsprintfW(buf, L"%d key%s mapped on the %s layer.", mapped, mapped == 1 ? L"" : L"s",
                  st->view == 0 ? L"base" : L"nav");
        st->status = buf;
        return;
    }
    const KeyCap& cap = board[st->selected];
    const Bind* bind = BindFor(st, cap.id);
    st->status = std::wstring(KeyName(cap.id)) + L": " +
                 (bind ? DescribeBind(*bind) : L"unbound");
    if (!bind)
        return;
    // Spell the roles out — which one you picked is the difference between the
    // Mac chords working and quietly doing nothing.
    switch (bind->action) {
    case Action::Layer:
        st->status += L" — hold it to reach the nav layer; a lone tap does nothing.";
        break;
    case Action::CtrlSwap:
        st->status += L" — sends Ctrl, and makes layer+arrows jump to line and "
                      L"document ends the way Mac cmd does.";
        break;
    case Action::WinKey:
        st->status += L" — the real Windows key (needs the kernel Scancode Map).";
        break;
    case Action::DesktopPrev:
    case Action::DesktopNext:
        st->status += L" — Ctrl+Win+Arrow.";
        break;
    default:
        break;
    }
}

void ApplyBind(HWND hwnd, KbState* st, const Bind& bind)
{
    int count = 0;
    const KeyCap* board = CurrentBoard(st, count);
    if (st->selected < 0 || st->selected >= count)
        return;
    Bind* slot = BindFor(st, board[st->selected].id);
    if (!slot)
        return;
    *slot = bind;
    SetStatusForSelection(st);
    InvalidateRect(hwnd, nullptr, FALSE);
    Notify(hwnd);
}

int HitTest(HWND hwnd, POINT pt, float& scale, int& originX, int& originY)
{
    KbState* st = State(hwnd);
    RECT rc;
    GetClientRect(hwnd, &rc);
    const int w = rc.right - rc.left;
    const int h = rc.bottom - rc.top;
    scale = min(static_cast<float>(w) / kBoardW, static_cast<float>(h) / kBoardH);
    originX = (w - static_cast<int>(kBoardW * scale)) / 2;
    originY = (h - static_cast<int>(kBoardH * scale)) / 2;

    int count = 0;
    const KeyCap* board = CurrentBoard(st, count);
    for (int i = 0; i < count; ++i) {
        const KeyCap& k = board[i];
        const int x = originX + static_cast<int>(k.x * scale);
        const int y = originY + static_cast<int>(k.y * scale);
        const int kw = static_cast<int>(k.w * scale);
        const int kh = static_cast<int>(k.h * scale);
        if (pt.x >= x && pt.x < x + kw && pt.y >= y && pt.y < y + kh)
            return i;
    }
    return -1;
}

void ShowRoleMenu(HWND hwnd, KbState* st, POINT screenPt)
{
    int count = 0;
    const KeyCap* board = CurrentBoard(st, count);
    if (st->selected < 0 || st->selected >= count)
        return;
    const KeyCap& cap = board[st->selected];
    if (cap.flags & KCF_UNBINDABLE)
        return;

    HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING, kMenuPressKey, L"Press a key\x2026");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    if (st->view == 0) {
        AppendMenuW(menu, MF_STRING, kMenuLayer, L"Hold for nav layer");
        AppendMenuW(menu, MF_STRING, kMenuWinKey, L"Windows key");
        AppendMenuW(menu, MF_STRING, kMenuCtrl, L"Ctrl (Mac cmd)");
    } else {
        AppendMenuW(menu, MF_STRING, kMenuDesktopPrev, L"Previous virtual desktop");
        AppendMenuW(menu, MF_STRING, kMenuDesktopNext, L"Next virtual desktop");
    }
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, kMenuClear, L"Clear");

    const int cmd = TrackPopupMenu(menu, TPM_RIGHTBUTTON | TPM_RETURNCMD | TPM_NONOTIFY,
                                   screenPt.x, screenPt.y, 0, hwnd, nullptr);
    DestroyMenu(menu);

    Bind bind;
    switch (cmd) {
    case kMenuPressKey:     BeginCapture(hwnd, st); return;
    case kMenuLayer:        bind.action = Action::Layer; break;
    case kMenuWinKey:       bind.action = Action::WinKey; break;
    case kMenuCtrl:         bind.action = Action::CtrlSwap; break;
    case kMenuDesktopPrev:  bind.action = Action::DesktopPrev; break;
    case kMenuDesktopNext:  bind.action = Action::DesktopNext; break;
    case kMenuClear:        break; // default-constructed Bind is Action::None
    default:                return;
    }
    ApplyBind(hwnd, st, bind);
}

// ------------------------------------------------------------------ drawing

COLORREF Blend(COLORREF a, COLORREF b, int pct)
{
    const int r = (GetRValue(a) * (100 - pct) + GetRValue(b) * pct) / 100;
    const int g = (GetGValue(a) * (100 - pct) + GetGValue(b) * pct) / 100;
    const int bl = (GetBValue(a) * (100 - pct) + GetBValue(b) * pct) / 100;
    return RGB(r, g, bl);
}

void PaintBoard(HWND hwnd, HDC target)
{
    KbState* st = State(hwnd);
    RECT rc;
    GetClientRect(hwnd, &rc);
    const int w = rc.right - rc.left;
    const int h = rc.bottom - rc.top;

    HDC dc = CreateCompatibleDC(target);
    HBITMAP bmp = CreateCompatibleBitmap(target, w, h);
    HGDIOBJ oldBmp = SelectObject(dc, bmp);

    const COLORREF face = GetSysColor(COLOR_BTNFACE);
    const COLORREF window = GetSysColor(COLOR_WINDOW);
    const COLORREF text = GetSysColor(COLOR_BTNTEXT);
    const COLORREF accent = GetSysColor(COLOR_HIGHLIGHT);

    HBRUSH bg = CreateSolidBrush(face);
    FillRect(dc, &rc, bg);
    DeleteObject(bg);

    const float scale = min(static_cast<float>(w) / kBoardW, static_cast<float>(h) / kBoardH);
    const int originX = (w - static_cast<int>(kBoardW * scale)) / 2;
    const int originY = (h - static_cast<int>(kBoardH * scale)) / 2;

    const int capFontH = max(9, static_cast<int>(34 * scale));
    const int bindFontH = max(8, static_cast<int>(26 * scale));
    HFONT capFont = CreateFontW(-capFontH, 0, 0, 0, FW_SEMIBOLD, 0, 0, 0, DEFAULT_CHARSET,
                                OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                VARIABLE_PITCH, L"Segoe UI");
    HFONT bindFont = CreateFontW(-bindFontH, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET,
                                 OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                 VARIABLE_PITCH, L"Segoe UI");
    HGDIOBJ oldFont = SelectObject(dc, capFont);
    SetBkMode(dc, TRANSPARENT);

    int count = 0;
    const KeyCap* board = CurrentBoard(st, count);
    for (int i = 0; i < count; ++i) {
        const KeyCap& k = board[i];
        RECT kr;
        kr.left = originX + static_cast<int>(k.x * scale);
        kr.top = originY + static_cast<int>(k.y * scale);
        kr.right = kr.left + static_cast<int>(k.w * scale) - max(1, static_cast<int>(6 * scale));
        kr.bottom = kr.top + static_cast<int>(k.h * scale) - max(1, static_cast<int>(6 * scale));

        const bool unbindable = (k.flags & KCF_UNBINDABLE) != 0;
        const Bind* bind = unbindable ? nullptr : BindFor(st, k.id);
        const bool bound = bind && bind->action != Action::None;
        const bool selected = i == st->selected;
        const bool hot = i == st->hot;

        COLORREF fill = window;
        if (unbindable)      fill = Blend(face, window, 40);
        else if (selected && st->capturing) fill = accent;
        else if (selected)   fill = Blend(window, accent, 35);
        else if (bound)      fill = Blend(window, accent, 18);
        if (hot && !selected)
            fill = Blend(fill, RGB(255, 255, 255), 25);

        HBRUSH brush = CreateSolidBrush(fill);
        HPEN pen = CreatePen(PS_SOLID, max(1, static_cast<int>(scale * (selected ? 4 : 2))),
                             bound || selected ? accent : Blend(face, text, 30));
        HGDIOBJ oldBrush = SelectObject(dc, brush);
        HGDIOBJ oldPen = SelectObject(dc, pen);
        const int radius = max(2, static_cast<int>(14 * scale));
        RoundRect(dc, kr.left, kr.top, kr.right, kr.bottom, radius, radius);
        SelectObject(dc, oldBrush);
        SelectObject(dc, oldPen);
        DeleteObject(brush);
        DeleteObject(pen);

        SetTextColor(dc, unbindable ? Blend(face, text, 45)
                                    : (selected && st->capturing ? window : text));
        SelectObject(dc, capFont);
        RECT tr = kr;
        tr.top += max(1, static_cast<int>(6 * scale));
        DrawTextW(dc, k.label, -1, &tr,
                  bound ? (DT_CENTER | DT_TOP | DT_SINGLELINE) : (DT_CENTER | DT_VCENTER | DT_SINGLELINE));

        if (bound) {
            SelectObject(dc, bindFont);
            SetTextColor(dc, selected && st->capturing ? window : Blend(text, accent, 60));
            RECT br = kr;
            br.top = kr.top + (kr.bottom - kr.top) / 2;
            br.bottom -= max(1, static_cast<int>(4 * scale));
            // A role and a plain key remap read identically otherwise —
            // "Right Ctrl" and "Ctrl (Mac cmd)" look like the same kind of
            // thing but behave nothing alike, so roles get a marker.
            std::wstring label = BindIsRole(*bind) ? L"\x25C6 " + DescribeBind(*bind)
                                                   : DescribeBind(*bind);
            DrawTextW(dc, label.c_str(), -1, &br,
                      DT_CENTER | DT_BOTTOM | DT_SINGLELINE | DT_END_ELLIPSIS);
        }
    }

    SelectObject(dc, oldFont);
    DeleteObject(capFont);
    DeleteObject(bindFont);

    BitBlt(target, 0, 0, w, h, dc, 0, 0, SRCCOPY);
    SelectObject(dc, oldBmp);
    DeleteObject(bmp);
    DeleteDC(dc);
}

LRESULT CALLBACK KeyboardWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    KbState* st = State(hwnd);

    switch (msg) {
    case WM_CREATE: {
        KbState* fresh = new KbState();
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(fresh));
        SetStatusForSelection(fresh);
        return 0;
    }
    case WM_DESTROY:
        if (st) {
            EndCapture(hwnd, st);
            delete st;
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
        }
        return 0;

    case KBM_SETCONFIG:
        if (st) {
            st->cfg = reinterpret_cast<Config*>(wParam);
            st->selected = -1;
            SetStatusForSelection(st);
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;

    case KBM_SETVIEW:
        if (st) {
            EndCapture(hwnd, st);
            st->view = wParam ? 1 : 0;
            st->selected = -1;
            SetStatusForSelection(st);
            InvalidateRect(hwnd, nullptr, FALSE);
            Notify(hwnd);
        }
        return 0;

    case WM_ERASEBKGND:
        return 1;

    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(hwnd, &ps);
        PaintBoard(hwnd, dc);
        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_SIZE:
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;

    case WM_MOUSEMOVE: {
        POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        float scale; int ox, oy;
        const int hit = HitTest(hwnd, pt, scale, ox, oy);
        if (st && hit != st->hot) {
            st->hot = hit;
            InvalidateRect(hwnd, nullptr, FALSE);
            TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, hwnd, 0 };
            TrackMouseEvent(&tme);
        }
        return 0;
    }

    case WM_MOUSELEAVE:
        if (st && st->hot != -1) {
            st->hot = -1;
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;

    case WM_LBUTTONDOWN:
    case WM_RBUTTONDOWN: {
        if (!st)
            return 0;
        SetFocus(hwnd);
        // A click always releases an armed capture first, so a stuck picker is
        // never more than one click from recovery.
        const bool wasCapturing = st->capturing;
        EndCapture(hwnd, st);

        POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        float scale; int ox, oy;
        const int hit = HitTest(hwnd, pt, scale, ox, oy);
        st->selected = hit;
        SetStatusForSelection(st);

        int count = 0;
        const KeyCap* board = CurrentBoard(st, count);
        if (hit >= 0 && (board[hit].flags & KCF_UNBINDABLE)) {
            st->status = std::wstring(board[hit].label) +
                         L" never reaches Windows, so it cannot be remapped here.";
            InvalidateRect(hwnd, nullptr, FALSE);
            Notify(hwnd);
            return 0;
        }
        if (hit < 0) {
            InvalidateRect(hwnd, nullptr, FALSE);
            Notify(hwnd);
            return 0;
        }
        if (msg == WM_RBUTTONDOWN) {
            POINT screen = pt;
            ClientToScreen(hwnd, &screen);
            InvalidateRect(hwnd, nullptr, FALSE);
            ShowRoleMenu(hwnd, st, screen);
        } else if (!wasCapturing) {
            BeginCapture(hwnd, st);
        } else {
            InvalidateRect(hwnd, nullptr, FALSE);
            Notify(hwnd);
        }
        return 0;
    }

    case WM_TIMER:
        if (st && wParam == kCaptureTimer) {
            EndCapture(hwnd, st);
            SetStatusForSelection(st);
            InvalidateRect(hwnd, nullptr, FALSE);
            Notify(hwnd);
        }
        return 0;

    case WM_KILLFOCUS:
        if (st) {
            EndCapture(hwnd, st);
            SetStatusForSelection(st);
            InvalidateRect(hwnd, nullptr, FALSE);
            Notify(hwnd);
        }
        return 0;

    case WM_GETDLGCODE:
        return DLGC_WANTARROWS | DLGC_WANTCHARS;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

} // namespace

// ------------------------------------------------------------ public surface

const KeyCap* BoardKeys(BoardLayout layout, int& count)
{
    BuildFilteredBoards();
    const std::vector<KeyCap>& board = layout == BoardLayout::Iso ? g_iso : g_ansi;
    count = static_cast<int>(board.size());
    return board.data();
}

const wchar_t* KeyName(KeyId id)
{
    for (const NameOverride& n : kNames)
        if (n.id == id)
            return n.name;
    for (const KeyCap& k : kBoard)
        if (k.id == id && k.id != kNoKey)
            return k.label;

    // Not on this board and not a key we have a word for — show the scancode
    // rather than inventing a name. Rotating buffers so two names can be built
    // for one sentence ("Key 5A -> Key 6B") without the second clobbering the
    // first.
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

std::wstring DescribeBind(const Bind& bind)
{
    switch (bind.action) {
    case Action::Key:         return KeyName(bind.target);
    case Action::Layer:       return L"nav layer";
    case Action::WinKey:      return L"Windows key";
    case Action::CtrlSwap:    return L"Ctrl (Mac cmd)";
    case Action::DesktopPrev: return L"prev desktop";
    case Action::DesktopNext: return L"next desktop";
    case Action::None:        break;
    }
    return L"unbound";
}

std::string DescribeBindAscii(KeyId id, const Bind& bind)
{
    return ToAscii(KeyName(id)) + " -> " + ToAscii(DescribeBind(bind));
}

void RegisterKeyboardControl(HINSTANCE instance)
{
    WNDCLASSW wc = {};
    wc.lpfnWndProc = KeyboardWndProc;
    wc.hInstance = instance;
    wc.lpszClassName = kKeyboardClass;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    RegisterClassW(&wc);
}

std::wstring KeyboardStatusText(HWND control)
{
    KbState* st = State(control);
    return st ? st->status : std::wstring();
}

std::wstring DescribeChord(const KeyChord& chord)
{
    if (!ChordValid(chord))
        return L"(none)";
    std::wstring out;
    for (uint8_t i = 0; i < chord.modCount; ++i)
        out += std::wstring(KeyName(chord.mods[i])) + L" + ";
    return out + KeyName(chord.trigger);
}

std::wstring DescribeChordAction(const ChordBinding& b)
{
    switch (b.action) {
    case ChordAction::StartMenu:     return L"Start menu";
    case ChordAction::InputLanguage: return L"Switch input language";
    case ChordAction::SendChord:     return L"sends " + DescribeChord(b.to);
    case ChordAction::None:          break;
    }
    return L"nothing";
}

std::wstring DescribeChordBinding(const ChordBinding& b)
{
    return DescribeChord(b.from) + L"   \x2192   " + DescribeChordAction(b);
}

std::string DescribeChordBindingAscii(const ChordBinding& b)
{
    return ToAscii(DescribeChord(b.from)) + " -> " + ToAscii(DescribeChordAction(b));
}

bool BeginChordCapture(HWND notify)
{
    if (g_captureHwnd)
        return false;
    g_chord = KeyChord();
    for (bool& down : g_chordDown)
        down = false;
    g_chordHeld = 0;
    g_chordGotKey = false;
    g_captureMode = CaptureMode::KeyChord;
    g_captureHwnd = notify;
    SetTimer(notify, kChordCaptureTimer, kCaptureTimeoutMs, nullptr);
    return true;
}

void CancelChordCapture()
{
    if (g_captureMode != CaptureMode::KeyChord)
        return;
    KillTimer(g_captureHwnd, kChordCaptureTimer);
    g_captureMode = CaptureMode::None;
    g_captureHwnd = nullptr;
}

KeyChord CapturedChord()
{
    return g_chord;
}

bool CaptureActive()
{
    return g_captureHwnd != nullptr;
}

// Accumulates keys while they go down and finalises on the last release, so
// the trigger is whatever was pressed last. Everything is swallowed meanwhile.
static bool ForwardChordKey(KeyId id, bool down)
{
    HWND notify = g_captureHwnd;
    const int slot = KeySlot(id);

    if (down) {
        if (g_chordDown[slot])
            return true; // autorepeat
        if (!g_chordGotKey && id == 0x01) {
            CancelChordCapture();
            PostMessageW(notify, CHM_CANCELLED, 0, 0);
            return true;
        }
        g_chordDown[slot] = true;
        ++g_chordHeld;
        g_chordGotKey = true;
        // The key pressed before this one becomes a modifier.
        if (ChordValid(g_chord) && g_chord.modCount < kMaxChordMods)
            g_chord.mods[g_chord.modCount++] = g_chord.trigger;
        g_chord.trigger = id;
        return true;
    }

    if (!g_chordDown[slot])
        return false; // held before we armed; not ours to eat
    g_chordDown[slot] = false;
    if (--g_chordHeld > 0)
        return true;

    KillTimer(notify, kChordCaptureTimer);
    g_captureMode = CaptureMode::None;
    g_captureHwnd = nullptr;
    PostMessageW(notify, CHM_CAPTURED, 0, 0);
    return true;
}

bool ForwardCaptureKey(KeyId id, bool down)
{
    HWND hwnd = g_captureHwnd;
    if (!hwnd)
        return false;
    if (g_captureMode == CaptureMode::KeyChord)
        return ForwardChordKey(id, down);

    KbState* st = State(hwnd);
    if (!st || !st->capturing) {
        g_captureHwnd = nullptr;
        return false;
    }

    if (!down) {
        // Swallow the release of the key we just captured; anything else was
        // already down before we armed, so let it through.
        if (id == st->pendingUp) {
            st->pendingUp = kNoKey;
            return true;
        }
        return false;
    }

    if (id == 0x01) { // Esc cancels
        EndCapture(hwnd, st);
        SetStatusForSelection(st);
        InvalidateRect(hwnd, nullptr, FALSE);
        Notify(hwnd);
        return true;
    }

    int count = 0;
    const KeyCap* board = CurrentBoard(st, count);
    Bind bind;
    if (st->selected >= 0 && st->selected < count && board[st->selected].id == id) {
        // Pointing a key at itself is how you say "leave this one alone".
        bind = Bind();
    } else {
        bind.action = Action::Key;
        bind.target = id;
    }

    st->pendingUp = id;
    st->capturing = false;
    g_captureMode = CaptureMode::None;
    g_captureHwnd = nullptr;
    KillTimer(hwnd, kCaptureTimer);
    ApplyBind(hwnd, st, bind);
    return true;
}
