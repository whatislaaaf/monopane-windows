#include "keychord.h"

namespace keychord {
namespace {

constexpr UINT_PTR kTimerId = 0xC401;

HWND g_notify = nullptr;
bool g_armed = false;

// Keys in the order they went down, so the last one is the trigger.
struct Pressed {
    KeyId key;
    KeyOrigin origin;
};
Pressed g_pressed[kMaxChordMods + 1];
int g_pressedCount = 0;

KeyOrigin g_down[kKeySlots];
int g_downCount = 0;
bool g_sawKey = false;

KeyChord g_result;

int IndexOf(KeyId key)
{
    for (int i = 0; i < g_pressedCount; ++i)
        if (g_pressed[i].key == key)
            return i;
    return -1;
}

void Reset()
{
    g_armed = false;
    g_notify = nullptr;
    g_pressedCount = 0;
    g_downCount = 0;
    g_sawKey = false;
    for (KeyOrigin& o : g_down)
        o = KeyOrigin::Any;
}

void Finalize()
{
    g_result = KeyChord();
    if (g_pressedCount == 0)
        return;

    // The trigger is whatever was pressed last.
    const Pressed trigger = g_pressed[g_pressedCount - 1];

    // When a remapper is in play the same physical press can reach us twice —
    // once as the real key (if our hook runs before the remapper's) and again
    // as its injected replacement. The injected form is the one that always
    // arrives, whichever order the hooks ended up in, so it wins and the
    // physical modifiers are dropped.
    bool sawInjectedMod = false;
    for (int i = 0; i < g_pressedCount - 1; ++i)
        sawInjectedMod = sawInjectedMod || g_pressed[i].origin == KeyOrigin::Injected;

    for (int i = 0; i < g_pressedCount - 1; ++i) {
        if (sawInjectedMod && g_pressed[i].origin != KeyOrigin::Injected)
            continue;
        if (g_result.modCount >= kMaxChordMods)
            break;
        g_result.mods[g_result.modCount] = g_pressed[i].key;
        g_result.modOrigin[g_result.modCount] = g_pressed[i].origin;
        ++g_result.modCount;
    }
    g_result.trigger = trigger.key;
    g_result.triggerOrigin = trigger.origin;
}

} // namespace

bool BeginChordCapture(HWND notify, UINT timeoutMs)
{
    if (g_armed || !notify)
        return false;
    Reset();
    g_armed = true;
    g_notify = notify;
    // Everything is swallowed while armed, so this backstop is what guarantees
    // an abandoned capture cannot hold the keyboard hostage.
    SetTimer(notify, kTimerId, timeoutMs, nullptr);
    return true;
}

void CancelChordCapture()
{
    if (!g_armed)
        return;
    KillTimer(g_notify, kTimerId);
    Reset();
}

bool ChordCaptureActive() { return g_armed; }

KeyChord CapturedChord() { return g_result; }

bool FeedChordKey(KeyId id, bool down, KeyOrigin origin)
{
    if (!g_armed)
        return false;

    const int slot = KeySlot(id);

    if (down) {
        if (g_down[slot] != KeyOrigin::Any)
            return true; // autorepeat
        if (!g_sawKey && id == 0x01) { // Esc, before anything else, cancels
            HWND notify = g_notify;
            KillTimer(notify, kTimerId);
            Reset();
            PostMessageW(notify, WM_CHORD_CANCELLED, 0, 0);
            return true;
        }
        g_sawKey = true;
        g_down[slot] = origin;
        ++g_downCount;
        if (g_pressedCount < kMaxChordMods + 1 && IndexOf(id) < 0)
            g_pressed[g_pressedCount++] = { id, origin };
        return true;
    }

    if (g_down[slot] == KeyOrigin::Any)
        return false; // was held before we armed; not ours to eat
    g_down[slot] = KeyOrigin::Any;
    if (--g_downCount > 0)
        return true;

    HWND notify = g_notify;
    Finalize();
    KillTimer(notify, kTimerId);
    g_armed = false;
    g_notify = nullptr;
    PostMessageW(notify, WM_CHORD_CAPTURED, 0, 0);
    return true;
}

} // namespace keychord
