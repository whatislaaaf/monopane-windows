#include "settings.h"

#include <string>

#include "aliases.h"
#include "launchpad.h"
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

// The dialog edits these copies; g_settings only changes on OK.
keychord::KeyChord g_editHotkey;
keychord::KeyChord g_editRotate;
keychord::KeyChord g_editLaunchpad;

// Which of the chords the running capture is for, if any.
enum class Capturing { None, Hotkey, Rotate, Launchpad };
Capturing g_capturing = Capturing::None;

void ShowHotkeys(HWND dlg)
{
    const bool busy = g_capturing != Capturing::None;
    SetDlgItemTextW(dlg, IDC_TXT_HOTKEY,
                    g_capturing == Capturing::Hotkey
                        ? L"Press it now…"
                        : keychord::DescribeChord(g_editHotkey).c_str());
    SetDlgItemTextW(dlg, IDC_TXT_ROTATE,
                    g_capturing == Capturing::Rotate
                        ? L"Press it now…"
                        : keychord::DescribeChord(g_editRotate).c_str());
    SetDlgItemTextW(dlg, IDC_TXT_LAUNCHPAD,
                    g_capturing == Capturing::Launchpad
                        ? L"Press it now…"
                        : keychord::DescribeChord(g_editLaunchpad).c_str());
    EnableWindow(GetDlgItem(dlg, IDC_BTN_HOTKEY), !busy);
    EnableWindow(GetDlgItem(dlg, IDC_BTN_ROTATE), !busy);
    EnableWindow(GetDlgItem(dlg, IDC_BTN_LAUNCHPAD), !busy);
    SetDlgItemTextW(dlg, IDC_TXT_HOTKEY_HINT,
                    busy
                        ? L"Hold the modifiers and press the trigger key, then let go."
                          L"  (Esc cancels)"
                        : L"Left and right are told apart, and a key remapped by MacKeys "
                          L"is told from a real one.  Rotating cycles the monitor under "
                          L"the cursor: landscape → portrait → portrait (flipped).");
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
        g_capturing = Capturing::None;
        g_editHotkey = g_settings.hotkey;
        g_editRotate = g_settings.rotateHotkey;
        g_editLaunchpad = g_settings.launchpadHotkey;
        ShowHotkeys(dlg);
        return TRUE;

    case keychord::WM_CHORD_CAPTURED:
        if (g_capturing == Capturing::Rotate)
            g_editRotate = keychord::CapturedChord();
        else if (g_capturing == Capturing::Launchpad)
            g_editLaunchpad = keychord::CapturedChord();
        else
            g_editHotkey = keychord::CapturedChord();
        g_capturing = Capturing::None;
        ShowHotkeys(dlg);
        return TRUE;

    case keychord::WM_CHORD_CANCELLED:
        g_capturing = Capturing::None;
        ShowHotkeys(dlg);
        return TRUE;

    // The only timer here is the capture backstop inside keychord, which stops
    // an abandoned capture swallowing every keystroke.
    case WM_TIMER:
        if (g_capturing != Capturing::None) {
            keychord::CancelChordCapture();
            g_capturing = Capturing::None;
            ShowHotkeys(dlg);
        }
        return TRUE;

    case WM_DESTROY:
        // Closing mid-capture would leave the hook eating keys with nowhere
        // to deliver them.
        keychord::CancelChordCapture();
        g_capturing = Capturing::None;
        return FALSE;

    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case IDC_BTN_HOTKEY:
            if (keychord::BeginChordCapture(dlg))
                g_capturing = Capturing::Hotkey;
            ShowHotkeys(dlg);
            return TRUE;

        case IDC_BTN_ROTATE:
            if (keychord::BeginChordCapture(dlg))
                g_capturing = Capturing::Rotate;
            ShowHotkeys(dlg);
            return TRUE;

        case IDC_BTN_LAUNCHPAD:
            if (keychord::BeginChordCapture(dlg))
                g_capturing = Capturing::Launchpad;
            ShowHotkeys(dlg);
            return TRUE;

        case IDC_BTN_LAUNCHPAD_FOLDER:
            OpenLaunchpadConfigFolder();
            return TRUE;

        case IDOK: {
            keychord::CancelChordCapture();
            g_settings.activateAllOfApp = IsDlgButtonChecked(dlg, IDC_CHK_ACTIVATE_ALL) == BST_CHECKED;
            g_settings.preselectPrevious = IsDlgButtonChecked(dlg, IDC_CHK_PRESELECT) == BST_CHECKED;
            g_settings.matchAppName = IsDlgButtonChecked(dlg, IDC_CHK_MATCH_APP) == BST_CHECKED;
            if (keychord::ChordValid(g_editHotkey))
                g_settings.hotkey = g_editHotkey;
            if (keychord::ChordValid(g_editRotate))
                g_settings.rotateHotkey = g_editRotate;
            if (keychord::ChordValid(g_editLaunchpad))
                g_settings.launchpadHotkey = g_editLaunchpad;
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

void DefaultRotateHotkey(keychord::KeyChord& out)
{
    // Origin::Any throughout: under MacKeys the left Ctrl arrives injected and
    // this reads as Cmd+Alt+R, without it the physical Ctrl+Alt+R does the same
    // job. Nothing here needs the two told apart.
    out = keychord::KeyChord();
    out.mods[0] = 0x1D;  // left ctrl
    out.mods[1] = 0x38;  // left alt
    out.modCount = 2;
    out.trigger = 0x13;  // R
}

void DefaultLaunchpadHotkey(keychord::KeyChord& out)
{
    out = keychord::KeyChord();
    out.mods[0] = 0x1D;  // left ctrl
    out.modCount = 1;
    out.trigger = 0x39;  // space
}

void LoadSettings()
{
    DefaultHotkey(g_settings.hotkey);
    DefaultRotateHotkey(g_settings.rotateHotkey);
    DefaultLaunchpadHotkey(g_settings.launchpadHotkey);

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

    const std::string storedLaunchpad = ReadAscii(key, L"LaunchpadHotkey");
    if (!storedLaunchpad.empty() && keychord::ParseChord(storedLaunchpad, parsed))
        g_settings.launchpadHotkey = parsed;

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
    WriteAscii(key, L"LaunchpadHotkey", keychord::FormatChord(g_settings.launchpadHotkey));
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
