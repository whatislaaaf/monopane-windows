# keychord

Physical key identity, chords, and hotkey capture for Win32 keyboard-hook apps.
Header plus three source files, no dependencies beyond `user32`.

Used by [MacKeys](https://github.com/whatislaaaf/mackeys-windows) and
[Monopane](https://github.com/whatislaaaf/monopane-windows).

## What it gives you

**Scancode identity.** `KeyId` is a set-1 scancode with `0xE0` prefixing
extended keys, so left and right modifiers stay distinct and bindings do not
move when the input language does. `KeyName()` turns one into "Right Alt".

**Chords.** `KeyChord` is up to four modifiers plus a trigger, with text
round-tripping for config files:

```
1D+39                # left ctrl + space
injected:1D+0F       # a remapper's injected left ctrl, then Tab
```

**Origins.** Each key in a chord records whether it must arrive as a real
keypress, as a key another tool injected, or either:

```cpp
enum class KeyOrigin { Any, Physical, Injected };
```

This is what lets a hotkey sit on a remapped Ctrl while a genuine physical
`Ctrl+Tab` still reaches the focused app.

**Capture.** `BeginChordCapture()` arms; feed your `WH_KEYBOARD_LL` callback into
`FeedChordKey()`; the chord completes when the last key comes back up, so the
trigger is whatever was pressed last. The notify window gets
`WM_CHORD_CAPTURED` or `WM_CHORD_CANCELLED`.

Everything is swallowed while armed, so capture always carries a timeout —
that backstop, plus Esc as the first key, is what stops an abandoned capture
holding the keyboard hostage.

## Matching

`ChordMatches()` takes an array of `KeyOrigin` indexed by `KeySlot()`, holding the
origin each key is currently down as (`KeyOrigin::Any` meaning up).

`exact` also requires that nothing else is held. Use it for a hotkey that
should be precise — `Left Ctrl + Space` firing but `Ctrl+Shift+Space` reaching
the app. Leave it off where extra modifiers must be tolerated, such as
`Cmd+Shift+Tab` reaching a `Cmd+Tab` switcher.

## Hook order

Two hook apps on one machine are called in reverse install order, and that
order is a race at logon. If a remapper swallows a physical key and injects a
replacement, your app may see the original, the replacement, or both,
differing between sessions.

Capture handles this: when any injected modifier appears, the physical ones are
dropped, because the injected form is the one that arrives whichever way the
race went.

## Using it

Add as a submodule, then compile the three `.cpp` files alongside your own:

```bat
git submodule add https://github.com/whatislaaaf/keychord external/keychord
```

```cpp
#include "../external/keychord/keychord.h"
using namespace keychord;
```
