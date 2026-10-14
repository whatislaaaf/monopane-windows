#include "settings.h"

#include <commctrl.h>
#include <windowsx.h>

#include <string>

#include "aliases.h"
#include "config.h"
#include "darkui.h"
#include "keysettings.h"
#include "lock.h"
#include "../res/resource.h"

Settings g_settings;

namespace {

constexpr wchar_t kSettingsKey[] = L"Software\\Monopane";
constexpr wchar_t kRunKey[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
constexpr wchar_t kRunValueName[] = L"Monopane";

bool ReadBool(HKEY key, const wchar_t* name, bool fallback)
{
    DWORD value = 0;
    DWORD size = sizeof(value);
    DWORD type = 0;
    if (RegQueryValueExW(key, name, nullptr, &type,
                         reinterpret_cast<BYTE*>(&value), &size) == ERROR_SUCCESS &&
        type == REG_DWORD)
        return value != 0;
    return fallback;
}

void WriteBool(HKEY key, const wchar_t* name, bool value)
{
    const DWORD data = value ? 1 : 0;
    RegSetValueExW(key, name, 0, REG_DWORD,
                   reinterpret_cast<const BYTE*>(&data), sizeof(data));
}

// The chord's text form is pure ASCII (hex and lowercase tags), so it needs no
// encoding care beyond widening it for the registry.
std::string ReadAscii(HKEY key, const wchar_t* name)
{
    wchar_t buf[128] = {};
    DWORD size = sizeof(buf);
    DWORD type = 0;
    if (RegQueryValueExW(key, name, nullptr, &type, reinterpret_cast<BYTE*>(buf), &size) !=
            ERROR_SUCCESS ||
        type != REG_SZ)
        return std::string();
    std::string out;
    for (const wchar_t* p = buf; *p; ++p)
        if (*p >= 32 && *p < 127)
            out += static_cast<char>(*p);
    return out;
}

void WriteAscii(HKEY key, const wchar_t* name, const std::string& value)
{
    std::wstring wide(value.begin(), value.end());
    RegSetValueExW(key, name, 0, REG_SZ, reinterpret_cast<const BYTE*>(wide.c_str()),
                   static_cast<DWORD>((wide.size() + 1) * sizeof(wchar_t)));
}

// ------------------------------------------------------------ switcher tab

// The tab edits these copies; g_settings only changes on OK.
keychord::KeyChord g_editHotkey;
keychord::KeyChord g_editRotate;

// Which of the chords the running capture is for, if any.
enum class Capturing { None, Hotkey, Rotate };
Capturing g_capturing = Capturing::None;

void ShowHotkeys(HWND page)
{
    const bool busy = g_capturing != Capturing::None;
    SetDlgItemTextW(page, IDC_TXT_HOTKEY,
                    g_capturing == Capturing::Hotkey
                        ? L"Press it now…"
                        : keychord::DescribeChord(g_editHotkey).c_str());
    SetDlgItemTextW(page, IDC_TXT_ROTATE,
                    g_capturing == Capturing::Rotate
                        ? L"Press it now…"
                        : keychord::DescribeChord(g_editRotate).c_str());
    EnableWindow(GetDlgItem(page, IDC_BTN_HOTKEY), !busy);
    EnableWindow(GetDlgItem(page, IDC_BTN_ROTATE), !busy);
    SetDlgItemTextW(page, IDC_TXT_HOTKEY_HINT,
                    busy
                        ? L"Hold the modifiers and press the trigger key, then let go."
                          L"  (Esc cancels)"
                        : L"Left and right are told apart, and a key the key map remaps "
                          L"is told from a real one.  Rotating cycles the monitor under "
                          L"the cursor: landscape → portrait → portrait (flipped).");
}

INT_PTR CALLBACK SwitcherTabProc(HWND page, UINT msg, WPARAM wParam, LPARAM lParam)
{
    INT_PTR darkResult = 0;
    if (dark::Handle(page, msg, wParam, lParam, darkResult))
        return darkResult;

    switch (msg) {
    case WM_INITDIALOG:
        dark::Apply(page);
        CheckDlgButton(page, IDC_CHK_ACTIVATE_ALL, g_settings.activateAllOfApp ? BST_CHECKED : BST_UNCHECKED);
        CheckDlgButton(page, IDC_CHK_PRESELECT, g_settings.preselectPrevious ? BST_CHECKED : BST_UNCHECKED);
        CheckDlgButton(page, IDC_CHK_MATCH_APP, g_settings.matchAppName ? BST_CHECKED : BST_UNCHECKED);
        g_capturing = Capturing::None;
        g_editHotkey = g_settings.hotkey;
        g_editRotate = g_settings.rotateHotkey;
        ShowHotkeys(page);
        return TRUE;

    case keychord::WM_CHORD_CAPTURED:
        if (g_capturing == Capturing::Rotate)
            g_editRotate = keychord::CapturedChord();
        else if (g_capturing == Capturing::Hotkey)
            g_editHotkey = keychord::CapturedChord();
        g_capturing = Capturing::None;
        ShowHotkeys(page);
        return TRUE;

    case keychord::WM_CHORD_CANCELLED:
        g_capturing = Capturing::None;
        ShowHotkeys(page);
        return TRUE;

    // The only timer here is the capture backstop inside keychord, which stops
    // an abandoned capture swallowing every keystroke.
    case WM_TIMER:
        if (g_capturing != Capturing::None) {
            keychord::CancelChordCapture();
            g_capturing = Capturing::None;
            ShowHotkeys(page);
        } else {
            // A capture that completed in the hook could not stop this: the
            // hook runs on its own thread, and KillTimer only works on the
            // window's. Stop it on its first tick instead.
            KillTimer(page, wParam);
        }
        return TRUE;

    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case IDC_BTN_HOTKEY:
            if (keychord::BeginChordCapture(page))
                g_capturing = Capturing::Hotkey;
            ShowHotkeys(page);
            return TRUE;

        case IDC_BTN_ROTATE:
            if (keychord::BeginChordCapture(page))
                g_capturing = Capturing::Rotate;
            ShowHotkeys(page);
            return TRUE;

        case IDC_BTN_CLEAR_MEMORY:
            if (MessageBoxW(page, L"Forget all learned search aliases?", L"Monopane",
                            MB_YESNO | MB_ICONQUESTION) == IDYES) {
                ClearAliases();
                EnableWindow(GetDlgItem(page, IDC_BTN_CLEAR_MEMORY), FALSE);
                SetDlgItemTextW(page, IDC_BTN_CLEAR_MEMORY, L"Memory cleared");
            }
            return TRUE;
        }
        break;
    }
    return FALSE;
}

// Called by the host on OK, with g_settingsLock held exclusive.
void CommitSwitcherTab(HWND page)
{
    g_settings.activateAllOfApp = IsDlgButtonChecked(page, IDC_CHK_ACTIVATE_ALL) == BST_CHECKED;
    g_settings.preselectPrevious = IsDlgButtonChecked(page, IDC_CHK_PRESELECT) == BST_CHECKED;
    g_settings.matchAppName = IsDlgButtonChecked(page, IDC_CHK_MATCH_APP) == BST_CHECKED;
    if (keychord::ChordValid(g_editHotkey))
        g_settings.hotkey = g_editHotkey;
    if (keychord::ChordValid(g_editRotate))
        g_settings.rotateHotkey = g_editRotate;
}

// ------------------------------------------------------------- host dialog

constexpr int kTabCount = 3;
const wchar_t* const kTabNames[kTabCount] = { L"Switcher", L"Key setup", L"Key options" };

struct HostState {
    KeysEdit* keys = nullptr;
    HWND tabs[kTabCount] = {};
    int selected = 0;
    // Autostart state when the dialog opened; SetAutoStart runs elevated (UAC
    // prompt), so it is only called when the checkbox actually changed.
    bool autoStartAtOpen = false;
};

HostState* Host(HWND dlg)
{
    return reinterpret_cast<HostState*>(GetWindowLongPtrW(dlg, DWLP_USER));
}

// The tab strip is painted by the dialog itself rather than being a tab
// control: SysTabControl32 only comes in light, and a row of names with an
// accent line under the current one is all that is wanted of it anyway.
// Everything below is in dialog units until MapDialogRect.
constexpr int kStripBottomDu = 28;
constexpr int kTabPadDu = 10;

RECT PageRectPx(HWND dlg)
{
    RECT rc{ 6, kStripBottomDu + 2, 414, 270 };
    MapDialogRect(dlg, &rc);
    return rc;
}

// The strip's tab rectangles, in pixels, measured from the names.
void TabRectsPx(HWND dlg, RECT out[kTabCount])
{
    RECT strip{ kTabPadDu, 0, 0, kStripBottomDu };
    MapDialogRect(dlg, &strip);
    RECT pad{ 0, 0, kTabPadDu, 0 };
    MapDialogRect(dlg, &pad);

    HDC dc = GetDC(dlg);
    HGDIOBJ old = SelectObject(dc, reinterpret_cast<HFONT>(SendMessageW(dlg, WM_GETFONT, 0, 0)));
    int x = strip.left;
    for (int i = 0; i < kTabCount; ++i) {
        SIZE sz{};
        GetTextExtentPoint32W(dc, kTabNames[i], lstrlenW(kTabNames[i]), &sz);
        out[i] = RECT{ x, 0, x + sz.cx + 2 * pad.right, strip.bottom };
        x = out[i].right;
    }
    SelectObject(dc, old);
    ReleaseDC(dlg, dc);
}

void PaintTabStrip(HWND dlg, HostState* st)
{
    PAINTSTRUCT ps;
    HDC dc = BeginPaint(dlg, &ps);
    RECT tabs[kTabCount];
    TabRectsPx(dlg, tabs);

    HGDIOBJ old = SelectObject(dc, reinterpret_cast<HFONT>(SendMessageW(dlg, WM_GETFONT, 0, 0)));
    SetBkMode(dc, TRANSPARENT);
    const int underline = dark::Px(dlg, 2);
    for (int i = 0; i < kTabCount; ++i) {
        const bool current = st && i == st->selected;
        SetTextColor(dc, current ? dark::kText : dark::kDim);
        DrawTextW(dc, kTabNames[i], -1, &tabs[i], DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        if (current) {
            RECT line{ tabs[i].left, tabs[i].bottom - underline, tabs[i].right, tabs[i].bottom };
            HBRUSH accent = CreateSolidBrush(dark::kAccent);
            FillRect(dc, &line, accent);
            DeleteObject(accent);
        }
    }
    // A hairline under the whole strip, which the accent sits on.
    RECT client;
    GetClientRect(dlg, &client);
    RECT rule{ tabs[0].left, tabs[0].bottom, client.right - tabs[0].left, tabs[0].bottom + 1 };
    HBRUSH line = CreateSolidBrush(dark::kLine);
    FillRect(dc, &rule, line);
    DeleteObject(line);
    SelectObject(dc, old);
    EndPaint(dlg, &ps);
}

void SelectTab(HWND dlg, HostState* st, int index)
{
    st->selected = index;
    for (int i = 0; i < kTabCount; ++i)
        ShowWindow(st->tabs[i], i == index ? SW_SHOW : SW_HIDE);
    RECT strip{ 0, 0, 420, kStripBottomDu + 1 };
    MapDialogRect(dlg, &strip);
    InvalidateRect(dlg, &strip, TRUE);
}

INT_PTR CALLBACK SettingsDlgProc(HWND dlg, UINT msg, WPARAM wParam, LPARAM lParam)
{
    HostState* st = Host(dlg);

    INT_PTR darkResult = 0;
    if (dark::Handle(dlg, msg, wParam, lParam, darkResult))
        return darkResult;

    switch (msg) {
    case WM_INITDIALOG: {
        st = new HostState();
        st->keys = BeginKeysEdit();
        SetWindowLongPtrW(dlg, DWLP_USER, reinterpret_cast<LONG_PTR>(st));
        dark::Apply(dlg);

        SendMessageW(dlg, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(
            LoadIconW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(IDI_APP))));

        HINSTANCE instance = reinterpret_cast<HINSTANCE>(
            GetWindowLongPtrW(dlg, GWLP_HINSTANCE));
        st->tabs[0] = CreateDialogParamW(instance, MAKEINTRESOURCEW(IDD_TAB_SWITCHER), dlg,
                                         SwitcherTabProc, 0);
        st->tabs[1] = CreateKeysTab(dlg, instance, st->keys);
        st->tabs[2] = CreateKeyOptionsTab(dlg, instance, st->keys);
        const RECT page = PageRectPx(dlg);
        for (HWND tab : st->tabs)
            SetWindowPos(tab, HWND_TOP, page.left, page.top, page.right - page.left,
                         page.bottom - page.top, SWP_NOACTIVATE);
        SelectTab(dlg, st, 0);

        st->autoStartAtOpen = IsAutoStartEnabled();
        CheckDlgButton(dlg, IDC_CHK_AUTOSTART, st->autoStartAtOpen ? BST_CHECKED : BST_UNCHECKED);
        return TRUE;
    }

    case WM_PAINT:
        PaintTabStrip(dlg, st);
        return TRUE;

    case WM_LBUTTONDOWN: {
        if (!st)
            return FALSE;
        const POINT pt{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        RECT tabs[kTabCount];
        TabRectsPx(dlg, tabs);
        for (int i = 0; i < kTabCount; ++i)
            if (PtInRect(&tabs[i], pt) && i != st->selected)
                SelectTab(dlg, st, i);
        return TRUE;
    }

    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case IDC_BTN_CONFIGFOLDER:
            // Opens what is on disk, which is the saved config — not the edits
            // still pending in this dialog.
            OpenConfigFolder();
            return TRUE;

        case IDOK: {
            if (!st)
                return TRUE;
            keychord::CancelChordCapture();
            g_capturing = Capturing::None;

            AcquireSRWLockExclusive(&g_settingsLock);
            CommitSwitcherTab(st->tabs[0]);
            ReleaseSRWLockExclusive(&g_settingsLock);
            SaveSettings();

            CommitKeysEdit(dlg, st->keys);

            const bool autoStart = IsDlgButtonChecked(dlg, IDC_CHK_AUTOSTART) == BST_CHECKED;
            if (autoStart != st->autoStartAtOpen)
                SetAutoStart(autoStart);
            EndDialog(dlg, IDOK);
            return TRUE;
        }

        case IDCANCEL:
            keychord::CancelChordCapture();
            g_capturing = Capturing::None;
            EndDialog(dlg, IDCANCEL);
            return TRUE;
        }
        break;

    case WM_DESTROY:
        // Closing while a capture is armed would leave the hook swallowing
        // every keystroke with nowhere to deliver it.
        keychord::CancelChordCapture();
        g_capturing = Capturing::None;
        if (st) {
            EndKeysEdit(st->keys);
            delete st;
            SetWindowLongPtrW(dlg, DWLP_USER, 0);
        }
        return 0;
    }
    return FALSE;
}

} // namespace

void DefaultHotkey(keychord::KeyChord& out)
{
    out = keychord::KeyChord();
    out.mods[0] = 0x1D;                               // left ctrl...
    out.modOrigin[0] = keychord::KeyOrigin::Injected; // ...as the key map injects it
    out.modCount = 1;
    out.trigger = 0x0F;                                  // Tab
    out.triggerOrigin = keychord::KeyOrigin::Physical;
}

void DefaultRotateHotkey(keychord::KeyChord& out)
{
    // Origin::Any throughout: with a remapped Ctrl the left Ctrl arrives
    // injected and this reads as Cmd+Alt+R, without one the physical
    // Ctrl+Alt+R does the same job. Nothing here needs the two told apart.
    out = keychord::KeyChord();
    out.mods[0] = 0x1D;  // left ctrl
    out.mods[1] = 0x38;  // left alt
    out.modCount = 2;
    out.trigger = 0x13;  // R
}

void LoadSettings()
{
    DefaultHotkey(g_settings.hotkey);
    DefaultRotateHotkey(g_settings.rotateHotkey);

    HKEY key;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kSettingsKey, 0, KEY_QUERY_VALUE, &key) != ERROR_SUCCESS)
        return;
    g_settings.activateAllOfApp = ReadBool(key, L"ActivateAllOfApp", g_settings.activateAllOfApp);
    g_settings.preselectPrevious = ReadBool(key, L"PreselectPrevious", g_settings.preselectPrevious);
    g_settings.matchAppName = ReadBool(key, L"MatchAppName", g_settings.matchAppName);

    // A stored chord that no longer parses leaves the default in place, rather
    // than an app with no way to open the switcher at all.
    keychord::KeyChord parsed;
    const std::string stored = ReadAscii(key, L"Hotkey");
    if (!stored.empty() && keychord::ParseChord(stored, parsed))
        g_settings.hotkey = parsed;

    const std::string storedRotate = ReadAscii(key, L"RotateHotkey");
    if (!storedRotate.empty() && keychord::ParseChord(storedRotate, parsed))
        g_settings.rotateHotkey = parsed;

    RegCloseKey(key);
}

void SaveSettings()
{
    HKEY key;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, kSettingsKey, 0, nullptr, 0,
                        KEY_SET_VALUE, nullptr, &key, nullptr) != ERROR_SUCCESS)
        return;
    WriteBool(key, L"ActivateAllOfApp", g_settings.activateAllOfApp);
    WriteBool(key, L"PreselectPrevious", g_settings.preselectPrevious);
    WriteBool(key, L"MatchAppName", g_settings.matchAppName);
    WriteAscii(key, L"Hotkey", keychord::FormatChord(g_settings.hotkey));
    WriteAscii(key, L"RotateHotkey", keychord::FormatChord(g_settings.rotateHotkey));
    RegCloseKey(key);
}

// Autostart uses a Task Scheduler logon task ("Monopane") rather than the
// classic Run key: Run-key apps are staggered by a minute or more at logon
// (and proved unreliable here), while a logon task fires within seconds.

bool IsAutoStartEnabled()
{
    STARTUPINFOW si = { sizeof(si) };
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION pi = {};
    wchar_t cmd[] = L"schtasks.exe /Query /TN Monopane";
    if (!CreateProcessW(nullptr, cmd, nullptr, nullptr, FALSE, CREATE_NO_WINDOW,
                        nullptr, nullptr, &si, &pi))
        return false;
    WaitForSingleObject(pi.hProcess, 5000);
    DWORD code = 1;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return code == 0;
}

void SetAutoStart(bool enable)
{
    // Drop any stale Run-key entry from before the scheduled-task switch.
    HKEY key;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey, 0, KEY_SET_VALUE, &key) == ERROR_SUCCESS) {
        RegDeleteValueW(key, kRunValueName);
        RegCloseKey(key);
    }

    wchar_t params[MAX_PATH + 80];
    if (enable) {
        wchar_t exePath[MAX_PATH];
        GetModuleFileNameW(nullptr, exePath, MAX_PATH);
        wsprintfW(params, L"/Create /F /SC ONLOGON /RL LIMITED /TN Monopane /TR \"\\\"%s\\\"\"",
                  exePath);
    } else {
        wsprintfW(params, L"/Delete /F /TN Monopane");
    }

    // Creating or deleting a logon-trigger task needs elevation, so this
    // shows a UAC prompt when the checkbox is toggled.
    SHELLEXECUTEINFOW sei = { sizeof(sei) };
    sei.fMask = SEE_MASK_NOCLOSEPROCESS;
    sei.lpVerb = L"runas";
    sei.lpFile = L"schtasks.exe";
    sei.lpParameters = params;
    sei.nShow = SW_HIDE;
    if (ShellExecuteExW(&sei) && sei.hProcess) {
        WaitForSingleObject(sei.hProcess, 10000);
        CloseHandle(sei.hProcess);
    }
}

void ShowSettingsDialog(HWND owner, HINSTANCE instance)
{
    INITCOMMONCONTROLSEX icc = { sizeof(icc), ICC_TAB_CLASSES | ICC_STANDARD_CLASSES };
    InitCommonControlsEx(&icc);
    DialogBoxW(instance, MAKEINTRESOURCEW(IDD_SETTINGS), owner, SettingsDlgProc);
}
