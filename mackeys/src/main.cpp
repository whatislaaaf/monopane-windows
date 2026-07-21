// MacKeys — replicates a Karabiner-Elements setup on Windows.
//
// Mappings (all keyboards, physical keys only — injected input passes through;
// the first three are toggleable in Settings):
//   Caps Lock            -> Backspace
//   Left cmd  (F23 via Scancode Map, or Left Win) -> Left Ctrl (Mac copy/paste)
//   Right option (Right Win via Scancode Map)     -> real Windows key
//   Right cmd (F24 via Scancode Map, or Right Win)
//                        -> nav layer, swallowed when tapped alone:
//        h -> Home    ; -> End
//        j -> Left    k -> Down    i -> Up    l -> Right
//        u -> Ctrl+Win+Left  (previous virtual desktop)
//        o -> Ctrl+Win+Right (next virtual desktop)
//   Other held modifiers (Shift, Ctrl, ...) pass through, so
//   layer+Shift+j selects text leftwards, matching Karabiner's optional-any.

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>

#include "settings.h"
#include "../res/resource.h"

namespace {

constexpr ULONG_PTR kInjectMarker = 0x4D4B5953; // "MKYS"
constexpr BYTE kSwallowUpOnly = 0xFF;           // keyup consumed, nothing sent

constexpr UINT kTrayMessage = WM_APP + 1;
constexpr UINT kTrayId = 1;
constexpr UINT kCmdSettings = 1;
constexpr UINT kCmdPause = 2;
constexpr UINT kCmdExit = 3;

const wchar_t kWindowClass[] = L"MacKeysHiddenWindow";

HINSTANCE g_instance = nullptr;
HHOOK g_hook = nullptr;
NOTIFYICONDATAW g_nid = {};
UINT g_taskbarCreatedMsg = 0;
bool g_paused = false;
bool g_settingsOpen = false;
bool g_rwinDown = false;
bool g_raltDown = false;
bool g_lwinDown = false;      // physical left cmd (remapped to Ctrl) is held
bool g_altChordUsed = false;  // an alt-masked chord ran; mask the coming alt-up
// Per physical vk: mapped vk we sent on keydown, so the matching keyup is
// translated even if the layer key was released first.
BYTE g_translated[256] = {};

bool IsExtendedVk(WORD vk) {
    switch (vk) {
    case VK_LEFT: case VK_RIGHT: case VK_UP: case VK_DOWN:
    case VK_HOME: case VK_END: case VK_LWIN: case VK_RWIN:
        return true;
    }
    return false;
}

void FillKeyInput(INPUT& in, WORD vk, bool down) {
    in.type = INPUT_KEYBOARD;
    in.ki.wVk = vk;
    in.ki.wScan = static_cast<WORD>(MapVirtualKeyW(vk, MAPVK_VK_TO_VSC));
    in.ki.dwFlags = down ? 0 : KEYEVENTF_KEYUP;
    if (IsExtendedVk(vk))
        in.ki.dwFlags |= KEYEVENTF_EXTENDEDKEY;
    in.ki.dwExtraInfo = kInjectMarker;
}

void SendVk(WORD vk, bool down) {
    INPUT in = {};
    FillKeyInput(in, vk, down);
    SendInput(1, &in, sizeof(in));
}

// One atomic Ctrl+Win+Arrow chord for virtual desktop switching.
void SendDesktopSwitch(WORD arrowVk) {
    const WORD sequence[] = { VK_LCONTROL, VK_LWIN, arrowVk };
    INPUT in[6] = {};
    for (int i = 0; i < 3; ++i)
        FillKeyInput(in[i], sequence[i], true);
    for (int i = 0; i < 3; ++i)
        FillKeyInput(in[3 + i], sequence[2 - i], false);
    SendInput(6, in, sizeof(INPUT));
}

bool AltHeld() {
    if (GetAsyncKeyState(VK_LMENU) & 0x8000)
        return true;
    // Right option counts as plain Alt only when it is not a layer key.
    if (!g_settings.rightOptLayer && (GetAsyncKeyState(VK_RMENU) & 0x8000))
        return true;
    return false;
}

// Send one tap of `vk`, optionally wrapped in Ctrl, while temporarily lifting
// held Alt (and/or the Ctrl coming from the left-cmd remap) so the app on the
// receiving end sees exactly the Mac-style editing chord and nothing else.
void SendNavChord(WORD vk, bool withCtrl, bool maskAlt, bool maskCtrl) {
    INPUT in[10];
    int n = 0;
    auto add = [&](WORD key, bool down) { FillKeyInput(in[n++], key, down); };
    const bool altL = maskAlt && (GetAsyncKeyState(VK_LMENU) & 0x8000);
    const bool altR = maskAlt && (GetAsyncKeyState(VK_RMENU) & 0x8000);
    // Ctrl goes down before any alt-up so the release cannot focus a menu bar.
    if (withCtrl) add(VK_LCONTROL, true);
    if (altL) add(VK_LMENU, false);
    if (altR) add(VK_RMENU, false);
    if (maskCtrl) add(VK_LCONTROL, false);
    add(vk, true);
    add(vk, false);
    if (maskCtrl) add(VK_LCONTROL, true);
    if (altR) add(VK_RMENU, true);
    if (altL) add(VK_LMENU, true);
    if (withCtrl) add(VK_LCONTROL, false);
    SendInput(n, in, sizeof(INPUT));
    if (altL || altR)
        g_altChordUsed = true;
}

// Tap the Win key to toggle the Start menu, temporarily lifting held Ctrl
// (Ctrl+Win is a no-op, so the chord must arrive as a bare Win tap). With
// `withSpace`, sends Win+Space instead — the input-language switcher.
void SendWinTap(bool withSpace) {
    INPUT in[8];
    int n = 0;
    auto add = [&](WORD key, bool down) { FillKeyInput(in[n++], key, down); };
    const bool lc = (GetAsyncKeyState(VK_LCONTROL) & 0x8000) != 0;
    const bool rc = (GetAsyncKeyState(VK_RCONTROL) & 0x8000) != 0;
    if (lc) add(VK_LCONTROL, false);
    if (rc) add(VK_RCONTROL, false);
    add(VK_LWIN, true);
    if (withSpace) {
        add(VK_SPACE, true);
        add(VK_SPACE, false);
    }
    add(VK_LWIN, false);
    if (rc) add(VK_RCONTROL, true);
    if (lc) add(VK_LCONTROL, true);
    SendInput(n, in, sizeof(INPUT));
}

// After an alt-masked chord, the re-pressed Alt would focus the menu bar on
// its physical release; a Ctrl tap right before the alt-up defuses that.
void MaskAltUpIfNeeded() {
    if (!g_altChordUsed)
        return;
    g_altChordUsed = false;
    SendVk(VK_LCONTROL, true);
    SendVk(VK_LCONTROL, false);
}

WORD LayerTarget(DWORD vk) {
    switch (vk) {
    case 'H': return VK_HOME;
    case 'J': return VK_LEFT;
    case 'K': return VK_DOWN;
    case 'I': return VK_UP;
    case 'L': return VK_RIGHT;
    case VK_OEM_1: return VK_END; // ; on ANSI/ISO layouts
    }
    return 0;
}

void ReleaseTranslatedKeys() {
    for (int vk = 0; vk < 256; ++vk) {
        if (g_translated[vk]) {
            if (g_translated[vk] != kSwallowUpOnly)
                SendVk(g_translated[vk], false);
            g_translated[vk] = 0;
        }
    }
    g_rwinDown = false;
    g_raltDown = false;
    if (g_lwinDown) {
        SendVk(VK_LCONTROL, false); // release the Ctrl held by the cmd remap
        g_lwinDown = false;
    }
}

LRESULT CALLBACK KeyboardProc(int code, WPARAM wParam, LPARAM lParam) {
    if (code != HC_ACTION)
        return CallNextHookEx(g_hook, code, wParam, lParam);

    const auto* k = reinterpret_cast<const KBDLLHOOKSTRUCT*>(lParam);
    if ((k->flags & LLKHF_INJECTED) || g_paused)
        return CallNextHookEx(g_hook, code, wParam, lParam);

    const bool down = (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN);

    switch (k->vkCode) {
    case VK_CAPITAL:
        if (!g_settings.capsAsBackspace)
            break;
        SendVk(VK_BACK, down);
        return 1;
    case VK_LWIN:
    case VK_F23: // left cmd after the kernel Scancode Map remap (see README)
        if (!g_settings.leftCmdAsCtrl)
            break;
        g_lwinDown = down;
        SendVk(VK_LCONTROL, down);
        return 1;
    case VK_F24: // right cmd after the kernel Scancode Map remap (see README)
        g_rwinDown = down;
        return 1;
    case VK_RWIN: // right option after the remap (right cmd arrives as F24)
        if (!g_settings.rightOptLayer)
            break; // acts as a real Windows key
        g_raltDown = down;
        return 1;
    case VK_RMENU: // right option before the remap / on other keyboards
        if (!g_settings.rightOptLayer) {
            if (!down)
                MaskAltUpIfNeeded();
            break;
        }
        g_raltDown = down;
        return 1;
    case VK_LMENU:
        if (!down)
            MaskAltUpIfNeeded();
        break;
    case VK_LCONTROL:
        // AltGr layouts emit a fake LCtrl (scan 0x21D) with every right alt;
        // drop it so it can't leak into layer navigation.
        if (g_settings.rightOptLayer && k->scanCode == 0x21D)
            return 1;
        break;
    case VK_SPACE:
        if (!down)
            break; // the keyup is swallowed via g_translated below
        if (g_lwinDown && g_settings.cmdSpaceLang) {
            // Left cmd+Space = input-language switcher (Win+Space), like Mac.
            if (!g_translated[VK_SPACE]) // ignore autorepeat while held
                SendWinTap(true);
            g_translated[VK_SPACE] = kSwallowUpOnly;
            return 1;
        }
        if (g_settings.ctrlSpaceStart && (GetAsyncKeyState(VK_CONTROL) & 0x8000)) {
            if (!g_translated[VK_SPACE])
                SendWinTap(false);
            g_translated[VK_SPACE] = kSwallowUpOnly;
            return 1;
        }
        break;
    }

    if (!down) {
        const BYTE sent = g_translated[k->vkCode & 0xFF];
        if (sent) {
            g_translated[k->vkCode & 0xFF] = 0;
            if (sent != kSwallowUpOnly)
                SendVk(sent, false);
            return 1;
        }
    } else if (g_rwinDown || g_raltDown) {
        if (const WORD target = LayerTarget(k->vkCode)) {
            const bool horizontal = target == VK_LEFT || target == VK_RIGHT;
            const bool vertical = target == VK_UP || target == VK_DOWN;
            if ((horizontal || vertical) && g_lwinDown) {
                // Mac cmd+arrow: j/l = line start/end, i/k = document
                // start/end. The remapped Ctrl stays down for i/k (making
                // Ctrl+Home/End) but is masked for j/l.
                const WORD dest =
                    (target == VK_LEFT || target == VK_UP) ? VK_HOME : VK_END;
                SendNavChord(dest, false, false, horizontal);
                g_translated[k->vkCode & 0xFF] = kSwallowUpOnly;
                return 1;
            }
            if ((horizontal || vertical) && AltHeld()) {
                // Mac option+arrow: word jump (j/l), paragraph-ish (i/k) —
                // Windows spells both as Ctrl+arrow.
                SendNavChord(target, true, true, false);
                g_translated[k->vkCode & 0xFF] = kSwallowUpOnly;
                return 1;
            }
            g_translated[k->vkCode & 0xFF] = static_cast<BYTE>(target);
            SendVk(target, true);
            return 1;
        }
        if (k->vkCode == 'U' || k->vkCode == 'O') {
            g_translated[k->vkCode & 0xFF] = kSwallowUpOnly;
            SendDesktopSwitch(k->vkCode == 'U' ? VK_LEFT : VK_RIGHT);
            return 1;
        }
    }

    return CallNextHookEx(g_hook, code, wParam, lParam);
}

// ------------------------------------------------------------------- tray

void UpdateTrayTooltip(HWND hwnd) {
    lstrcpyW(g_nid.szTip, g_paused ? L"MacKeys (paused)" : L"MacKeys — Mac-style keys active");
    g_nid.hWnd = hwnd;
    Shell_NotifyIconW(NIM_MODIFY, &g_nid);
}

void OpenSettings(HWND hwnd) {
    if (g_settingsOpen)
        return;
    g_settingsOpen = true;
    ShowSettingsDialog(hwnd, g_instance);
    g_settingsOpen = false;
}

void ShowTrayMenu(HWND hwnd) {
    HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING, kCmdSettings, L"Settings…");
    AppendMenuW(menu, MF_STRING | (g_paused ? MF_CHECKED : 0), kCmdPause, L"Pause remapping");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, kCmdExit, L"Exit");

    POINT pt;
    GetCursorPos(&pt);
    SetForegroundWindow(hwnd);
    TrackPopupMenu(menu, TPM_RIGHTBUTTON, pt.x, pt.y, 0, hwnd, nullptr);
    PostMessageW(hwnd, WM_NULL, 0, 0);
    DestroyMenu(menu);
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    // Explorer (re)started — the tray was rebuilt, so re-add our icon. Also
    // covers autostart racing ahead of the taskbar right after logon.
    if (msg == g_taskbarCreatedMsg && g_taskbarCreatedMsg != 0) {
        g_nid.hWnd = hwnd;
        Shell_NotifyIconW(NIM_ADD, &g_nid);
        UpdateTrayTooltip(hwnd);
        return 0;
    }
    switch (msg) {
    case kTrayMessage:
        switch (LOWORD(lParam)) {
        case WM_RBUTTONUP:
        case WM_CONTEXTMENU:
            ShowTrayMenu(hwnd);
            break;
        case WM_LBUTTONDBLCLK:
            OpenSettings(hwnd);
            break;
        }
        return 0;
    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case kCmdSettings:
            OpenSettings(hwnd);
            break;
        case kCmdPause:
            g_paused = !g_paused;
            if (g_paused)
                ReleaseTranslatedKeys();
            UpdateTrayTooltip(hwnd);
            break;
        case kCmdExit:
            DestroyWindow(hwnd);
            break;
        }
        return 0;
    case WM_DESTROY:
        Shell_NotifyIconW(NIM_DELETE, &g_nid);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

} // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    g_instance = instance;

    CreateMutexW(nullptr, TRUE, L"Local\\MacKeysSingleInstance");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        MessageBoxW(nullptr, L"MacKeys is already running (check the tray).",
                    L"MacKeys", MB_OK | MB_ICONINFORMATION);
        return 0;
    }

    LoadSettings();
    g_taskbarCreatedMsg = RegisterWindowMessageW(L"TaskbarCreated");

    WNDCLASSW wc = {};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = instance;
    wc.lpszClassName = kWindowClass;
    wc.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(IDI_APP));
    RegisterClassW(&wc);

    // A normal (never shown) window rather than HWND_MESSAGE: message-only
    // windows don't receive the TaskbarCreated broadcast.
    HWND hwnd = CreateWindowExW(WS_EX_TOOLWINDOW, kWindowClass, L"MacKeys",
                                WS_POPUP, 0, 0, 0, 0,
                                nullptr, nullptr, instance, nullptr);
    if (!hwnd)
        return 1;

    g_nid.cbSize = sizeof(g_nid);
    g_nid.hWnd = hwnd;
    g_nid.uID = kTrayId;
    g_nid.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    g_nid.uCallbackMessage = kTrayMessage;
    g_nid.hIcon = static_cast<HICON>(LoadImageW(
        instance, MAKEINTRESOURCEW(IDI_APP), IMAGE_ICON,
        GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), 0));
    lstrcpyW(g_nid.szTip, L"MacKeys — Mac-style keys active");
    Shell_NotifyIconW(NIM_ADD, &g_nid);

    EnsureFirstRunAutoStart();

    g_hook = SetWindowsHookExW(WH_KEYBOARD_LL, KeyboardProc, instance, 0);
    if (!g_hook) {
        Shell_NotifyIconW(NIM_DELETE, &g_nid);
        MessageBoxW(nullptr, L"Failed to install the keyboard hook.", L"MacKeys",
                    MB_OK | MB_ICONERROR);
        return 1;
    }

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    UnhookWindowsHookEx(g_hook);
    ReleaseTranslatedKeys();
    return 0;
}
