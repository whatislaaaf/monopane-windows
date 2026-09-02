# Monopane

A searchable Alt+Tab window switcher for Windows, inspired by the *Search in Context*
feature of the [Context](https://contexts.co) app on macOS.

Press your hotkey (**Cmd+Tab** by default, see below), type the first few letters of a window's name,
press **Enter** — that window becomes active.

## Features

- **A hotkey you capture by pressing it** opens a searchable overlay (via a
  low-level keyboard hook; native Alt+Tab is left untouched), centred on the
  monitor the cursor is on
- **Fuzzy search** over app name and window title — `chr` matches *Google Chrome*,
  `vsc` matches *Visual Studio Code*
- **Keyboard navigation** — Up/Down arrows, Tab / Shift+Tab, PageUp/PageDown,
  Enter to activate, Esc to dismiss
- **App icons** next to each window, like Context
- **Mouse support** — hover to select, click to activate
- **Learned aliases** — activating a window with a search typed remembers that
  query → app pairing (e.g. `cla` → Claude), ranking it first from then on;
  forgettable via *Clear search memory* in Settings
- **Rotate the display** — a second hotkey (`Ctrl+Alt+R` by default) cycles the
  monitor under the cursor through landscape → portrait → portrait (flipped),
  for swapping between a tall screen for work and a wide one for games
- **Start with Windows** — toggle from the tray icon menu
- Native C++ / Win32, single small executable, no dependencies, per-monitor DPI aware

## The hotkeys

There are two, both captured the same way: **Settings → Hotkeys → Set…**, then
hold the modifiers and press the trigger key and let go. The trigger is
whichever key you pressed last. Esc cancels, and the capture releases itself
after 15 seconds, so it can never leave the keyboard swallowed.

### Opening the switcher

Out of the box it is **Cmd+Tab**, where Cmd is whichever key
[MacKeys](https://github.com/whatislaaaf/mackeys-windows) gives the *Ctrl (Mac
cmd)* role.

Two things make this more precise than a normal Windows hotkey, both courtesy of
[keychord](https://github.com/whatislaaaf/keychord):

- **Left and right are distinct**, because keys are identified by scancode
  rather than virtual-key code.
- **A remapped key is distinct from a real one.** Each key in the chord records
  whether it has to arrive as a genuine keypress or as one MacKeys injected.
  That is what lets the hotkey sit on the remapped Ctrl while a physical
  `Ctrl+Tab` still reaches your editor, so binding it here does not cost you
  that chord everywhere else.

Extra modifiers are tolerated, so `Cmd+Shift+Tab` still cycles backwards. Native
`Alt+Tab` and `Win+Tab` are untouched.

A hotkey captured while MacKeys is running records MacKeys' injected key, so it
will not fire while MacKeys is paused. Capture a different chord if you want one
that works without it.

### Rotating the display

**Ctrl+Alt+R** by default, and unlike the switcher hotkey it does not care where
its keys come from: under MacKeys it reads as Cmd+Alt+R, without it as a plain
physical Ctrl+Alt+R, and either fires it.

Each press turns the monitor **the cursor is on** one step through:

    Landscape → Portrait → Portrait (flipped) → Landscape

Landscape (flipped) is not in the cycle — a monitor that starts there rotates on
to Landscape. Which of the two portrait modes matches the way you physically
turned the screen depends on the monitor, so press it twice if the first one
comes out upside down. The change is written to the registry, so it survives a
reboot, exactly as the Settings app's own dropdown does.

`AltGr+R` is safe: AltGr arrives as *right* Alt, and the chord is bound to the
left one by scancode.

## Usage

| Key | Action |
| --- | --- |
| your hotkey (default `Cmd+Tab`) | Open the switcher (centred on the monitor with the cursor) |
| `Cmd+Tab` again / `Tab` / `↓` | Move selection down |
| `Cmd+Shift+Tab` / `Shift+Tab` / `↑` | Move selection up |
| type letters | Fuzzy-filter the window list |
| `Enter` | Activate the selected window |
| `Esc` | Dismiss without switching |
| `Ctrl+Alt+R` (default) | Rotate the monitor under the cursor one step |

Monopane lives in the system tray. Right-click the tray icon for
**Start with Windows** and **Exit**.

## Building

Requires Visual Studio 2019+ with the *Desktop development with C++* workload.

Key handling lives in the [keychord](https://github.com/whatislaaaf/keychord)
submodule, so a fresh clone needs:

```bat
git submodule update --init
```

Then:

```bat
build.bat
```

Produces `build\monopane.exe`. Alternatively, with CMake:

```bat
cmake -B out -S .
cmake --build out --config Release
```

## Limitations

- Windows Store / UWP apps are hosted by *ApplicationFrameHost*, so their icon and
  app name may show as the frame host rather than the actual app.
