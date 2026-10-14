#include "keysettings.h"

#include <commctrl.h>

#include <string>
#include <vector>

#include "../res/resource.h"
#include "keyboard.h"
#include "lock.h"
#include "remap.h"
#include "scancodemap.h"

// The tabs edit this copy; g_config only changes on OK.
struct KeysEdit {
    Config work;
    HWND tabKeys = nullptr;
    HWND tabOptions = nullptr;
    // Chord capture in flight: which entry, and whether we are collecting the
    // trigger (0) or the chord it should send (1).
    int chordIndex = -1;
    int chordStage = 0;
    bool chordIsNew = false;
};

namespace {

// Set while the hotkey list has a capture armed. The hook reads it from its
// own thread to decide which stage feeds the capture; for a bool that is safe
// enough, and the worst case is one keystroke fed a stage late.
bool g_physicalCapture = false;

KeysEdit* State(HWND dlg)
{
    return reinterpret_cast<KeysEdit*>(GetWindowLongPtrW(dlg, DWLP_USER));
}

// ------------------------------------------------------------- keys tab

INT_PTR CALLBACK KeysTabProc(HWND dlg, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg) {
    case WM_INITDIALOG: {
        KeysEdit* st = reinterpret_cast<KeysEdit*>(lParam);
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

void RefreshChordList(HWND dlg, KeysEdit* st)
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

void EndChordCapture(HWND dlg, KeysEdit* st)
{
    keychord::CancelChordCapture();
    g_physicalCapture = false;
    st->chordIndex = -1;
    st->chordStage = 0;
    st->chordIsNew = false;
    RefreshChordList(dlg, st);
}

void StartChordCapture(HWND dlg, KeysEdit* st, int index, int stage, bool isNew)
{
    st->chordIndex = index;
    st->chordStage = stage;
    st->chordIsNew = isNew;
    RefreshChordList(dlg, st);
    SetChordHint(dlg, stage == 0
        ? L"Hold the modifiers and press the trigger key, then let go. "
          L"Left and right are told apart.  (Esc cancels)"
        : L"Now press the chord it should send, then let go.  (Esc cancels)");
    // Flagged before arming, so the first key cannot slip past to the wrong
    // stage of the hook.
    g_physicalCapture = true;
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
                                           : L"Not installed yet — Monopane will offer to write it on OK.";
    }
    if (plan.overflow)
        text += L"\r\nToo many keys need diverting; some were dropped.";

    SetDlgItemTextW(dlg, IDC_TXT_SCMAP, text.c_str());
}

INT_PTR CALLBACK OptionsTabProc(HWND dlg, UINT msg, WPARAM wParam, LPARAM lParam)
{
    KeysEdit* st = State(dlg);

    switch (msg) {
    case WM_INITDIALOG: {
        st = reinterpret_cast<KeysEdit*>(lParam);
        SetWindowLongPtrW(dlg, DWLP_USER, reinterpret_cast<LONG_PTR>(st));

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

        SendDlgItemMessageW(dlg, IDC_CMB_BOARD, CB_ADDSTRING, 0,
                            reinterpret_cast<LPARAM>(L"Full-size"));
        SendDlgItemMessageW(dlg, IDC_CMB_BOARD, CB_ADDSTRING, 0,
                            reinterpret_cast<LPARAM>(L"MX Keys Mini for Mac"));
        SendDlgItemMessageW(dlg, IDC_CMB_BOARD, CB_SETCURSEL,
                            static_cast<WPARAM>(st->work.board), 0);

        UpdateScancodeMapText(dlg, st->work);
        return TRUE;
    }

    // The Scancode Map text depends on what the picker on the other tab has
    // been doing, so it is refreshed each time this tab comes to the front.
    case WM_SHOWWINDOW:
        if (wParam && st)
            UpdateScancodeMapText(dlg, st->work);
        return FALSE;

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

    // The only timer on this tab is the chord-capture backstop, armed by
    // BeginChordCapture so an armed capture can never swallow the keyboard
    // indefinitely. A capture that completed inside the hook could not stop
    // it — the hook runs on its own thread, and KillTimer only works on the
    // window's — so a tick with nothing in flight is stopped here instead.
    case WM_TIMER:
        if (st && st->chordIndex >= 0)
            PostMessageW(dlg, keychord::WM_CHORD_CANCELLED, 0, 0);
        else
            KillTimer(dlg, wParam);
        return TRUE;

    case WM_COMMAND:
        if (!st)
            break;
        switch (LOWORD(wParam)) {
        case IDC_BTN_CHORD_ADD: {
            if (st->work.chords.size() >= kMaxChords) {
                SetChordHint(dlg, L"That is as many hotkeys as the list holds.");
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
        case IDC_CMB_BOARD:
            if (HIWORD(wParam) == CBN_SELCHANGE) {
                const LRESULT sel = SendDlgItemMessageW(dlg, IDC_CMB_BOARD, CB_GETCURSEL, 0, 0);
                st->work.board = static_cast<BoardShape>(sel < 0 ? 0 : sel);
                InvalidateRect(GetDlgItem(st->tabKeys, IDC_KEYBOARD), nullptr, FALSE);
            }
            return TRUE;
        case IDC_BTN_RESETDEFAULTS:
            if (MessageBoxW(dlg,
                            L"Reset every key binding to the defaults?\n\n"
                            L"Caps Lock sends Backspace, right Alt holds the nav layer, "
                            L"ijkl/h/; navigate and u/o switch desktops.",
                            L"Monopane", MB_YESNO | MB_ICONQUESTION) == IDYES) {
                const BoardLayout keepLayout = st->work.layout;
                const BoardShape keepBoard = st->work.board;
                ResetToDefaults(st->work);
                st->work.layout = keepLayout;
                st->work.board = keepBoard;
                RefreshChordList(dlg, st);
                InvalidateRect(GetDlgItem(st->tabKeys, IDC_KEYBOARD), nullptr, FALSE);
                UpdateScancodeMapText(dlg, st->work);
            }
            return TRUE;
        case IDC_BTN_SCMAP_RESET: {
            std::vector<BYTE> current;
            if (!ReadCurrentScancodeMap(current)) {
                MessageBoxW(dlg, L"No Scancode Map is installed.", L"Monopane",
                            MB_OK | MB_ICONINFORMATION);
                return TRUE;
            }
            if (MessageBoxW(dlg,
                            L"Remove the kernel Scancode Map?\n\n"
                            L"This restores stock behaviour for every key it remapped. "
                            L"Administrator rights are required and the change takes "
                            L"effect at the next reboot.",
                            L"Monopane", MB_YESNO | MB_ICONWARNING) == IDYES) {
                if (ResetScancodeMap())
                    MessageBoxW(dlg, L"Removed. Reboot to take effect.", L"Monopane",
                                MB_OK | MB_ICONINFORMATION);
                else
                    MessageBoxW(dlg, L"Could not remove it (the elevation prompt was "
                                     L"declined, or the write failed).",
                                L"Monopane", MB_OK | MB_ICONERROR);
                UpdateScancodeMapText(dlg, st->work);
            }
            return TRUE;
        }
        }
        break;
    }
    return FALSE;
}

} // namespace

KeysEdit* BeginKeysEdit()
{
    KeysEdit* edit = new KeysEdit();
    AcquireSRWLockShared(&g_settingsLock);
    edit->work = g_config;
    ReleaseSRWLockShared(&g_settingsLock);
    return edit;
}

HWND CreateKeysTab(HWND host, HINSTANCE instance, KeysEdit* edit)
{
    edit->tabKeys = CreateDialogParamW(instance, MAKEINTRESOURCEW(IDD_TAB_KEYS), host,
                                       KeysTabProc, reinterpret_cast<LPARAM>(edit));
    return edit->tabKeys;
}

HWND CreateKeyOptionsTab(HWND host, HINSTANCE instance, KeysEdit* edit)
{
    edit->tabOptions = CreateDialogParamW(instance, MAKEINTRESOURCEW(IDD_TAB_KEYOPTIONS), host,
                                          OptionsTabProc, reinterpret_cast<LPARAM>(edit));
    return edit->tabOptions;
}

void CommitKeysEdit(HWND owner, KeysEdit* edit)
{
    AcquireSRWLockExclusive(&g_settingsLock);
    g_config = edit->work;
    ReleaseSRWLockExclusive(&g_settingsLock);
    SaveConfig();
    remap::RequestReload();
    SyncScancodeMap(owner);
}

void EndKeysEdit(KeysEdit* edit)
{
    if (!edit)
        return;
    if (g_physicalCapture) {
        keychord::CancelChordCapture();
        g_physicalCapture = false;
    }
    delete edit;
}

bool KeysChordCaptureArmed()
{
    return g_physicalCapture && keychord::ChordCaptureActive();
}

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

    if (MessageBoxW(owner, msg.c_str(), L"Monopane", MB_YESNO | MB_ICONQUESTION) != IDYES)
        return false;

    if (!ApplyScancodeMap(plan.blob)) {
        MessageBoxW(owner,
                    L"The Scancode Map was not written (the elevation prompt was declined, "
                    L"or the write failed). Nothing was changed.",
                    L"Monopane", MB_OK | MB_ICONERROR);
        return false;
    }
    MessageBoxW(owner, L"Written. Reboot for it to take effect.", L"Monopane",
                MB_OK | MB_ICONINFORMATION);
    return true;
}
