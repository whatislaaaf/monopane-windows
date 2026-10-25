#pragma once
#include <windows.h>

#include "keys.h"

// The key remapper: what MacKeys was before it moved in here. The hook hands
// it every physical key first; what it lets through, or injects in place of
// it, is what the switcher's hotkeys then see.
//
// The tables it matches against are only ever touched from the hook thread.
// The UI thread asks for changes through the Request* calls, which post to
// that thread, so no key can be mid-translation while its binding changes.

namespace remap {

// dwExtraInfo stamped on every key the remapper injects, so the hook can tell
// its own output from a physical key and from other tools' injected input.
constexpr ULONG_PTR kInjectMarker = 0x4D4B5953; // "MKYS"

// ------------------------------------------------------ hook-thread side

// Runs one physical key event through the captures, the pause and the key
// map. Returns true when the event was consumed and must not be passed on.
bool HandlePhysicalKey(const KBDLLHOOKSTRUCT& k, bool down);

// A key holding the switcher role is down. It is swallowed, so neither the OS
// nor the hotkeys ever see it; Tab under it opens the switcher.
bool SwitcherHeld();

// Rebuilds the live tables from g_config (taking g_settingsLock shared),
// releasing anything held under the old ones.
void ReloadConfig();

// Paused, every physical key passes through untouched and whatever the
// remapper was holding down is released. The captures in Settings keep
// working, so a key can still be picked while paused.
void SetPaused(bool paused);
bool Paused();

// Lifts every key the remapper has down. Called on the hook thread normally;
// safe from the main thread once the hook thread has stopped.
void ReleaseAll();

// -------------------------------------------------------- UI-thread side

// The hook thread registers itself here; until it has, the requests are
// dropped, which is fine because ReloadConfig runs when it starts anyway.
void BindHookThread(DWORD threadId);
void RequestReload();
void RequestPause(bool paused);
void RequestRelease();

// The thread messages the requests travel as; the hook thread's loop in
// main.cpp dispatches them.
constexpr UINT WM_REMAP_RELOAD = WM_APP + 20;
constexpr UINT WM_REMAP_PAUSE = WM_APP + 21; // wParam: 1 = pause
constexpr UINT WM_REMAP_RELEASE = WM_APP + 22;

} // namespace remap
