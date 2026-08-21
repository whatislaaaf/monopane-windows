#include "settings.h"

#include <string>

#include "aliases.h"
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

// Autostart state when the dialog opened; SetAutoStart runs elevated (UAC
// prompt), so it is only called when the checkbox actually changed.
bool g_autoStartAtOpen = false;

// The dialog edits this copy; g_settings.hotkey only changes on OK.
keychord::KeyChord g_editHotkey;
bool g_capturing = false;

void ShowHotkey(HWND dlg)
{
    SetDlgItemTextW(dlg, IDC_TXT_HOTKEY, keychord::DescribeChord(g_editHotkey).c_str());
    EnableWindow(GetDlgItem(dlg, IDC_BTN_HOTKEY), !g_capturing);
    SetDlgItemTextW(dlg, IDC_TXT_HOTKEY_HINT,
                    g_capturing
                        ? L"Hold the modifiers and press the trigger key, then let go."
                          L"  (Esc cancels)"
                        : L"Left and right are told apart, and a key remapped by MacKeys "
                          L"is told from a real one.");
}

INT_PTR CALLBACK SettingsDlgProc(HWND dlg, UINT msg, WPARAM wParam, LPARAM)
{
    switch (msg) {
    case WM_INITDIALOG:
        SendMessageW(dlg, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(
            LoadIconW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(IDI_APP))));
        CheckDlgButton(dlg, IDC_CHK_ACTIVATE_ALL, g_settings.activateAllOfApp ? BST_CHECKED : BST_UNCHECKED);
        CheckDlgButton(dlg, IDC_CHK_PRESELECT, g_settings.preselectPrevious ? BST_CHECKED : BST_UNCHECKED);
        CheckDlgButton(dlg, IDC_CHK_MATCH_APP, g_settings.matchAppName ? BST_CHECKED : BST_UNCHECKED);
        g_autoStartAtOpen = IsAutoStartEnabled();
        CheckDlgButton(dlg, IDC_CHK_AUTOSTART, g_autoStartAtOpen ? BST_CHECKED : BST_UNCHECKED);
        g_capturing = false;
        g_editHotkey = g_settings.hotkey;
        ShowHotkey(dlg);
        return TRUE;

    case keychord::WM_CHORD_CAPTURED:
        g_capturing = false;
        g_editHotkey = keychord::CapturedChord();
        ShowHotkey(dlg);
        return TRUE;

    case keychord::WM_CHORD_CANCELLED:
        g_capturing = false;
        ShowHotkey(dlg);
        return TRUE;

    // The only timer here is the capture backstop inside keychord, which stops
    // an abandoned capture swallowing every keystroke.
    case WM_TIMER:
        if (g_capturing) {
            keychord::CancelChordCapture();
            g_capturing = false;
            ShowHotkey(dlg);
        }
        return TRUE;

    case WM_DESTROY:
        // Closing mid-capture would leave the hook eating keys with nowhere
        // to deliver them.
        keychord::CancelChordCapture();
        g_capturing = false;
        return FALSE;

    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case IDC_BTN_HOTKEY:
            g_capturing = keychord::BeginChordCapture(dlg);
            ShowHotkey(dlg);
            return TRUE;

        case IDOK: {
            keychord::CancelChordCapture();
            g_settings.activateAllOfApp = IsDlgButtonChecked(dlg, IDC_CHK_ACTIVATE_ALL) == BST_CHECKED;
            g_settings.preselectPrevious = IsDlgButtonChecked(dlg, IDC_CHK_PRESELECT) == BST_CHECKED;
            g_settings.matchAppName = IsDlgButtonChecked(dlg, IDC_CHK_MATCH_APP) == BST_CHECKED;
            if (keychord::ChordValid(g_editHotkey))
                g_settings.hotkey = g_editHotkey;
            SaveSettings();
            const bool autoStart = IsDlgButtonChecked(dlg, IDC_CHK_AUTOSTART) == BST_CHECKED;
            if (autoStart != g_autoStartAtOpen)
                SetAutoStart(autoStart);
            EndDialog(dlg, IDOK);
            return TRUE;
        }
        case IDCANCEL:
            keychord::CancelChordCapture();
            EndDialog(dlg, IDCANCEL);
            return TRUE;
        case IDC_BTN_CLEAR_MEMORY:
            if (MessageBoxW(dlg, L"Forget all learned search aliases?", L"Monopane",
                            MB_YESNO | MB_ICONQUESTION) == IDYES) {
                ClearAliases();
                EnableWindow(GetDlgItem(dlg, IDC_BTN_CLEAR_MEMORY), FALSE);
                SetDlgItemTextW(dlg, IDC_BTN_CLEAR_MEMORY, L"Memory cleared");
            }
            return TRUE;
        }
        break;
    }
    return FALSE;
}

} // namespace

void DefaultHotkey(keychord::KeyChord& out)
{
    out = keychord::KeyChord();
    out.mods[0] = 0x1D;                               // left ctrl...
    out.modOrigin[0] = keychord::KeyOrigin::Injected; // ...as MacKeys injects it
    out.modCount = 1;
    out.trigger = 0x0F;                                  // Tab
    out.triggerOrigin = keychord::KeyOrigin::Physical;
}

void LoadSettings()
{
    DefaultHotkey(g_settings.hotkey);

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
    RegCloseKey(key);
}

// Autostart uses a Task Scheduler logon task ("Monopane") rather than the
// classic Run key: Run-key apps are staggered by a minute or more at logon
// (and proved unreliable here), while a logon task fires within seconds.
// Same approach as MacKeys.

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
    DialogBoxW(instance, MAKEINTRESOURCEW(IDD_SETTINGS), owner, SettingsDlgProc);
}
