// MacKeys — a configurable Mac-style key remapper for Windows.
//
// Every mapping lives in %APPDATA%\MacKeys\mackeys.ini and is edited on a
// picture of the keyboard in Settings. Two tables drive the hook: `base`
// applies always, `nav` applies only while a key bound to `layer` is held.
// Injected input passes through untouched, so other automation tools are not
// re-remapped.
//
// The only remapping that cannot happen here is a Win key doing a non-Windows
// job: Win+L is handled below keyboard hooks, so those keys are diverted onto
// spare scancodes by the kernel Scancode Map instead (see scancodemap.h) and
// aliased back to their physical identity on the way in.

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>

#include <vector>

#include "../res/resource.h"
#include "config.h"
#include "keyboard.h"
#include "keys.h"
#include "scancodemap.h"
#include "settings.h"

namespace {

constexpr ULONG_PTR kInjectMarker = 0x4D4B5953; // "MKYS"
constexpr KeyId kSwallowUpOnly = 0xFFFF;        // keyup consumed, nothing sent

constexpr UINT kTrayMessage = WM_APP + 1;
constexpr UINT kTrayId = 1;
constexpr UINT kCmdSettings = 1;
constexpr UINT kCmdPause = 2;
constexpr UINT kCmdResetMap = 3;
constexpr UINT kCmdExit = 4;

// Scancodes the chord helpers need by name.
constexpr KeyId kScLCtrl = 0x1D;
constexpr KeyId kScLAlt = 0x38;
constexpr KeyId kScRAlt = 0xE038;
constexpr KeyId kScSpace = 0x39;
constexpr KeyId kScLeft = 0xE04B;
constexpr KeyId kScRight = 0xE04D;
constexpr KeyId kScUp = 0xE048;
constexpr KeyId kScDown = 0xE050;
constexpr KeyId kScHome = 0xE047;
constexpr KeyId kScEnd = 0xE04F;

// The fake left-ctrl that AltGr layouts emit alongside every right alt.
constexpr DWORD kAltGrFillerScan = 0x21D;

const wchar_t kWindowClass[] = L"MacKeysHiddenWindow";

HINSTANCE g_instance = nullptr;
HHOOK g_hook = nullptr;
NOTIFYICONDATAW g_nid = {};
UINT g_taskbarCreatedMsg = 0;
bool g_paused = false;
bool g_settingsOpen = false;

// Live copies of the config, so the hook never touches a table being edited.
Bind g_base[kKeySlots];
Bind g_nav[kKeySlots];
// Keys the Scancode Map diverted, mapped back to their physical identity.
KeyId g_alias[kKeySlots];

std::vector<ChordBinding> g_chords;
// Which physical keys are down, for exact chord matching. Tracked here rather
// than read from GetAsyncKeyState because that reports the *virtual* keys —
// merging left and right Ctrl, and seeing the Ctrl a remap injects as though
// the user had pressed a real Ctrl key.
KeyOrigin g_physDown[kKeySlots] = {};
int g_physCount = 0;

int g_layerHeld = 0;         // number of held layer keys
bool g_ctrlSwapDown = false; // a key acting as Ctrl (Mac cmd) is held
bool g_altChordUsed = false; // an alt-masked chord ran; mask the coming alt-up
// Per physical key: what we sent on keydown, so the matching keyup is
// translated even if the layer key was released first.
KeyId g_translated[kKeySlots] = {};

void FillKeyInput(INPUT& in, KeyId id, bool down)
{
    in.type = INPUT_KEYBOARD;
    in.ki.wVk = 0;
    in.ki.wScan = static_cast<WORD>(id & 0xFF);
    in.ki.dwFlags = KEYEVENTF_SCANCODE | (down ? 0 : KEYEVENTF_KEYUP);
    if (KeyIsExtended(id))
        in.ki.dwFlags |= KEYEVENTF_EXTENDEDKEY;
    in.ki.dwExtraInfo = kInjectMarker;
}

void SendKey(KeyId id, bool down)
{
    INPUT in = {};
    FillKeyInput(in, id, down);
    SendInput(1, &in, sizeof(in));
}

// One atomic Ctrl+Win+Arrow chord for virtual desktop switching.
void SendDesktopSwitch(KeyId arrow)
{
    const KeyId sequence[] = { kScLCtrl, kScLeftWin, arrow };
    INPUT in[6] = {};
    for (int i = 0; i < 3; ++i)
        FillKeyInput(in[i], sequence[i], true);
    for (int i = 0; i < 3; ++i)
        FillKeyInput(in[3 + i], sequence[2 - i], false);
    SendInput(6, in, sizeof(INPUT));
}

bool AltHeld()
{
    if (GetAsyncKeyState(VK_LMENU) & 0x8000)
        return true;
    // Right alt only counts as plain Alt while it has no job of its own.
    if (g_base[KeySlot(kScRAlt)].action == Action::None &&
        (GetAsyncKeyState(VK_RMENU) & 0x8000))
        return true;
    return false;
}

// Send one tap of `id`, optionally wrapped in Ctrl, while temporarily lifting
// held Alt (and/or the Ctrl coming from the Mac-cmd remap) so the app on the
// receiving end sees exactly the Mac-style editing chord and nothing else.
void SendNavChord(KeyId id, bool withCtrl, bool maskAlt, bool maskCtrl)
{
    INPUT in[10];
    int n = 0;
    auto add = [&](KeyId key, bool down) { FillKeyInput(in[n++], key, down); };
    const bool altL = maskAlt && (GetAsyncKeyState(VK_LMENU) & 0x8000) != 0;
    const bool altR = maskAlt && (GetAsyncKeyState(VK_RMENU) & 0x8000) != 0;
    // Ctrl goes down before any alt-up so the release cannot focus a menu bar.
    if (withCtrl) add(kScLCtrl, true);
    if (altL) add(kScLAlt, false);
    if (altR) add(kScRAlt, false);
    if (maskCtrl) add(kScLCtrl, false);
    add(id, true);
    add(id, false);
    if (maskCtrl) add(kScLCtrl, true);
    if (altR) add(kScRAlt, true);
    if (altL) add(kScLAlt, true);
    if (withCtrl) add(kScLCtrl, false);
    SendInput(n, in, sizeof(INPUT));
    if (altL || altR)
        g_altChordUsed = true;
}

// What a held physical key currently has down as far as Windows is concerned,
// which is not the key itself once it has been remapped.
KeyId EffectiveDown(KeyId physical)
{
    const Bind& b = g_base[KeySlot(physical)];
    switch (b.action) {
    case Action::None:     return physical;
    case Action::Key:      return b.target;
    case Action::CtrlSwap: return kScLCtrl;
    default:               return kNoKey; // layer/win keys hold nothing
    }
}

// The keys a chord's own modifiers are holding down right now. Asking the
// bindings rather than GetAsyncKeyState is what makes this work whatever the
// modifier has been remapped into: the key labelled Ctrl may be holding Alt.
int HeldByChord(const KeyChord& from, KeyId* out, int max)
{
    int n = 0;
    for (uint8_t i = 0; i < from.modCount && n < max; ++i) {
        const KeyId eff = EffectiveDown(from.mods[i]);
        if (eff != kNoKey)
            out[n++] = eff;
    }
    return n;
}

// Tap the Win key to toggle the Start menu. Only a *bare* Win tap opens it —
// Win with any modifier still down is either a different shortcut or nothing —
// so whatever the chord's own modifiers are holding is lifted first and put
// back after. With `withSpace`, sends Win+Space instead: the input-language
// switcher.
void SendWinTap(bool withSpace, const KeyChord& from)
{
    INPUT in[16];
    int n = 0;
    auto add = [&](KeyId key, bool down) { FillKeyInput(in[n++], key, down); };

    KeyId lifted[kMaxChordMods];
    const int liftCount = HeldByChord(from, lifted, kMaxChordMods);

    for (int i = 0; i < liftCount; ++i) {
        add(lifted[i], false);
        if (lifted[i] == kScLAlt || lifted[i] == kScRAlt)
            g_altChordUsed = true;
    }
    add(kScLeftWin, true);
    if (withSpace) {
        add(kScSpace, true);
        add(kScSpace, false);
    }
    add(kScLeftWin, false);
    for (int i = liftCount; i > 0; --i)
        add(lifted[i - 1], true);
    SendInput(n, in, sizeof(INPUT));
}

// After an alt-masked chord, the re-pressed Alt would focus the menu bar on
// its physical release; a Ctrl tap right before the alt-up defuses that.
void MaskAltUpIfNeeded()
{
    if (!g_altChordUsed)
        return;
    g_altChordUsed = false;
    SendKey(kScLCtrl, true);
    SendKey(kScLCtrl, false);
}

void ReleaseTranslatedKeys()
{
    for (int slot = 0; slot < kKeySlots; ++slot) {
        if (g_translated[slot]) {
            if (g_translated[slot] != kSwallowUpOnly)
                SendKey(g_translated[slot], false);
            g_translated[slot] = 0;
        }
    }
    g_layerHeld = 0;
    for (int slot = 0; slot < kKeySlots; ++slot)
        g_physDown[slot] = KeyOrigin::Any;
    g_physCount = 0;
    if (g_ctrlSwapDown) {
        SendKey(kScLCtrl, false); // release the Ctrl held by the Mac-cmd remap
        g_ctrlSwapDown = false;
    }
}

bool IsHorizontalArrow(KeyId id) { return id == kScLeft || id == kScRight; }
bool IsVerticalArrow(KeyId id) { return id == kScUp || id == kScDown; }

// ------------------------------------------------------------------ chords

// Send `to` as a chord, first lifting whatever the trigger's own modifiers are
// holding down so the receiving app sees the output and nothing else.
void SendOutputChord(const KeyChord& from, const KeyChord& to)
{
    INPUT in[24];
    int n = 0;
    auto add = [&](KeyId key, bool down) { FillKeyInput(in[n++], key, down); };

    KeyId held[kMaxChordMods];
    const int heldCount = HeldByChord(from, held, kMaxChordMods);

    // A modifier the output wants anyway stays down rather than being lifted
    // and immediately re-pressed.
    KeyId lifted[kMaxChordMods];
    int liftCount = 0;
    for (int i = 0; i < heldCount; ++i) {
        bool wanted = false;
        for (uint8_t j = 0; j < to.modCount; ++j)
            wanted = wanted || to.mods[j] == held[i];
        if (!wanted)
            lifted[liftCount++] = held[i];
    }

    for (int i = 0; i < liftCount; ++i) {
        add(lifted[i], false);
        if (lifted[i] == kScLAlt || lifted[i] == kScRAlt)
            g_altChordUsed = true;
    }
    for (uint8_t i = 0; i < to.modCount; ++i)
        add(to.mods[i], true);
    add(to.trigger, true);
    add(to.trigger, false);
    for (int i = to.modCount; i > 0; --i)
        add(to.mods[i - 1], false);
    for (int i = liftCount; i > 0; --i)
        add(lifted[i - 1], true);
    SendInput(n, in, sizeof(INPUT));
}

void RunChord(const ChordBinding& binding)
{
    switch (binding.action) {
    case ChordAction::StartMenu:     SendWinTap(false, binding.from); break;
    case ChordAction::InputLanguage: SendWinTap(true, binding.from); break;
    case ChordAction::SendChord:     SendOutputChord(binding.from, binding.to); break;
    case ChordAction::None:          break;
    }
}

// Exact match: every listed modifier must be down and nothing else may be, so
// Left Ctrl + Space is distinct from Right Ctrl + Space and from
// Ctrl + Shift + Space. `g_physCount` already includes the trigger.
const ChordBinding* MatchChord(KeyId trigger)
{
    for (const ChordBinding& c : g_chords) {
        if (c.from.trigger != trigger)
            continue;
        if (ChordMatches(c.from, g_physDown, g_physCount, /*exact=*/true))
            return &c;
    }
    return nullptr;
}

LRESULT CALLBACK KeyboardProc(int code, WPARAM wParam, LPARAM lParam)
{
    if (code != HC_ACTION)
        return CallNextHookEx(g_hook, code, wParam, lParam);

    const auto* k = reinterpret_cast<const KBDLLHOOKSTRUCT*>(lParam);
    if (k->flags & LLKHF_INJECTED)
        return CallNextHookEx(g_hook, code, wParam, lParam);

    const bool down = (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN);

    // Both settings captures take raw keystrokes ahead of everything, including
    // the pause state, and swallow them so nothing else reacts. Injected input
    // was dropped above, so every key reaching here is a physical one.
    {
        const KeyId raw = MakeKeyId(k->scanCode, (k->flags & LLKHF_EXTENDED) != 0);
        if (CaptureActive() && ForwardCaptureKey(raw, down))
            return 1; // the keyboard picker, capturing one key
        if (keychord::ChordCaptureActive() &&
            keychord::FeedChordKey(raw, down, KeyOrigin::Physical))
            return 1; // the Hotkeys list, capturing a whole chord
    }

    if (g_paused)
        return CallNextHookEx(g_hook, code, wParam, lParam);

    // AltGr layouts emit a fake left ctrl with every right alt; it would
    // otherwise look exactly like a real one and leak into the layer.
    if (k->scanCode == kAltGrFillerScan) {
        if (g_base[KeySlot(kScRAlt)].action != Action::None)
            return 1;
        return CallNextHookEx(g_hook, code, wParam, lParam);
    }

    KeyId id = MakeKeyId(k->scanCode, (k->flags & LLKHF_EXTENDED) != 0);
    // Undo a kernel divert, so bindings stay keyed to the physical key the
    // user clicked on the picture rather than the spare it now arrives as.
    if (const KeyId original = g_alias[KeySlot(id)])
        id = original;

    const int slot = KeySlot(id);
    const Bind& base = g_base[slot];

    // Physical key state, kept before any early return so it can never drift.
    // Autorepeat must not double-count.
    if (down) {
        if (g_physDown[slot] == KeyOrigin::Any) {
            g_physDown[slot] = KeyOrigin::Physical;
            ++g_physCount;
        }
    } else if (g_physDown[slot] != KeyOrigin::Any) {
        g_physDown[slot] = KeyOrigin::Any;
        if (g_physCount > 0)
            --g_physCount;
    }

    // ------------------------------------------------------ modifier roles

    switch (base.action) {
    case Action::Layer:
        if (down) {
            if (!g_translated[slot]) {
                ++g_layerHeld;
                g_translated[slot] = kSwallowUpOnly;
            }
        } else if (g_translated[slot]) {
            g_translated[slot] = 0;
            if (g_layerHeld > 0)
                --g_layerHeld;
        }
        return 1; // swallowed, so a lone tap does nothing
    case Action::CtrlSwap:
        g_ctrlSwapDown = down;
        SendKey(kScLCtrl, down);
        return 1;
    case Action::WinKey:
        // The Scancode Map already turned this into a real Windows key; the
        // hook must keep its hands off it.
        return CallNextHookEx(g_hook, code, wParam, lParam);
    default:
        break;
    }

    // Alt releases need defusing — keyed on what the key is actually holding
    // down, not on its own identity, since the Alt may be coming from a remap
    // of some entirely different key.
    if (!down) {
        const KeyId eff = EffectiveDown(id);
        if (eff == kScLAlt || eff == kScRAlt)
            MaskAltUpIfNeeded();
    }

    // ------------------------------------------------------------- chords

    if (down) {
        if (const ChordBinding* chord = MatchChord(id)) {
            if (!g_translated[slot]) // ignore autorepeat while held
                RunChord(*chord);
            g_translated[slot] = kSwallowUpOnly;
            return 1;
        }
    }

    // ------------------------------------------------------- key movement

    if (!down) {
        const KeyId sent = g_translated[slot];
        if (sent) {
            g_translated[slot] = 0;
            if (sent != kSwallowUpOnly)
                SendKey(sent, false);
            return 1;
        }
        return CallNextHookEx(g_hook, code, wParam, lParam);
    }

    const Bind& active = g_layerHeld > 0 && g_nav[slot].action != Action::None ? g_nav[slot]
                                                                              : base;
    switch (active.action) {
    case Action::DesktopPrev:
    case Action::DesktopNext:
        g_translated[slot] = kSwallowUpOnly;
        SendDesktopSwitch(active.action == Action::DesktopPrev ? kScLeft : kScRight);
        return 1;

    case Action::Key: {
        const KeyId target = active.target;
        const bool horizontal = IsHorizontalArrow(target);
        const bool vertical = IsVerticalArrow(target);
        if ((horizontal || vertical) && g_ctrlSwapDown) {
            // Mac cmd+arrow: left/right = line start/end, up/down = document
            // start/end. The remapped Ctrl stays down for up/down (making
            // Ctrl+Home/End) but is masked for left/right.
            const KeyId dest = (target == kScLeft || target == kScUp) ? kScHome : kScEnd;
            SendNavChord(dest, false, false, horizontal);
            g_translated[slot] = kSwallowUpOnly;
            return 1;
        }
        if ((horizontal || vertical) && AltHeld()) {
            // Mac option+arrow: word jump (left/right), paragraph-ish
            // (up/down) — Windows spells both as Ctrl+arrow.
            SendNavChord(target, true, true, false);
            g_translated[slot] = kSwallowUpOnly;
            return 1;
        }
        g_translated[slot] = target;
        SendKey(target, true);
        return 1;
    }

    default:
        break;
    }

    return CallNextHookEx(g_hook, code, wParam, lParam);
}

// ------------------------------------------------------------------- tray

void UpdateTrayTooltip(HWND hwnd)
{
    lstrcpyW(g_nid.szTip, g_paused ? L"MacKeys (paused)" : L"MacKeys — Mac-style keys active");
    g_nid.hWnd = hwnd;
    g_nid.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    Shell_NotifyIconW(NIM_MODIFY, &g_nid);
}

void ShowBalloon(HWND hwnd, const wchar_t* title, const wchar_t* text)
{
    NOTIFYICONDATAW nid = g_nid;
    nid.hWnd = hwnd;
    nid.uFlags = NIF_INFO;
    nid.dwInfoFlags = NIIF_INFO;
    lstrcpynW(nid.szInfoTitle, title, ARRAYSIZE(nid.szInfoTitle));
    lstrcpynW(nid.szInfo, text, ARRAYSIZE(nid.szInfo));
    Shell_NotifyIconW(NIM_MODIFY, &nid);
}

void OpenSettings(HWND hwnd)
{
    if (g_settingsOpen)
        return;
    g_settingsOpen = true;
    // Anything held when the dialog opens would otherwise stay stuck down
    // while the picker eats keystrokes.
    ReleaseTranslatedKeys();
    ShowSettingsDialog(hwnd, g_instance);
    g_settingsOpen = false;
}

void ShowTrayMenu(HWND hwnd)
{
    HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING, kCmdSettings, L"Settings…");
    AppendMenuW(menu, MF_STRING | (g_paused ? MF_CHECKED : 0), kCmdPause, L"Pause remapping");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, kCmdResetMap, L"Remove kernel Scancode Map…");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, kCmdExit, L"Exit");

    POINT pt;
    GetCursorPos(&pt);
    SetForegroundWindow(hwnd);
    TrackPopupMenu(menu, TPM_RIGHTBUTTON, pt.x, pt.y, 0, hwnd, nullptr);
    PostMessageW(hwnd, WM_NULL, 0, 0);
    DestroyMenu(menu);
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
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
        case kCmdResetMap:
            if (MessageBoxW(hwnd,
                            L"Remove the kernel Scancode Map?\n\n"
                            L"This restores stock behaviour for every key it remapped, "
                            L"including any MacKeys relies on. Administrator rights are "
                            L"required and the change takes effect at the next reboot.",
                            L"MacKeys", MB_YESNO | MB_ICONWARNING) == IDYES) {
                if (ResetScancodeMap())
                    MessageBoxW(hwnd, L"Removed. Reboot to take effect.", L"MacKeys",
                                MB_OK | MB_ICONINFORMATION);
                else
                    MessageBoxW(hwnd, L"Could not remove it (the elevation prompt was "
                                      L"declined, or the write failed).",
                                L"MacKeys", MB_OK | MB_ICONERROR);
            }
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

void OnConfigChanged()
{
    // Any key held under the old tables would never see its keyup translated.
    ReleaseTranslatedKeys();

    for (int i = 0; i < kKeySlots; ++i) {
        g_base[i] = g_config.base[i];
        g_nav[i] = g_config.nav[i];
        g_alias[i] = kNoKey;
    }
    g_chords = g_config.chords;
    const ScancodeMapPlan plan = BuildScancodeMap(g_config);
    for (const Divert& d : plan.diverts)
        g_alias[KeySlot(d.to)] = d.from;
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int)
{
    g_instance = instance;

    CreateMutexW(nullptr, TRUE, L"Local\\MacKeysSingleInstance");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        MessageBoxW(nullptr, L"MacKeys is already running (check the tray).", L"MacKeys",
                    MB_OK | MB_ICONINFORMATION);
        return 0;
    }

    LoadConfig();
    OnConfigChanged();
    RegisterKeyboardControl(instance);
    g_taskbarCreatedMsg = RegisterWindowMessageW(L"TaskbarCreated");

    WNDCLASSW wc = {};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = instance;
    wc.lpszClassName = kWindowClass;
    wc.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(IDI_APP));
    RegisterClassW(&wc);

    // A normal (never shown) window rather than HWND_MESSAGE: message-only
    // windows don't receive the TaskbarCreated broadcast.
    HWND hwnd = CreateWindowExW(WS_EX_TOOLWINDOW, kWindowClass, L"MacKeys", WS_POPUP, 0, 0, 0, 0,
                                nullptr, nullptr, instance, nullptr);
    if (!hwnd)
        return 1;

    g_nid.cbSize = sizeof(g_nid);
    g_nid.hWnd = hwnd;
    g_nid.uID = kTrayId;
    g_nid.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    g_nid.uCallbackMessage = kTrayMessage;
    g_nid.hIcon = static_cast<HICON>(LoadImageW(instance, MAKEINTRESOURCEW(IDI_APP), IMAGE_ICON,
                                                GetSystemMetrics(SM_CXSMICON),
                                                GetSystemMetrics(SM_CYSMICON), 0));
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

    // A config needing a kernel remap that isn't installed yet works in every
    // respect except Win+L, which is worth saying out loud rather than leaving
    // the user to discover by locking their PC.
    if (!ScancodeMapIsCurrent(BuildScancodeMap(g_config)))
        ShowBalloon(hwnd, L"MacKeys",
                    L"Your key setup needs a kernel remap that isn't installed yet. "
                    L"Open Settings and click OK to write it.");

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    UnhookWindowsHookEx(g_hook);
    ReleaseTranslatedKeys();
    return 0;
}
