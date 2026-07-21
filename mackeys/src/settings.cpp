#include "settings.h"

#include <string>

#include "../res/resource.h"

Settings g_settings;

namespace {

constexpr wchar_t kSettingsKey[] = L"Software\\MacKeys";
constexpr wchar_t kRunKey[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
constexpr wchar_t kRunValueName[] = L"MacKeys";

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

INT_PTR CALLBACK SettingsDlgProc(HWND dlg, UINT msg, WPARAM wParam, LPARAM)
{
    switch (msg) {
    case WM_INITDIALOG:
        SendMessageW(dlg, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(
            LoadIconW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(IDI_APP))));
        CheckDlgButton(dlg, IDC_CHK_CAPS, g_settings.capsAsBackspace ? BST_CHECKED : BST_UNCHECKED);
        CheckDlgButton(dlg, IDC_CHK_LCMD, g_settings.leftCmdAsCtrl ? BST_CHECKED : BST_UNCHECKED);
        CheckDlgButton(dlg, IDC_CHK_ROPT, g_settings.rightOptLayer ? BST_CHECKED : BST_UNCHECKED);
        CheckDlgButton(dlg, IDC_CHK_CTRLSPACE, g_settings.ctrlSpaceStart ? BST_CHECKED : BST_UNCHECKED);
        CheckDlgButton(dlg, IDC_CHK_CMDSPACE, g_settings.cmdSpaceLang ? BST_CHECKED : BST_UNCHECKED);
        CheckDlgButton(dlg, IDC_CHK_AUTOSTART, IsAutoStartEnabled() ? BST_CHECKED : BST_UNCHECKED);
        return TRUE;

    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case IDOK:
            g_settings.capsAsBackspace = IsDlgButtonChecked(dlg, IDC_CHK_CAPS) == BST_CHECKED;
            g_settings.leftCmdAsCtrl = IsDlgButtonChecked(dlg, IDC_CHK_LCMD) == BST_CHECKED;
            g_settings.rightOptLayer = IsDlgButtonChecked(dlg, IDC_CHK_ROPT) == BST_CHECKED;
            g_settings.ctrlSpaceStart = IsDlgButtonChecked(dlg, IDC_CHK_CTRLSPACE) == BST_CHECKED;
            g_settings.cmdSpaceLang = IsDlgButtonChecked(dlg, IDC_CHK_CMDSPACE) == BST_CHECKED;
            SaveSettings();
            SetAutoStart(IsDlgButtonChecked(dlg, IDC_CHK_AUTOSTART) == BST_CHECKED);
            EndDialog(dlg, IDOK);
            return TRUE;
        case IDCANCEL:
            EndDialog(dlg, IDCANCEL);
            return TRUE;
        }
        break;
    }
    return FALSE;
}

} // namespace

void LoadSettings()
{
    HKEY key;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kSettingsKey, 0, KEY_QUERY_VALUE, &key) != ERROR_SUCCESS)
        return;
    g_settings.capsAsBackspace = ReadBool(key, L"CapsAsBackspace", g_settings.capsAsBackspace);
    g_settings.leftCmdAsCtrl = ReadBool(key, L"LeftCmdAsCtrl", g_settings.leftCmdAsCtrl);
    g_settings.rightOptLayer = ReadBool(key, L"RightOptAsLayer", g_settings.rightOptLayer);
    g_settings.ctrlSpaceStart = ReadBool(key, L"CtrlSpaceStart", g_settings.ctrlSpaceStart);
    g_settings.cmdSpaceLang = ReadBool(key, L"CmdSpaceLang", g_settings.cmdSpaceLang);
    RegCloseKey(key);
}

void SaveSettings()
{
    HKEY key;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, kSettingsKey, 0, nullptr, 0,
                        KEY_SET_VALUE, nullptr, &key, nullptr) != ERROR_SUCCESS)
        return;
    WriteBool(key, L"CapsAsBackspace", g_settings.capsAsBackspace);
    WriteBool(key, L"LeftCmdAsCtrl", g_settings.leftCmdAsCtrl);
    WriteBool(key, L"RightOptAsLayer", g_settings.rightOptLayer);
    WriteBool(key, L"CtrlSpaceStart", g_settings.ctrlSpaceStart);
    WriteBool(key, L"CmdSpaceLang", g_settings.cmdSpaceLang);
    RegCloseKey(key);
}

// Autostart uses a Task Scheduler logon task ("MacKeys") rather than the
// classic Run key: Run-key apps are staggered by a minute or more at logon
// (and proved unreliable here), while a logon task fires within seconds.

bool IsAutoStartEnabled()
{
    STARTUPINFOW si = { sizeof(si) };
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION pi = {};
    wchar_t cmd[] = L"schtasks.exe /Query /TN MacKeys";
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
        wsprintfW(params, L"/Create /F /SC ONLOGON /RL LIMITED /TN MacKeys /TR \"\\\"%s\\\"\"",
                  exePath);
    } else {
        wsprintfW(params, L"/Delete /F /TN MacKeys");
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

void EnsureFirstRunAutoStart()
{
    DWORD configured = 0;
    DWORD size = sizeof(configured);
    if (RegGetValueW(HKEY_CURRENT_USER, kSettingsKey, L"AutostartConfigured",
                     RRF_RT_REG_DWORD, nullptr, &configured, &size) == ERROR_SUCCESS)
        return;
    SetAutoStart(true);
    configured = 1;
    RegSetKeyValueW(HKEY_CURRENT_USER, kSettingsKey, L"AutostartConfigured",
                    REG_DWORD, &configured, sizeof(configured));
}

void ShowSettingsDialog(HWND owner, HINSTANCE instance)
{
    DialogBoxW(instance, MAKEINTRESOURCEW(IDD_SETTINGS), owner, SettingsDlgProc);
}
