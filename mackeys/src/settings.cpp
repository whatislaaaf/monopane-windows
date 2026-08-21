#include "settings.h"

#include <commctrl.h>

#include <string>

#include "../res/resource.h"
#include "config.h"
#include "keyboard.h"
#include "scancodemap.h"

namespace {

constexpr wchar_t kSettingsKey[] = L"Software\\MacKeys";
constexpr wchar_t kRunKey[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
constexpr wchar_t kRunValueName[] = L"MacKeys";

// The dialog edits this copy; g_config only changes on OK.
struct DlgState {
    Config work;
    HWND tabKeys = nullptr;
    HWND tabOptions = nullptr;
    // Chord capture in flight: which entry, and whether we are collecting the
    // trigger (0) or the chord it should send (1).
    int chordIndex = -1;
    int chordStage = 0;
    bool chordIsNew = false;
};

DlgState* HostState(HWND dlg)
{
    return reinterpret_cast<DlgState*>(GetWindowLongPtrW(dlg, DWLP_USER));
}

// ------------------------------------------------------------- keys tab

INT_PTR CALLBACK KeysTabProc(HWND dlg, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg) {
    case WM_INITDIALOG: {
        DlgState* st = reinterpret_cast<DlgState*>(lParam);
        SetWindowLongPtrW(dlg, DWLP_USER, reinterpret_cast<LONG_PTR>(st));
        CheckRadioButton(dlg, IDC_RAD_BASE, IDC_RAD_NAV, IDC_RAD_BASE);
        SendDlgItemMessageW(dlg, IDC_KEYBOARD, KBM_SETCONFIG,
                            reinterpret_cast<WPARAM>(&st->work), 0);
        SetDlgItemTextW(dlg, IDC_TXT_STATUS,
                        KeyboardStatusText(GetDlgItem(dlg, IDC_KEYBOARD)).c_str());
        return TRUE;
    }
    case KBM_CHANGED:
        SetDlgItemTextW(dlg, IDC_TXT_STATUS,
                        KeyboardStatusText(GetDlgItem(dlg, IDC_KEYBOARD)).c_str());
        return TRUE;

    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case IDC_RAD_BASE:
        case IDC_RAD_NAV:
            SendDlgItemMessageW(dlg, IDC_KEYBOARD, KBM_SETVIEW,
                                LOWORD(wParam) == IDC_RAD_NAV ? 1 : 0, 0);
            SetDlgItemTextW(dlg, IDC_TXT_STATUS,
                            KeyboardStatusText(GetDlgItem(dlg, IDC_KEYBOARD)).c_str());
            return TRUE;
        }
        break;
    }
    return FALSE;
}

// ---------------------------------------------------------- options tab

constexpr UINT kMenuChordStart = 1;
constexpr UINT kMenuChordLang = 2;
constexpr UINT kMenuChordSend = 3;

void RefreshChordList(HWND dlg, DlgState* st)
{
    HWND list = GetDlgItem(dlg, IDC_LST_CHORDS);
    const LRESULT keep = SendMessageW(list, LB_GETCURSEL, 0, 0);
    SendMessageW(list, LB_RESETCONTENT, 0, 0);
    for (const ChordBinding& c : st->work.chords)
        SendMessageW(list, LB_ADDSTRING, 0,
                     reinterpret_cast<LPARAM>(DescribeChordBinding(c).c_str()));
    if (keep >= 0 && keep < static_cast<LRESULT>(st->work.chords.size()))
        SendMessageW(list, LB_SETCURSEL, static_cast<WPARAM>(keep), 0);

    const bool busy = st->chordIndex >= 0;
    EnableWindow(GetDlgItem(dlg, IDC_BTN_CHORD_ADD), !busy);
    EnableWindow(GetDlgItem(dlg, IDC_BTN_CHORD_SET), !busy);
    EnableWindow(GetDlgItem(dlg, IDC_BTN_CHORD_DEL), !busy);
    EnableWindow(list, !busy);
}

void SetChordHint(HWND dlg, const wchar_t* text)
{
    SetDlgItemTextW(dlg, IDC_TXT_CHORD_HINT, text);
}

void EndChordCapture(HWND dlg, DlgState* st)
{
    keychord::CancelChordCapture();
    st->chordIndex = -1;
    st->chordStage = 0;
    st->chordIsNew = false;
    RefreshChordList(dlg, st);
}

void StartChordCapture(HWND dlg, DlgState* st, int index, int stage, bool isNew)
{
    st->chordIndex = index;
    st->chordStage = stage;
    st->chordIsNew = isNew;
    RefreshChordList(dlg, st);
    SetChordHint(dlg, stage == 0
        ? L"Hold the modifiers and press the trigger key, then let go. "
          L"Left and right are told apart.  (Esc cancels)"
        : L"Now press the chord it should send, then let go.  (Esc cancels)");
    if (!keychord::BeginChordCapture(dlg))
        EndChordCapture(dlg, st);
}

void UpdateScancodeMapText(HWND dlg, const Config& cfg)
{
    const ScancodeMapPlan plan = BuildScancodeMap(cfg);
    std::wstring text;

    if (plan.blob.empty()) {
        text = L"Not needed by this configuration.";
        if (ForeignScancodeMapPresent(plan))
            text += L" One is installed, though — removing it needs admin rights "
                    L"and a reboot.";
    } else {
        text = L"Needed, because Windows handles Win+L below keyboard hooks:\r\n";
        for (const std::wstring& n : plan.notes)
            text += L"    " + n + L"\r\n";
        text += ScancodeMapIsCurrent(plan) ? L"Installed and up to date."
                                           : L"Not installed yet — MacKeys will offer to write it on OK.";
    }
    if (plan.overflow)
        text += L"\r\nToo many keys need diverting; some were dropped.";

    SetDlgItemTextW(dlg, IDC_TXT_SCMAP, text.c_str());
}

INT_PTR CALLBACK OptionsTabProc(HWND dlg, UINT msg, WPARAM wParam, LPARAM lParam)
{
    DlgState* st = reinterpret_cast<DlgState*>(GetWindowLongPtrW(dlg, DWLP_USER));

    switch (msg) {
    case WM_INITDIALOG: {
        st = reinterpret_cast<DlgState*>(lParam);
        SetWindowLongPtrW(dlg, DWLP_USER, reinterpret_cast<LONG_PTR>(st));

        CheckDlgButton(dlg, IDC_CHK_AUTOSTART,
                       IsAutoStartEnabled() ? BST_CHECKED : BST_UNCHECKED);
        RefreshChordList(dlg, st);
        SetChordHint(dlg, L"Hotkeys match exactly: every listed key must be held and "
                          L"nothing else, so left Ctrl is distinct from right.");

        SendDlgItemMessageW(dlg, IDC_CMB_LAYOUT, CB_ADDSTRING, 0,
                            reinterpret_cast<LPARAM>(L"Detect"));
        SendDlgItemMessageW(dlg, IDC_CMB_LAYOUT, CB_ADDSTRING, 0,
                            reinterpret_cast<LPARAM>(L"ANSI"));
        SendDlgItemMessageW(dlg, IDC_CMB_LAYOUT, CB_ADDSTRING, 0,
                            reinterpret_cast<LPARAM>(L"ISO"));
        SendDlgItemMessageW(dlg, IDC_CMB_LAYOUT, CB_SETCURSEL,
                            static_cast<WPARAM>(st->work.layout), 0);

        UpdateScancodeMapText(dlg, st->work);
        return TRUE;
    }

    case keychord::WM_CHORD_CAPTURED: {
        if (!st || st->chordIndex < 0 ||
            st->chordIndex >= static_cast<int>(st->work.chords.size()))
            return TRUE;
        ChordBinding& binding = st->work.chords[st->chordIndex];
        if (st->chordStage == 0) {
            binding.from = keychord::CapturedChord();
            if (binding.action == ChordAction::SendChord) {
                // A send needs both halves; go straight on to the second.
                StartChordCapture(dlg, st, st->chordIndex, 1, st->chordIsNew);
                return TRUE;
            }
        } else {
            binding.to = keychord::CapturedChord();
        }
        EndChordCapture(dlg, st);
        SetChordHint(dlg, L"Captured.");
        return TRUE;
    }

    case keychord::WM_CHORD_CANCELLED:
        if (!st)
            return TRUE;
        // A half-built entry would otherwise linger with no keys attached.
        if (st->chordIsNew && st->chordIndex >= 0 &&
            st->chordIndex < static_cast<int>(st->work.chords.size()))
            st->work.chords.erase(st->work.chords.begin() + st->chordIndex);
        EndChordCapture(dlg, st);
        SetChordHint(dlg, L"Cancelled.");
        return TRUE;

    // The only timer on this dialog is the chord-capture timeout, armed by
    // BeginChordCapture so an armed picker can never swallow the keyboard
    // indefinitely.
    case WM_TIMER:
        if (st && st->chordIndex >= 0)
            PostMessageW(dlg, keychord::WM_CHORD_CANCELLED, 0, 0);
        return TRUE;

    case WM_COMMAND:
        if (!st)
            break;
        switch (LOWORD(wParam)) {
        case IDC_BTN_CHORD_ADD: {
            if (st->work.chords.size() >= kMaxChords) {
                SetChordHint(dlg, L"That is as many hotkeys as MacKeys holds.");
                return TRUE;
            }
            HMENU menu = CreatePopupMenu();
            AppendMenuW(menu, MF_STRING, kMenuChordStart, L"Open the Start menu");
            AppendMenuW(menu, MF_STRING, kMenuChordLang, L"Switch input language");
            AppendMenuW(menu, MF_STRING, kMenuChordSend, L"Send another chord\x2026");
            RECT rc;
            GetWindowRect(GetDlgItem(dlg, IDC_BTN_CHORD_ADD), &rc);
            const int cmd = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_NONOTIFY,
                                           rc.left, rc.bottom, 0, dlg, nullptr);
            DestroyMenu(menu);
            if (!cmd)
                return TRUE;
            ChordBinding binding;
            binding.action = cmd == kMenuChordStart  ? ChordAction::StartMenu
                             : cmd == kMenuChordLang ? ChordAction::InputLanguage
                                                     : ChordAction::SendChord;
            st->work.chords.push_back(binding);
            StartChordCapture(dlg, st, static_cast<int>(st->work.chords.size()) - 1, 0, true);
            return TRUE;
        }
        case IDC_BTN_CHORD_SET: {
            const LRESULT sel = SendDlgItemMessageW(dlg, IDC_LST_CHORDS, LB_GETCURSEL, 0, 0);
            if (sel < 0 || sel >= static_cast<LRESULT>(st->work.chords.size())) {
                SetChordHint(dlg, L"Select a hotkey first.");
                return TRUE;
            }
            StartChordCapture(dlg, st, static_cast<int>(sel), 0, false);
            return TRUE;
        }
        case IDC_BTN_CHORD_DEL: {
            const LRESULT sel = SendDlgItemMessageW(dlg, IDC_LST_CHORDS, LB_GETCURSEL, 0, 0);
            if (sel < 0 || sel >= static_cast<LRESULT>(st->work.chords.size())) {
                SetChordHint(dlg, L"Select a hotkey first.");
                return TRUE;
            }
            st->work.chords.erase(st->work.chords.begin() + sel);
            RefreshChordList(dlg, st);
            return TRUE;
        }
        case IDC_CMB_LAYOUT:
            if (HIWORD(wParam) == CBN_SELCHANGE) {
                const LRESULT sel = SendDlgItemMessageW(dlg, IDC_CMB_LAYOUT, CB_GETCURSEL, 0, 0);
                st->work.layout = static_cast<BoardLayout>(sel < 0 ? 0 : sel);
                // The picker draws whichever layout is selected.
                InvalidateRect(GetDlgItem(st->tabKeys, IDC_KEYBOARD), nullptr, FALSE);
                UpdateScancodeMapText(dlg, st->work);
            }
            return TRUE;
        case IDC_BTN_RESETDEFAULTS:
            if (MessageBoxW(dlg,
                            L"Reset every key binding to the defaults?\n\n"
                            L"Caps Lock sends Backspace, right Alt holds the nav layer, "
                            L"ijkl/h/; navigate and u/o switch desktops.",
                            L"MacKeys", MB_YESNO | MB_ICONQUESTION) == IDYES) {
                const BoardLayout keepLayout = st->work.layout;
                ResetToDefaults(st->work);
                st->work.layout = keepLayout;
                RefreshChordList(dlg, st);
                InvalidateRect(GetDlgItem(st->tabKeys, IDC_KEYBOARD), nullptr, FALSE);
                UpdateScancodeMapText(dlg, st->work);
            }
            return TRUE;
        case IDC_BTN_SCMAP_RESET: {
            std::vector<BYTE> current;
            if (!ReadCurrentScancodeMap(current)) {
                MessageBoxW(dlg, L"No Scancode Map is installed.", L"MacKeys",
                            MB_OK | MB_ICONINFORMATION);
                return TRUE;
            }
            if (MessageBoxW(dlg,
                            L"Remove the kernel Scancode Map?\n\n"
                            L"This restores stock behaviour for every key it remapped. "
                            L"Administrator rights are required and the change takes "
                            L"effect at the next reboot.",
                            L"MacKeys", MB_YESNO | MB_ICONWARNING) == IDYES) {
                if (ResetScancodeMap())
                    MessageBoxW(dlg, L"Removed. Reboot to take effect.", L"MacKeys",
                                MB_OK | MB_ICONINFORMATION);
                else
                    MessageBoxW(dlg, L"Could not remove it (the elevation prompt was "
                                     L"declined, or the write failed).",
                                L"MacKeys", MB_OK | MB_ICONERROR);
                UpdateScancodeMapText(dlg, st->work);
            }
            return TRUE;
        }
        }
        break;
    }
    return FALSE;
}

// ------------------------------------------------------------- host dialog

void PositionTabChild(HWND dlg, HWND child)
{
    HWND tab = GetDlgItem(dlg, IDC_TAB);
    RECT rc;
    GetWindowRect(tab, &rc);
    MapWindowPoints(nullptr, dlg, reinterpret_cast<POINT*>(&rc), 2);
    TabCtrl_AdjustRect(tab, FALSE, &rc);
    SetWindowPos(child, HWND_TOP, rc.left, rc.top, rc.right - rc.left, rc.bottom - rc.top,
                 SWP_NOACTIVATE);
}

void SelectTab(DlgState* st, int index)
{
    ShowWindow(st->tabKeys, index == 0 ? SW_SHOW : SW_HIDE);
    ShowWindow(st->tabOptions, index == 1 ? SW_SHOW : SW_HIDE);
    if (index == 1)
        UpdateScancodeMapText(st->tabOptions, st->work);
}

INT_PTR CALLBACK SettingsDlgProc(HWND dlg, UINT msg, WPARAM wParam, LPARAM lParam)
{
    DlgState* st = HostState(dlg);

    switch (msg) {
    case WM_INITDIALOG: {
        st = new DlgState();
        st->work = g_config;
        SetWindowLongPtrW(dlg, DWLP_USER, reinterpret_cast<LONG_PTR>(st));

        SendMessageW(dlg, WM_SETICON, ICON_SMALL,
                     reinterpret_cast<LPARAM>(LoadIconW(GetModuleHandleW(nullptr),
                                                        MAKEINTRESOURCEW(IDI_APP))));

        HWND tab = GetDlgItem(dlg, IDC_TAB);
        TCITEMW item = {};
        item.mask = TCIF_TEXT;
        item.pszText = const_cast<wchar_t*>(L"Key setup");
        TabCtrl_InsertItem(tab, 0, &item);
        item.pszText = const_cast<wchar_t*>(L"Options");
        TabCtrl_InsertItem(tab, 1, &item);

        HINSTANCE instance = reinterpret_cast<HINSTANCE>(
            GetWindowLongPtrW(dlg, GWLP_HINSTANCE));
        st->tabKeys = CreateDialogParamW(instance, MAKEINTRESOURCEW(IDD_TAB_KEYS), dlg,
                                         KeysTabProc, reinterpret_cast<LPARAM>(st));
        st->tabOptions = CreateDialogParamW(instance, MAKEINTRESOURCEW(IDD_TAB_OPTIONS), dlg,
                                            OptionsTabProc, reinterpret_cast<LPARAM>(st));
        PositionTabChild(dlg, st->tabKeys);
        PositionTabChild(dlg, st->tabOptions);
        SelectTab(st, 0);
        return TRUE;
    }

    case WM_NOTIFY: {
        const NMHDR* hdr = reinterpret_cast<const NMHDR*>(lParam);
        if (st && hdr->idFrom == IDC_TAB && hdr->code == TCN_SELCHANGE)
            SelectTab(st, TabCtrl_GetCurSel(GetDlgItem(dlg, IDC_TAB)));
        return 0;
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
            g_config = st->work;
            SaveConfig();
            OnConfigChanged();
            SetAutoStart(IsDlgButtonChecked(st->tabOptions, IDC_CHK_AUTOSTART) == BST_CHECKED);
            SyncScancodeMap(dlg);
            EndDialog(dlg, IDOK);
            return TRUE;
        }

        case IDCANCEL:
            EndDialog(dlg, IDCANCEL);
            return TRUE;
        }
        break;

    case WM_DESTROY:
        // Closing while a capture is armed would leave the hook swallowing
        // every keystroke with nowhere to deliver it.
        keychord::CancelChordCapture();
        if (st) {
            delete st;
            SetWindowLongPtrW(dlg, DWLP_USER, 0);
        }
        return 0;
    }
    return FALSE;
}

} // namespace

bool SyncScancodeMap(HWND owner)
{
    const ScancodeMapPlan plan = BuildScancodeMap(g_config);
    if (ScancodeMapIsCurrent(plan))
        return false;

    std::wstring msg;
    if (plan.blob.empty()) {
        msg = L"A kernel Scancode Map is installed that this configuration no longer "
              L"needs.\n\nRemove it? Administrator rights are required, and the change "
              L"takes effect at the next reboot.";
    } else {
        msg = L"This configuration needs a kernel-level remap, because Windows handles "
              L"Win+L below keyboard hooks:\n\n";
        for (const std::wstring& n : plan.notes)
            msg += L"    " + n + L"\n";
        msg += L"\nWrite it now? Administrator rights are required, and the change takes "
               L"effect at the next reboot.";
    }

    if (MessageBoxW(owner, msg.c_str(), L"MacKeys", MB_YESNO | MB_ICONQUESTION) != IDYES)
        return false;

    if (!ApplyScancodeMap(plan.blob)) {
        MessageBoxW(owner,
                    L"The Scancode Map was not written (the elevation prompt was declined, "
                    L"or the write failed). Nothing was changed.",
                    L"MacKeys", MB_OK | MB_ICONERROR);
        return false;
    }
    MessageBoxW(owner, L"Written. Reboot for it to take effect.", L"MacKeys",
                MB_OK | MB_ICONINFORMATION);
    return true;
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
    if (IsAutoStartEnabled() == enable)
        return; // don't raise a UAC prompt for a no-op

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

    // Creating or deleting a logon-trigger task needs elevation, so this shows
    // a UAC prompt when the checkbox is toggled.
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
    INITCOMMONCONTROLSEX icc = { sizeof(icc), ICC_TAB_CLASSES | ICC_STANDARD_CLASSES };
    InitCommonControlsEx(&icc);
    DialogBoxW(instance, MAKEINTRESOURCEW(IDD_SETTINGS), owner, SettingsDlgProc);
}
