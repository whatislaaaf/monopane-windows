// Monopane — a searchable Cmd+Tab window switcher for Windows.
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
#include "fuzzy.h"
#include "settings.h"
#include "window_list.h"
#include "../res/resource.h"

namespace {

constexpr wchar_t kOverlayClass[] = L"MonopaneOverlay";
constexpr wchar_t kMutexName[] = L"MonopaneSingleInstance";

// dwExtraInfo marker MacKeys stamps on the input it injects ("MKYS").
constexpr ULONG_PTR kMacKeysMarker = 0x4D4B5953;

constexpr UINT WM_APP_HOTKEY = WM_APP + 1;  // wParam: 1 = shift held (cycle up)
constexpr UINT WM_APP_TRAY = WM_APP + 2;

constexpr UINT IDC_SEARCH_EDIT = 100;
constexpr UINT IDM_EXIT = 201;
constexpr UINT IDM_SETTINGS = 202;
constexpr UINT TRAY_ICON_ID = 1;

constexpr int MAX_VISIBLE_ROWS = 10;

// Base (96-dpi) metrics.
constexpr int BASE_WIDTH = 600;
constexpr int BASE_SEARCH_H = 56;
constexpr int BASE_ROW_H = 44;
constexpr int BASE_PAD = 10;
constexpr int BASE_ICON = 24;

constexpr COLORREF CLR_BG = RGB(30, 30, 30);
constexpr COLORREF CLR_SEARCH_BG = RGB(45, 45, 45);
constexpr COLORREF CLR_SELECTION = RGB(0, 95, 184);
constexpr COLORREF CLR_TEXT = RGB(242, 242, 242);
constexpr COLORREF CLR_TEXT_DIM = RGB(170, 170, 170);

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
NOTIFYICONDATAW g_trayIcon{};

int Scale(int value)
{
    return MulDiv(value, g_dpi, 96);
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

    if (g_hwndEdit)
        SendMessageW(g_hwndEdit, WM_SETFONT, reinterpret_cast<WPARAM>(g_fontEdit), TRUE);
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
    else if (g_selected >= g_scrollTop + MAX_VISIBLE_ROWS)
        g_scrollTop = g_selected - MAX_VISIBLE_ROWS + 1;
}

// ---------------------------------------------------------------------------
// Overlay layout & visibility

void LayoutOverlay(bool reposition)
{
    const int width = Scale(BASE_WIDTH);
    const int searchH = Scale(BASE_SEARCH_H);
    const int rowH = Scale(BASE_ROW_H);
    const int pad = Scale(BASE_PAD);

    int rows = std::min(static_cast<int>(g_filtered.size()), MAX_VISIBLE_ROWS);
    if (rows == 0)
        rows = 1;  // room for the "no matching windows" row
    const int height = searchH + rows * rowH + pad;

    UINT flags = SWP_NOACTIVATE;
    int x = 0, y = 0;
    if (reposition) {
        POINT cursor;
        GetCursorPos(&cursor);
        HMONITOR monitor = MonitorFromPoint(cursor, MONITOR_DEFAULTTOPRIMARY);
        MONITORINFO mi{ sizeof(mi) };
        GetMonitorInfoW(monitor, &mi);
        x = mi.rcWork.left + (mi.rcWork.right - mi.rcWork.left - width) / 2;
        y = mi.rcWork.top + (mi.rcWork.bottom - mi.rcWork.top) / 5;
    } else {
        flags |= SWP_NOMOVE;
    }

    SetWindowPos(g_hwndOverlay, HWND_TOPMOST, x, y, width, height, flags);
    MoveWindow(g_hwndEdit, pad * 2, pad + Scale(6), width - pad * 4, searchH - pad * 2 - Scale(6), TRUE);
    InvalidateRect(g_hwndOverlay, nullptr, TRUE);
}

void ForceForeground(HWND hwnd)
{
    if (SetForegroundWindow(hwnd) && GetForegroundWindow() == hwnd)
        return;

    const DWORD fgThread = GetWindowThreadProcessId(GetForegroundWindow(), nullptr);
    const DWORD myThread = GetCurrentThreadId();
    if (fgThread != myThread && AttachThreadInput(myThread, fgThread, TRUE)) {
        SetForegroundWindow(hwnd);
        AttachThreadInput(myThread, fgThread, FALSE);
    }

    if (GetForegroundWindow() != hwnd) {
        // Last resort: a synthesized key event satisfies the foreground-lock
        // heuristic ("the process received the last input event").
        INPUT input[2]{};
        input[0].type = INPUT_KEYBOARD;
        input[0].ki.wVk = VK_MENU;
        input[1].type = INPUT_KEYBOARD;
        input[1].ki.wVk = VK_MENU;
        input[1].ki.dwFlags = KEYEVENTF_KEYUP;
        SendInput(2, input, sizeof(INPUT));
        SetForegroundWindow(hwnd);
    }
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

    // Search box background (the edit control paints itself on top)
    RECT searchRect{ pad, pad, width - pad, searchH - Scale(4) };
    FillRect(mem, &searchRect, g_brushSearchBg);

    SetBkMode(mem, TRANSPARENT);

    if (g_filtered.empty()) {
        RECT row{ pad, searchH, width - pad, searchH + rowH };
        SelectObject(mem, g_fontTitle);
        SetTextColor(mem, CLR_TEXT_DIM);
        DrawTextW(mem, L"No matching windows", -1, &row,
                  DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }

    const int last = std::min(static_cast<int>(g_filtered.size()), g_scrollTop + MAX_VISIBLE_ROWS);
    for (int i = g_scrollTop; i < last; ++i) {
        const WindowInfo& win = g_windows[g_filtered[i]];
        const int y = searchH + (i - g_scrollTop) * rowH;

        if (i == g_selected) {
            HBRUSH selBrush = CreateSolidBrush(CLR_SELECTION);
            HPEN selPen = CreatePen(PS_SOLID, 1, CLR_SELECTION);
            HGDIOBJ oldBrush = SelectObject(mem, selBrush);
            HGDIOBJ oldPen = SelectObject(mem, selPen);
            RoundRect(mem, pad, y + Scale(2), width - pad, y + rowH - Scale(2), Scale(8), Scale(8));
            SelectObject(mem, oldBrush);
            SelectObject(mem, oldPen);
            DeleteObject(selBrush);
            DeleteObject(selPen);
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
    if (row >= static_cast<int>(g_filtered.size()) || row >= g_scrollTop + MAX_VISIBLE_ROWS)
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
        MoveSelection(MAX_VISIBLE_ROWS);
        return true;
    case VK_PRIOR:
        MoveSelection(-MAX_VISIBLE_ROWS);
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

// True while MacKeys' cmd-remap Ctrl (its marker on the injected event) is
// held. Only used to tell that Ctrl apart from a physically pressed Ctrl —
// the authoritative "is the key down NOW" checks below use GetAsyncKeyState,
// so a missed keyup event (hook timeout while the thread was busy) cannot
// leave the hotkey stuck on.
bool g_cmdCtrlDown = false;

LRESULT CALLBACK LowLevelKeyboardProc(int code, WPARAM wParam, LPARAM lParam)
{
    if (code == HC_ACTION) {
        const auto* kb = reinterpret_cast<const KBDLLHOOKSTRUCT*>(lParam);
        const bool down = wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN;
        const bool injected = (kb->flags & LLKHF_INJECTED) != 0;

        if (kb->vkCode == VK_LCONTROL && injected &&
            kb->dwExtraInfo == kMacKeysMarker) {
            g_cmdCtrlDown = down;
        } else if (kb->vkCode == VK_TAB && down && !injected) {
            // Self-heal: if the flag claims MacKeys' Ctrl is held but no Ctrl
            // is actually down, the release event was missed — clear it.
            if (g_cmdCtrlDown && !(GetAsyncKeyState(VK_LCONTROL) & 0x8000))
                g_cmdCtrlDown = false;

            // Cmd is held either as MacKeys' injected Ctrl (verified against
            // real key state) or as raw F23 when MacKeys is paused or absent
            // (only then do F23 events reach the async key state at all).
            const bool cmdHeld =
                g_cmdCtrlDown || (GetAsyncKeyState(VK_F23) & 0x8000);
            if (cmdHeld) {
                const WPARAM shift = (GetAsyncKeyState(VK_SHIFT) & 0x8000) ? 1 : 0;
                PostMessageW(g_hwndOverlay, WM_APP_HOTKEY, shift, 0);
                return 1;  // the Tab belongs to the switcher
            }
        }
    }
    return CallNextHookEx(g_keyboardHook, code, wParam, lParam);
}

// ---------------------------------------------------------------------------
// Tray icon

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
        if (!g_overlayVisible)
            ShowOverlay();
        else
            MoveSelection(wParam ? -1 : 1);
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
        if (LOWORD(wParam) == IDM_SETTINGS) {
            HideOverlay();
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
        const int row = RowFromPoint(GET_Y_LPARAM(lParam));
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

    INITCOMMONCONTROLSEX icc{ sizeof(icc), ICC_STANDARD_CLASSES };
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

    g_keyboardHook = SetWindowsHookExW(WH_KEYBOARD_LL, LowLevelKeyboardProc, hInstance, 0);
    if (!g_keyboardHook) {
        MessageBoxW(nullptr, L"Failed to install the keyboard hook.",
                    L"Monopane", MB_ICONERROR);
        DestroyWindow(hwnd);
        return 1;
    }

    AddTrayIcon(hwnd);

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    UnhookWindowsHookEx(g_keyboardHook);
    ClearIconCache();
    if (g_fontApp) DeleteObject(g_fontApp);
    if (g_fontTitle) DeleteObject(g_fontTitle);
    if (g_fontEdit) DeleteObject(g_fontEdit);
    if (g_brushSearchBg) DeleteObject(g_brushSearchBg);
    CoUninitialize();
    ReleaseMutex(mutex);
    CloseHandle(mutex);
    return 0;
}
