// Monopane — a searchable Cmd+Tab window switcher for Windows, with a
// launchpad of pinned apps on a second hotkey (see launchpad.h).
//
// Press left Cmd+Tab (on a Mac keyboard remapped by MacKeys) to open the
// overlay, type a few letters to fuzzy-filter the open windows, press Enter
// to activate the selected one. Esc dismisses.
//
// The left Cmd key arrives one of two ways, both handled here:
//   - VK_F23: the kernel Scancode Map remap, when MacKeys is paused or absent
//   - an injected Left Ctrl carrying MacKeys' dwExtraInfo marker, when MacKeys
//     is running (it swallows F23 and holds Ctrl for Mac-style copy/paste)
// Physical Ctrl+Tab is deliberately NOT intercepted, so in-app tab switching
// keeps working.

#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <dwmapi.h>
#include <shellapi.h>

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

#include "aliases.h"
#include "display.h"
#include "fuzzy.h"
#include "launchpad.h"
#include "paint.h"
#include "settings.h"
#include "theme.h"
#include "window_list.h"
#include "../res/resource.h"

namespace {

constexpr wchar_t kOverlayClass[] = L"MonopaneOverlay";
constexpr wchar_t kMutexName[] = L"MonopaneSingleInstance";

// dwExtraInfo marker MacKeys stamps on the input it injects ("MKYS").
constexpr ULONG_PTR kMacKeysMarker = 0x4D4B5953;

constexpr UINT WM_APP_HOTKEY = WM_APP + 1;  // wParam: 1 = shift held (cycle up)
constexpr UINT WM_APP_TRAY = WM_APP + 2;
constexpr UINT WM_APP_ROTATE = WM_APP + 3;
constexpr UINT WM_APP_LAUNCHPAD = WM_APP + 4;

constexpr UINT IDC_SEARCH_EDIT = 100;
constexpr UINT IDM_EXIT = 201;
constexpr UINT IDM_SETTINGS = 202;
constexpr UINT IDM_LAUNCHPAD = 203;
constexpr UINT TRAY_ICON_ID = 1;

// As many rows as will be shown at once, before the list scrolls. A short
// screen may fit fewer; g_visibleRows is what the layout settled on.
constexpr int MAX_VISIBLE_ROWS = 10;
int g_visibleRows = MAX_VISIBLE_ROWS;

// Base (96-dpi) metrics. The width, the search box and the padding are shared
// with the launchpad; see theme.h.
constexpr int BASE_ROW_H = 44;
constexpr int BASE_ICON = 24;


HINSTANCE g_hInstance = nullptr;
HWND g_hwndOverlay = nullptr;
HWND g_hwndEdit = nullptr;
HHOOK g_keyboardHook = nullptr;
WNDPROC g_editBaseProc = nullptr;

std::vector<WindowInfo> g_windows;
std::vector<int> g_filtered;   // indices into g_windows, best match first
int g_selected = 0;
int g_scrollTop = 0;
bool g_overlayVisible = false;
DWORD g_shownTick = 0;         // when the overlay was last shown

UINT g_dpi = 96;
HFONT g_fontApp = nullptr;
HFONT g_fontTitle = nullptr;
HFONT g_fontEdit = nullptr;
HBRUSH g_brushSearchBg = nullptr;
int g_editLineH = 0;   // one line of g_fontEdit, for centring the search box
NOTIFYICONDATAW g_trayIcon{};

// Explorer broadcasts this when the taskbar appears — at logon, and again if
// it is ever restarted. Either way the tray icon has to be added afresh.
UINT g_taskbarCreatedMsg = 0;

// Where the cursor was when a mouse move was last acted on, in screen
// coordinates.
//
// Windows sends WM_MOUSEMOVE when a window moves or resizes under a cursor
// that has not moved at all, and this overlay resizes on every keystroke as
// the list filters. Taken at face value that silently drags the selection off
// the best match and onto whatever row happened to slide under the pointer, so
// a hover only counts when the pointer has actually been somewhere else.
POINT g_lastMouseScreen{};

int Scale(int value)
{
    return MulDiv(value, g_dpi, 96);
}

bool MouseActuallyMoved(HWND hwnd, POINT client)
{
    POINT screen = client;
    ClientToScreen(hwnd, &screen);
    if (screen.x == g_lastMouseScreen.x && screen.y == g_lastMouseScreen.y)
        return false;
    g_lastMouseScreen = screen;
    return true;
}

void CreateFonts()
{
    if (g_fontApp) DeleteObject(g_fontApp);
    if (g_fontTitle) DeleteObject(g_fontTitle);
    if (g_fontEdit) DeleteObject(g_fontEdit);

    LOGFONTW lf{};
    lf.lfQuality = CLEARTYPE_QUALITY;
    wcscpy_s(lf.lfFaceName, L"Segoe UI");

    lf.lfHeight = -Scale(15);
    lf.lfWeight = FW_SEMIBOLD;
    g_fontApp = CreateFontIndirectW(&lf);

    lf.lfWeight = FW_NORMAL;
    g_fontTitle = CreateFontIndirectW(&lf);

    lf.lfHeight = -Scale(17);
    g_fontEdit = CreateFontIndirectW(&lf);

    // A single-line edit control draws its text at the top of its client area,
    // not down the middle of it, so the only way to have the text sit centred
    // in the search plate is to make the control exactly one line tall and
    // centre that. Which means knowing how tall a line is.
    if (HDC dc = GetDC(g_hwndOverlay)) {
        HGDIOBJ old = SelectObject(dc, g_fontEdit);
        TEXTMETRICW tm{};
        GetTextMetricsW(dc, &tm);
        g_editLineH = tm.tmHeight;
        SelectObject(dc, old);
        ReleaseDC(g_hwndOverlay, dc);
    }

    if (g_hwndEdit)
        SendMessageW(g_hwndEdit, WM_SETFONT, reinterpret_cast<WPARAM>(g_fontEdit), TRUE);
}

// The plate the search box sits on.
RECT SearchPlateRect()
{
    const int pad = Scale(BASE_PAD);
    return RECT{ pad, pad, Scale(BASE_PANEL_WIDTH) - pad, Scale(BASE_SEARCH_H) - Scale(4) };
}

// ---------------------------------------------------------------------------
// Filtering

std::wstring GetSearchText()
{
    wchar_t buffer[256];
    GetWindowTextW(g_hwndEdit, buffer, 256);
    return buffer;
}

void ApplyFilter()
{
    const std::wstring query = GetSearchText();
    g_filtered.clear();

    if (query.empty()) {
        for (int i = 0; i < static_cast<int>(g_windows.size()); ++i)
            g_filtered.push_back(i);
    } else {
        // A learned alias ranks its app's windows first — and keeps them in
        // the list even when the fuzzy match alone would not include them.
        const std::wstring aliasTarget = LookupAlias(query);
        constexpr int kAliasBoost = 1 << 20;

        std::vector<std::pair<int, int>> scored;  // (score, index)
        for (int i = 0; i < static_cast<int>(g_windows.size()); ++i) {
            const std::wstring haystack = g_settings.matchAppName
                ? g_windows[i].appName + L" " + g_windows[i].title
                : g_windows[i].title;
            int score = FuzzyScore(query, haystack);
            if (!aliasTarget.empty() && g_windows[i].exePath == aliasTarget)
                score = (score < 0 ? 0 : score) + kAliasBoost;
            if (score >= 0)
                scored.emplace_back(score, i);
        }
        // Stable sort keeps z-order (most recently used first) for equal scores.
        std::stable_sort(scored.begin(), scored.end(),
                         [](const auto& a, const auto& b) { return a.first > b.first; });
        for (const auto& [score, index] : scored)
            g_filtered.push_back(index);
    }

    g_selected = 0;
    g_scrollTop = 0;
}

void EnsureSelectionVisible()
{
    if (g_selected < g_scrollTop)
        g_scrollTop = g_selected;
    else if (g_selected >= g_scrollTop + g_visibleRows)
        g_scrollTop = g_selected - g_visibleRows + 1;
}

// ---------------------------------------------------------------------------
// Overlay layout & visibility

void LayoutOverlay(bool reposition)
{
    const int width = Scale(BASE_PANEL_WIDTH);
    const int searchH = Scale(BASE_SEARCH_H);
    const int rowH = Scale(BASE_ROW_H);
    const int pad = Scale(BASE_PAD);

    // On the monitor the cursor is on when opening; on the one it is already
    // on when the list is merely re-filtering.
    HMONITOR monitor;
    if (reposition) {
        POINT cursor;
        GetCursorPos(&cursor);
        monitor = MonitorFromPoint(cursor, MONITOR_DEFAULTTOPRIMARY);
    } else {
        monitor = MonitorFromWindow(g_hwndOverlay, MONITOR_DEFAULTTOPRIMARY);
    }
    MONITORINFO mi{ sizeof(mi) };
    GetMonitorInfoW(monitor, &mi);

    // The search box sits on the middle of the work area, the same line the
    // launchpad puts its own on, and the list hangs below it. The top is fixed
    // whatever the list is doing, so nothing creeps up the screen as a search
    // narrows it, and nothing jumps when the launchpad opens instead.
    const int top = PanelTop(mi.rcWork, SearchPlateRect());
    g_visibleRows = std::min(RowsThatFit(mi.rcWork, top, searchH, pad, rowH), MAX_VISIBLE_ROWS);

    int rows = std::min(static_cast<int>(g_filtered.size()), g_visibleRows);
    if (rows == 0)
        rows = 1;  // room for the "no matching windows" row
    const int height = searchH + rows * rowH + pad;

    const int x = mi.rcWork.left + (mi.rcWork.right - mi.rcWork.left - width) / 2;
    SetWindowPos(g_hwndOverlay, HWND_TOPMOST, x, top, width, height, SWP_NOACTIVATE);

    // One line tall, centred in the plate: see CreateFonts.
    const RECT plate = SearchPlateRect();
    const int editH = g_editLineH + Scale(2);
    MoveWindow(g_hwndEdit, plate.left + Scale(10),
               plate.top + ((plate.bottom - plate.top) - editH) / 2,
               (plate.right - Scale(10)) - (plate.left + Scale(10)), editH, TRUE);
    InvalidateRect(g_hwndOverlay, nullptr, TRUE);
}

void HideOverlay()
{
    if (!g_overlayVisible)
        return;
    g_overlayVisible = false;
    ShowWindow(g_hwndOverlay, SW_HIDE);
}

void ShowOverlay()
{
    SetIconSizePx(Scale(BASE_ICON));
    g_windows = EnumerateAltTabWindows();
    SetWindowTextW(g_hwndEdit, L"");
    ApplyFilter();
    // Like native Alt+Tab: preselect the previous window, not the current one.
    if (g_settings.preselectPrevious && g_filtered.size() > 1)
        g_selected = 1;

    LayoutOverlay(true);
    // Opening under a cursor that is already sitting there is not a hover.
    GetCursorPos(&g_lastMouseScreen);
    g_overlayVisible = true;
    g_shownTick = GetTickCount();
    ShowWindow(g_hwndOverlay, SW_SHOW);
    ForceForeground(g_hwndOverlay);
    SetFocus(g_hwndEdit);
}

void MoveSelection(int delta)
{
    if (g_filtered.empty())
        return;
    const int count = static_cast<int>(g_filtered.size());
    g_selected = ((g_selected + delta) % count + count) % count;
    EnsureSelectionVisible();
    InvalidateRect(g_hwndOverlay, nullptr, TRUE);
}

void ActivateSelection()
{
    if (g_filtered.empty()) {
        HideOverlay();
        return;
    }
    const WindowInfo& selected = g_windows[g_filtered[g_selected]];
    const HWND target = selected.hwnd;

    // Learn the query -> app association for next time.
    SaveAlias(GetSearchText(), selected.exePath);

    // Optionally bring the app's other windows forward too, bottom-most first
    // so their relative z-order is preserved and the selected one ends on top.
    std::vector<HWND> siblings;
    if (g_settings.activateAllOfApp && !selected.exePath.empty()) {
        for (auto it = g_windows.rbegin(); it != g_windows.rend(); ++it) {
            if (it->hwnd != target && it->exePath == selected.exePath)
                siblings.push_back(it->hwnd);
        }
    }

    HideOverlay();
    for (HWND sibling : siblings) {
        if (IsIconic(sibling))
            ShowWindow(sibling, SW_RESTORE);
        SetWindowPos(sibling, HWND_TOP, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    }
    ActivateWindow(target);
}

// ---------------------------------------------------------------------------
// Painting

void PaintOverlay(HDC hdc, const RECT& client)
{
    const int width = client.right;
    const int height = client.bottom;
    const int searchH = Scale(BASE_SEARCH_H);
    const int rowH = Scale(BASE_ROW_H);
    const int pad = Scale(BASE_PAD);
    const int iconPx = Scale(BASE_ICON);

    HDC mem = CreateCompatibleDC(hdc);
    HBITMAP bitmap = CreateCompatibleBitmap(hdc, width, height);
    HGDIOBJ oldBitmap = SelectObject(mem, bitmap);

    // Background
    HBRUSH bgBrush = CreateSolidBrush(CLR_BG);
    RECT full{ 0, 0, width, height };
    FillRect(mem, &full, bgBrush);
    DeleteObject(bgBrush);

    // Search box background (the edit control paints itself on top), rounded
    // to the same radius the compositor rounds the window itself by.
    FillRoundRectAA(mem, SearchPlateRect(), Scale(BASE_CORNER), CLR_SEARCH_BG, CLR_BG);

    SetBkMode(mem, TRANSPARENT);

    if (g_filtered.empty()) {
        RECT row{ pad, searchH, width - pad, searchH + rowH };
        SelectObject(mem, g_fontTitle);
        SetTextColor(mem, CLR_TEXT_DIM);
        DrawTextW(mem, L"No matching windows", -1, &row,
                  DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }

    const int last = std::min(static_cast<int>(g_filtered.size()), g_scrollTop + g_visibleRows);
    for (int i = g_scrollTop; i < last; ++i) {
        const WindowInfo& win = g_windows[g_filtered[i]];
        const int y = searchH + (i - g_scrollTop) * rowH;

        if (i == g_selected) {
            const RECT sel{ pad, y + Scale(2), width - pad, y + rowH - Scale(2) };
            FillRoundRectAA(mem, sel, Scale(6), CLR_SELECTION, CLR_BG);
        }

        int x = pad * 2;
        if (win.icon) {
            DrawIconEx(mem, x, y + (rowH - iconPx) / 2, win.icon, iconPx, iconPx, 0, nullptr, DI_NORMAL);
        }
        x += iconPx + pad;

        const int textRight = width - pad * 2;
        RECT textRect{ x, y, textRight, y + rowH };

        SelectObject(mem, g_fontApp);
        SetTextColor(mem, CLR_TEXT);
        std::wstring appName = win.appName.empty() ? L"?" : win.appName;
        DrawTextW(mem, appName.c_str(), -1, &textRect,
                  DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);

        SIZE appExtent{};
        GetTextExtentPoint32W(mem, appName.c_str(), static_cast<int>(appName.size()), &appExtent);
        textRect.left = std::min(static_cast<LONG>(x + appExtent.cx + Scale(8)), textRect.right);

        if (textRect.left < textRect.right) {
            SelectObject(mem, g_fontTitle);
            SetTextColor(mem, i == g_selected ? RGB(220, 230, 244) : CLR_TEXT_DIM);
            const std::wstring title = L"— " + win.title;
            DrawTextW(mem, title.c_str(), -1, &textRect,
                      DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
        }
    }

    BitBlt(hdc, 0, 0, width, height, mem, 0, 0, SRCCOPY);
    SelectObject(mem, oldBitmap);
    DeleteObject(bitmap);
    DeleteDC(mem);
}

int RowFromPoint(int yPos)
{
    const int searchH = Scale(BASE_SEARCH_H);
    const int rowH = Scale(BASE_ROW_H);
    if (yPos < searchH)
        return -1;
    const int row = g_scrollTop + (yPos - searchH) / rowH;
    if (row >= static_cast<int>(g_filtered.size()) || row >= g_scrollTop + g_visibleRows)
        return -1;
    return row;
}

// ---------------------------------------------------------------------------
// Keyboard handling

// Handles navigation keys for the overlay. Returns true if the key was consumed.
bool HandleNavigationKey(WPARAM vk)
{
    switch (vk) {
    case VK_DOWN:
        MoveSelection(1);
        return true;
    case VK_UP:
        MoveSelection(-1);
        return true;
    case VK_TAB:
        MoveSelection((GetKeyState(VK_SHIFT) & 0x8000) ? -1 : 1);
        return true;
    case VK_NEXT:
        MoveSelection(g_visibleRows);
        return true;
    case VK_PRIOR:
        MoveSelection(-g_visibleRows);
        return true;
    case VK_RETURN:
        ActivateSelection();
        return true;
    case VK_ESCAPE:
        HideOverlay();
        return true;
    default:
        return false;
    }
}

LRESULT CALLBACK EditSubclassProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg) {
    case WM_KEYDOWN:
        if (HandleNavigationKey(wParam))
            return 0;
        break;
    case WM_CHAR:
        // Swallow the chars for keys handled above to avoid the edit beep.
        if (wParam == L'\r' || wParam == L'\t' || wParam == 27)
            return 0;
        break;
    }
    return CallWindowProcW(g_editBaseProc, hwnd, msg, wParam, lParam);
}

// Which keys are down and how each one arrived, indexed by KeySlot(). Tracked
// here rather than read back from GetAsyncKeyState because that reports
// *virtual* keys: it merges left and right Ctrl, and shows the Ctrl MacKeys
// injects as though a real Ctrl key were pressed. Those are exactly the two
// distinctions the hotkey depends on.
keychord::KeyOrigin g_held[keychord::kKeySlots];
int g_heldCount = 0;

keychord::KeyId SlotToKeyId(int slot)
{
    return static_cast<keychord::KeyId>((slot & 0x100) ? (0xE000 | (slot & 0xFF))
                                                       : (slot & 0xFF));
}

// A keyup is missed if the hook times out while the thread is busy, which
// would leave a modifier stuck down and the hotkey permanently armed. Before
// matching, drop anything the OS agrees is no longer held.
void PruneStaleHeld()
{
    for (int slot = 0; slot < keychord::kKeySlots; ++slot) {
        if (g_held[slot] == keychord::KeyOrigin::Any)
            continue;
        const UINT vk = MapVirtualKeyW(SlotToKeyId(slot), MAPVK_VSC_TO_VK_EX);
        if (vk == 0)
            continue; // nothing to check it against; leave it alone
        if (!(GetAsyncKeyState(static_cast<int>(vk)) & 0x8000)) {
            g_held[slot] = keychord::KeyOrigin::Any;
            if (g_heldCount > 0)
                --g_heldCount;
        }
    }
}

LRESULT CALLBACK LowLevelKeyboardProc(int code, WPARAM wParam, LPARAM lParam)
{
    if (code == HC_ACTION) {
        const auto* kb = reinterpret_cast<const KBDLLHOOKSTRUCT*>(lParam);
        const bool down = wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN;
        const bool injected = (kb->flags & LLKHF_INJECTED) != 0;
        const bool fromMacKeys = injected && kb->dwExtraInfo == kMacKeysMarker;

        // Input injected by anything else (remote desktop, AutoHotkey) is left
        // alone entirely, as before.
        if (!injected || fromMacKeys) {
            const keychord::KeyId id =
                keychord::MakeKeyId(kb->scanCode, (kb->flags & LLKHF_EXTENDED) != 0);
            const keychord::KeyOrigin origin = fromMacKeys ? keychord::KeyOrigin::Injected
                                                           : keychord::KeyOrigin::Physical;

            // While Settings is capturing a hotkey it takes raw keystrokes
            // ahead of everything, and swallows them.
            if (keychord::ChordCaptureActive() && keychord::FeedChordKey(id, down, origin))
                return 1;

            const int slot = keychord::KeySlot(id);
            if (down) {
                if (g_held[slot] == keychord::KeyOrigin::Any)
                    ++g_heldCount;
                g_held[slot] = origin;
            } else if (g_held[slot] != keychord::KeyOrigin::Any) {
                g_held[slot] = keychord::KeyOrigin::Any;
                if (g_heldCount > 0)
                    --g_heldCount;
            }

            const keychord::KeyChord& hotkey = g_settings.hotkey;
            if (down && id == hotkey.trigger &&
                (hotkey.triggerOrigin == keychord::KeyOrigin::Any ||
                 hotkey.triggerOrigin == origin)) {
                PruneStaleHeld();
                // Loose match: Shift is held for reverse cycling, and the
                // switcher still has to open.
                if (keychord::ChordMatches(hotkey, g_held, g_heldCount, /*exact=*/false)) {
                    const WPARAM shift = (GetAsyncKeyState(VK_SHIFT) & 0x8000) ? 1 : 0;
                    PostMessageW(g_hwndOverlay, WM_APP_HOTKEY, shift, 0);
                    return 1; // the trigger belongs to the switcher
                }
            }

            // Rotating the display takes the better part of a second, and a
            // low-level hook that blocks for that long gets torn out by
            // Windows — so the work happens on the message loop.
            const keychord::KeyChord& rotate = g_settings.rotateHotkey;
            if (down && keychord::ChordValid(rotate) && id == rotate.trigger &&
                (rotate.triggerOrigin == keychord::KeyOrigin::Any ||
                 rotate.triggerOrigin == origin)) {
                PruneStaleHeld();
                if (keychord::ChordMatches(rotate, g_held, g_heldCount, /*exact=*/false)) {
                    PostMessageW(g_hwndOverlay, WM_APP_ROTATE, 0, 0);
                    return 1;
                }
            }

            // Loose like the others: under MacKeys the physical key behind
            // the chord's modifier may still be seen down alongside the one
            // MacKeys injects for it, and an exact match would refuse that.
            const keychord::KeyChord& launchpad = g_settings.launchpadHotkey;
            if (down && keychord::ChordValid(launchpad) && id == launchpad.trigger &&
                (launchpad.triggerOrigin == keychord::KeyOrigin::Any ||
                 launchpad.triggerOrigin == origin)) {
                PruneStaleHeld();
                if (keychord::ChordMatches(launchpad, g_held, g_heldCount, /*exact=*/false)) {
                    PostMessageW(g_hwndOverlay, WM_APP_LAUNCHPAD, 0, 0);
                    return 1;
                }
            }
        }
    }
    return CallNextHookEx(g_keyboardHook, code, wParam, lParam);
}

// ---------------------------------------------------------------------------
// Tray icon

// A logon task fires within seconds, which is often *before* Explorer is up,
// and an app that does its setup then comes up half-working: Shell_NotifyIcon
// has no taskbar to add an icon to, and the keyboard hook is installed against
// a desktop that is not yet the one receiving input — so the tray icon is
// missing and the hotkey does nothing. Waiting for the shell window to exist
// costs nothing on a normal launch (it is already there) and puts the logon
// case on the same footing.
void WaitForShell(DWORD timeoutMs)
{
    const DWORD deadline = GetTickCount() + timeoutMs;
    while (GetShellWindow() == nullptr && GetTickCount() < deadline)
        Sleep(250);
}

void AddTrayIcon(HWND hwnd)
{
    g_trayIcon.cbSize = sizeof(g_trayIcon);
    g_trayIcon.hWnd = hwnd;
    g_trayIcon.uID = TRAY_ICON_ID;
    g_trayIcon.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    g_trayIcon.uCallbackMessage = WM_APP_TRAY;
    g_trayIcon.hIcon = static_cast<HICON>(LoadImageW(
        g_hInstance, MAKEINTRESOURCEW(IDI_APP), IMAGE_ICON,
        GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), 0));
    wcscpy_s(g_trayIcon.szTip, L"Monopane — Cmd+Tab window search");
    Shell_NotifyIconW(NIM_ADD, &g_trayIcon);
}

void ShowTrayMenu(HWND hwnd)
{
    HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING, IDM_LAUNCHPAD, L"Launchpad");
    AppendMenuW(menu, MF_STRING, IDM_SETTINGS, L"Settings…");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, IDM_EXIT, L"Exit");

    POINT pt;
    GetCursorPos(&pt);
    SetForegroundWindow(hwnd);
    TrackPopupMenu(menu, TPM_RIGHTBUTTON, pt.x, pt.y, 0, hwnd, nullptr);
    PostMessageW(hwnd, WM_NULL, 0, 0);
    DestroyMenu(menu);
}

// ---------------------------------------------------------------------------
// Window procedure

LRESULT CALLBACK OverlayWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    // Registered messages have no compile-time value, so this cannot be a case
    // label. NIM_ADD is idempotent enough: a duplicate add is refused, and the
    // icon we already have stays put.
    if (msg != 0 && msg == g_taskbarCreatedMsg) {
        AddTrayIcon(hwnd);
        return 0;
    }

    switch (msg) {
    case WM_CREATE: {
        g_hwndOverlay = hwnd;
        g_dpi = GetDpiForWindow(hwnd);
        g_brushSearchBg = CreateSolidBrush(CLR_SEARCH_BG);

        g_hwndEdit = CreateWindowExW(
            0, L"EDIT", L"",
            WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
            0, 0, 0, 0, hwnd,
            reinterpret_cast<HMENU>(static_cast<UINT_PTR>(IDC_SEARCH_EDIT)),
            g_hInstance, nullptr);
        g_editBaseProc = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(
            g_hwndEdit, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(EditSubclassProc)));
        SendMessageW(g_hwndEdit, EM_SETCUEBANNER, TRUE,
                     reinterpret_cast<LPARAM>(L"Search windows…"));

        CreateFonts();
        return 0;
    }

    case WM_APP_HOTKEY:
        if (LaunchpadVisible())
            HideLaunchpad();
        if (!g_overlayVisible)
            ShowOverlay();
        else
            MoveSelection(wParam ? -1 : 1);
        return 0;

    case WM_APP_LAUNCHPAD:
        HideOverlay();
        if (LaunchpadVisible())
            HideLaunchpad();
        else
            ShowLaunchpad();
        return 0;

    case WM_APP_ROTATE:
        // The switcher is sized to the monitor it opens on, so a rotation
        // underneath it would leave it stale; it reopens re-laid-out.
        HideOverlay();
        HideLaunchpad();
        CycleOrientation();
        return 0;

    case WM_APP_TRAY:
        if (LOWORD(lParam) == WM_RBUTTONUP || LOWORD(lParam) == WM_CONTEXTMENU)
            ShowTrayMenu(hwnd);
        else if (LOWORD(lParam) == WM_LBUTTONDBLCLK)
            ShowOverlay();
        return 0;

    case WM_COMMAND:
        if (LOWORD(wParam) == IDC_SEARCH_EDIT && HIWORD(wParam) == EN_CHANGE) {
            if (g_overlayVisible) {
                ApplyFilter();
                LayoutOverlay(false);
            }
            return 0;
        }
        if (LOWORD(wParam) == IDM_LAUNCHPAD) {
            HideOverlay();
            ShowLaunchpad();
            return 0;
        }
        if (LOWORD(wParam) == IDM_SETTINGS) {
            HideOverlay();
            HideLaunchpad();
            ShowSettingsDialog(hwnd, g_hInstance);
            return 0;
        }
        if (LOWORD(wParam) == IDM_EXIT) {
            DestroyWindow(hwnd);
            return 0;
        }
        break;

    case WM_KEYDOWN:
        if (HandleNavigationKey(wParam))
            return 0;
        break;

    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        RECT client;
        GetClientRect(hwnd, &client);
        PaintOverlay(hdc, client);
        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_ERASEBKGND:
        return 1;

    case WM_CTLCOLOREDIT: {
        HDC hdc = reinterpret_cast<HDC>(wParam);
        SetBkColor(hdc, CLR_SEARCH_BG);
        SetTextColor(hdc, CLR_TEXT);
        return reinterpret_cast<LRESULT>(g_brushSearchBg);
    }

    case WM_MOUSEMOVE: {
        const POINT pt{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        if (!MouseActuallyMoved(hwnd, pt))
            return 0;
        const int row = RowFromPoint(pt.y);
        if (row >= 0 && row != g_selected) {
            g_selected = row;
            InvalidateRect(hwnd, nullptr, TRUE);
        }
        return 0;
    }

    case WM_LBUTTONDOWN: {
        const int row = RowFromPoint(GET_Y_LPARAM(lParam));
        if (row >= 0) {
            g_selected = row;
            ActivateSelection();
        }
        return 0;
    }

    case WM_ACTIVATE:
        if (LOWORD(wParam) == WA_INACTIVE && g_overlayVisible) {
            // A window activated just before the overlay opened (e.g. via a
            // quick switch-then-switch-again) can finish its asynchronous
            // activation AFTER the overlay shows, yanking focus away. Within
            // a short grace period, reclaim focus instead of dismissing.
            if (GetTickCount() - g_shownTick < 400) {
                ForceForeground(hwnd);
                SetFocus(g_hwndEdit);
            } else {
                // Normal dismissal: user clicked elsewhere, etc.
                HideOverlay();
            }
        }
        return 0;

    case WM_SYSCOMMAND:
        // Releasing Alt alone must not enter the window-menu loop.
        if ((wParam & 0xFFF0) == SC_KEYMENU)
            return 0;
        break;

    case WM_DPICHANGED:
        g_dpi = HIWORD(wParam);
        CreateFonts();
        if (g_overlayVisible)
            LayoutOverlay(true);
        return 0;

    case WM_DESTROY:
        Shell_NotifyIconW(NIM_DELETE, &g_trayIcon);
        PostQuitMessage(0);
        return 0;
    }

    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

} // namespace

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, PWSTR, int)
{
    g_hInstance = hInstance;

    HANDLE mutex = CreateMutexW(nullptr, TRUE, kMutexName);
    if (mutex == nullptr || GetLastError() == ERROR_ALREADY_EXISTS)
        return 0;

    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    LoadSettings();
    LoadAliases();

    INITCOMMONCONTROLSEX icc{ sizeof(icc), ICC_STANDARD_CLASSES | ICC_LISTVIEW_CLASSES };
    InitCommonControlsEx(&icc);

    WNDCLASSEXW wc{ sizeof(wc) };
    wc.lpfnWndProc = OverlayWndProc;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hIcon = LoadIconW(hInstance, MAKEINTRESOURCEW(IDI_APP));
    wc.lpszClassName = kOverlayClass;
    wc.style = CS_HREDRAW | CS_VREDRAW;
    RegisterClassExW(&wc);

    HWND hwnd = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW, kOverlayClass, L"Monopane",
        WS_POPUP | WS_CLIPCHILDREN,
        0, 0, 100, 100, nullptr, nullptr, hInstance, nullptr);
    if (!hwnd)
        return 1;

    // Rounded corners on Windows 11 (no-op on Windows 10).
    DWORD cornerPref = 2;  // DWMWCP_ROUND
    DwmSetWindowAttribute(hwnd, 33 /*DWMWA_WINDOW_CORNER_PREFERENCE*/,
                          &cornerPref, sizeof(cornerPref));

    if (!CreateLaunchpadWindow(hInstance)) {
        DestroyWindow(hwnd);
        return 1;
    }

    g_taskbarCreatedMsg = RegisterWindowMessageW(L"TaskbarCreated");

    // Started by the logon task, this is where the first seconds after sign-in
    // are spent; on a hand-launched run it returns at once.
    WaitForShell(60000);

    AddTrayIcon(hwnd);

    // The hook goes in last, with nothing between it and the pump below.
    //
    // A low-level hook is dispatched on this thread's message queue, so a hook
    // installed while the thread is still busy elsewhere has to wait for the
    // work to finish before it can answer. Windows only waits so long
    // (LowLevelHooksTimeout) and then quietly drops the hook — the process
    // stays up, the tray icon stays put, and no key is ever seen again. At
    // logon, where Shell_NotifyIcon above can block on a taskbar that is itself
    // still starting, that gap was wide enough to lose the hook now and then.
    g_keyboardHook = SetWindowsHookExW(WH_KEYBOARD_LL, LowLevelKeyboardProc, hInstance, 0);
    if (!g_keyboardHook) {
        MessageBoxW(nullptr, L"Failed to install the keyboard hook.",
                    L"Monopane", MB_ICONERROR);
        DestroyWindow(hwnd);
        return 1;
    }

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    UnhookWindowsHookEx(g_keyboardHook);
    ClearIconCache();
    DestroyLaunchpadResources();
    if (g_fontApp) DeleteObject(g_fontApp);
    if (g_fontTitle) DeleteObject(g_fontTitle);
    if (g_fontEdit) DeleteObject(g_fontEdit);
    if (g_brushSearchBg) DeleteObject(g_brushSearchBg);
    CoUninitialize();
    ReleaseMutex(mutex);
    CloseHandle(mutex);
    return 0;
}
