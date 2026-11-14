// The key remapper — MacKeys, before it moved in here.
//
// Every mapping lives in %APPDATA%\Monopane\keys.ini and is edited on a
// picture of the keyboard in Settings. Two tables drive it: `base` applies
// always, `nav` applies only while a key bound to `layer` is held. Injected
// input never reaches it, so other automation tools are not re-remapped.
//
// The only remapping that cannot happen here is a Win key doing a non-Windows
// job: Win+L is handled below keyboard hooks, so those keys are diverted onto
// spare scancodes by the kernel Scancode Map instead (see scancodemap.h) and
// aliased back to their physical identity on the way in.
//
// Everything in here runs on the hook thread, which is the only thread that
// touches the tables; the UI thread talks to it through the Request* calls.

#include "remap.h"

#include <vector>

#include "config.h"
#include "keyboard.h"
#include "keysettings.h"
#include "lock.h"
#include "scancodemap.h"

namespace remap {
namespace {

constexpr KeyId kSwallowUpOnly = 0xFFFF; // keyup consumed, nothing sent

// Scancodes the chord helpers need by name.
constexpr KeyId kScLCtrl = 0x1D;
constexpr KeyId kScLAlt = 0x38;
constexpr KeyId kScCapsLock = 0x3A;
constexpr KeyId kScRAlt = 0xE038;
constexpr KeyId kScSpace = 0x39;
constexpr KeyId kScLeft = 0xE04B;
constexpr KeyId kScRight = 0xE04D;
constexpr KeyId kScUp = 0xE048;
constexpr KeyId kScDown = 0xE050;

// The fake left-ctrl that AltGr layouts emit alongside every right alt.
constexpr DWORD kAltGrFillerScan = 0x21D;

DWORD g_hookThreadId = 0;
bool g_paused = false;

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
int g_switcherHeld = 0;      // number of held switcher keys
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

// Send one tap of `id` wrapped in Ctrl, while temporarily lifting held Alt so
// the app on the receiving end sees exactly the Mac-style editing chord and
// nothing else.
void SendNavChord(KeyId id)
{
    INPUT in[8];
    int n = 0;
    auto add = [&](KeyId key, bool down) { FillKeyInput(in[n++], key, down); };
    const bool altL = (GetAsyncKeyState(VK_LMENU) & 0x8000) != 0;
    const bool altR = (GetAsyncKeyState(VK_RMENU) & 0x8000) != 0;
    // Ctrl goes down before any alt-up so the release cannot focus a menu bar.
    add(kScLCtrl, true);
    if (altL) add(kScLAlt, false);
    if (altR) add(kScRAlt, false);
    add(id, true);
    add(id, false);
    if (altR) add(kScRAlt, true);
    if (altL) add(kScLAlt, true);
    add(kScLCtrl, false);
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
    g_switcherHeld = 0;
    for (int slot = 0; slot < kKeySlots; ++slot)
        g_physDown[slot] = KeyOrigin::Any;
    g_physCount = 0;
}

// A remapped Caps Lock can no longer turn Caps Lock off, so a lock switched on
// while the key map was not running — paused for a game, or before Monopane
// started at logon — would stay on for good. Whenever the key map takes over
// again, a lock it cannot reach is put back off.
void ClearUnreachableCapsLock()
{
    if (g_base[KeySlot(kScCapsLock)].action == Action::None)
        return;
    if (!(GetKeyState(VK_CAPITAL) & 1))
        return;
    INPUT in[2] = {};
    FillKeyInput(in[0], kScCapsLock, true);
    FillKeyInput(in[1], kScCapsLock, false);
    SendInput(2, in, sizeof(INPUT));
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

} // namespace

bool HandlePhysicalKey(const KBDLLHOOKSTRUCT& k, bool down)
{
    if (k.flags & LLKHF_INJECTED)
        return false;

    KeyId id = MakeKeyId(k.scanCode, (k.flags & LLKHF_EXTENDED) != 0);
    // Undo a kernel divert, so bindings stay keyed to the physical key the
    // user clicked on the picture rather than the spare it now arrives as.
    // The captures below get the same identity: a chord captured as the
    // spare would never match, since matching runs on the key it came from.
    if (const KeyId original = g_alias[KeySlot(id)])
        id = original;

    // Both settings captures take keystrokes ahead of everything, including
    // the pause, and swallow them so nothing else reacts. The hotkey list's
    // chords are matched against physical keys, so its capture is fed here,
    // before any remapping; the switcher's captures are fed by the hook after
    // it, so they record what the switcher will actually see.
    if (CaptureActive() && ForwardCaptureKey(id, down))
        return true; // the keyboard picker, capturing one key
    if (KeysChordCaptureArmed() && keychord::FeedChordKey(id, down, KeyOrigin::Physical))
        return true; // the hotkey list, capturing a whole chord

    if (g_paused)
        return false;

    // AltGr layouts emit a fake left ctrl with every right alt; it would
    // otherwise look exactly like a real one and leak into the layer.
    if (k.scanCode == kAltGrFillerScan)
        return g_base[KeySlot(kScRAlt)].action != Action::None;

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
        return true; // swallowed, so a lone tap does nothing
    case Action::WinKey:
        // The Scancode Map already turned this into a real Windows key; the
        // hook must keep its hands off it.
        return false;
    case Action::Switcher:
        // Nothing reaches the app — not Win, not the spare the Scancode Map
        // diverted it onto. The hook asks SwitcherHeld() when Tab comes.
        if (down) {
            if (!g_translated[slot]) {
                ++g_switcherHeld;
                g_translated[slot] = kSwallowUpOnly;
            }
        } else if (g_translated[slot]) {
            g_translated[slot] = 0;
            if (g_switcherHeld > 0)
                --g_switcherHeld;
        }
        return true;
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
            return true;
        }
    }

    // ------------------------------------------------------- key movement

    if (!down) {
        const KeyId sent = g_translated[slot];
        if (sent) {
            g_translated[slot] = 0;
            if (sent != kSwallowUpOnly)
                SendKey(sent, false);
            return true;
        }
        return false;
    }

    const Bind& active = g_layerHeld > 0 && g_nav[slot].action != Action::None ? g_nav[slot]
                                                                              : base;
    switch (active.action) {
    case Action::DesktopPrev:
    case Action::DesktopNext:
        g_translated[slot] = kSwallowUpOnly;
        SendDesktopSwitch(active.action == Action::DesktopPrev ? kScLeft : kScRight);
        return true;

    case Action::Key: {
        const KeyId target = active.target;
        const bool horizontal = IsHorizontalArrow(target);
        const bool vertical = IsVerticalArrow(target);
        if ((horizontal || vertical) && AltHeld()) {
            // Mac option+arrow: word jump (left/right), paragraph-ish
            // (up/down) — Windows spells both as Ctrl+arrow.
            SendNavChord(target);
            g_translated[slot] = kSwallowUpOnly;
            return true;
        }
        g_translated[slot] = target;
        SendKey(target, true);
        return true;
    }

    default:
        break;
    }

    return false;
}

void ReloadConfig()
{
    // Any key held under the old tables would never see its keyup translated.
    ReleaseTranslatedKeys();

    AcquireSRWLockShared(&g_settingsLock);
    for (int i = 0; i < kKeySlots; ++i) {
        g_base[i] = g_config.base[i];
        g_nav[i] = g_config.nav[i];
    }
    g_chords = g_config.chords;
    ReleaseSRWLockShared(&g_settingsLock);

    // Aliases follow the map the kernel is running, not the one the config
    // wants: aliasing a divert that is not in yet mistakes a key for another
    // (an incoming Right Win taken for the key meant to become it, while the
    // physical right Win, not yet diverted, is still the one sending it).
    // The kernel reads the map at boot, so it is read once here too — a map
    // written from Settings changes the registry but not the keys until the
    // reboot.
    static const std::vector<Divert> inEffect = InstalledDiverts();
    for (int i = 0; i < kKeySlots; ++i)
        g_alias[i] = kNoKey;
    for (const Divert& d : inEffect)
        g_alias[KeySlot(d.to)] = d.from;

    ClearUnreachableCapsLock();
}

bool SwitcherHeld()
{
    return g_switcherHeld > 0;
}

void SetPaused(bool paused)
{
    if (g_paused == paused)
        return;
    g_paused = paused;
    if (paused)
        ReleaseTranslatedKeys();
    else
        ClearUnreachableCapsLock();
}

bool Paused()
{
    return g_paused;
}

void ReleaseAll()
{
    ReleaseTranslatedKeys();
}

void BindHookThread(DWORD threadId)
{
    g_hookThreadId = threadId;
}

void RequestReload()
{
    if (g_hookThreadId)
        PostThreadMessageW(g_hookThreadId, WM_REMAP_RELOAD, 0, 0);
}

void RequestPause(bool paused)
{
    if (g_hookThreadId)
        PostThreadMessageW(g_hookThreadId, WM_REMAP_PAUSE, paused ? 1 : 0, 0);
}

void RequestRelease()
{
    if (g_hookThreadId)
        PostThreadMessageW(g_hookThreadId, WM_REMAP_RELEASE, 0, 0);
}

} // namespace remap
