# Monopane

A searchable Alt+Tab window switcher for Windows, inspired by the *Search in Context*
feature of the [Context](https://contexts.co) app on macOS.

Press **Cmd+Tab** (see below), type the first few letters of a window's name,
press **Enter** — that window becomes active.

## Features

- **Mac-style Cmd+Tab** opens a searchable overlay (via a low-level keyboard
  hook; native Alt+Tab is left untouched)
- **Fuzzy search** over app name and window title — `chr` matches *Google Chrome*,
  `vsc` matches *Visual Studio Code*
- **Keyboard navigation** — Up/Down arrows, Tab / Shift+Tab, PageUp/PageDown,
  Enter to activate, Esc to dismiss
- **App icons** next to each window, like Context
- **Mouse support** — hover to select, click to activate
- **Learned aliases** — activating a window with a search typed remembers that
  query → app pairing (e.g. `cla` → Claude), ranking it first from then on;
  forgettable via *Clear search memory* in Settings
- **Start with Windows** — toggle from the tray icon menu
- Native C++ / Win32, single small executable, no dependencies, per-monitor DPI aware

## The hotkey

Monopane is built to pair with [MacKeys](../mackeys-windows), which remaps a Mac
keyboard's left Cmd key on Windows (physically delivered as `F23` via a kernel
Scancode Map, then held as `Left Ctrl` by MacKeys). The switcher opens on
**left Cmd+Tab** and recognizes the key in either form, so it works whether
MacKeys is running, paused, or absent. Physical `Ctrl+Tab` is deliberately not
intercepted — in-app tab switching keeps working — and native `Alt+Tab` and
`Win+Tab` are untouched.

## Usage

| Key | Action |
| --- | --- |
| `Cmd+Tab` | Open the switcher (opens on the monitor with the cursor) |
| `Cmd+Tab` again / `Tab` / `↓` | Move selection down |
| `Cmd+Shift+Tab` / `Shift+Tab` / `↑` | Move selection up |
| type letters | Fuzzy-filter the window list |
| `Enter` | Activate the selected window |
| `Esc` | Dismiss without switching |

Monopane lives in the system tray. Right-click the tray icon for
**Start with Windows** and **Exit**.

## Building

Requires Visual Studio 2019+ with the *Desktop development with C++* workload.

```bat
build.bat
```

Produces `build\monopane.exe`. Alternatively, with CMake:

```bat
cmake -B out -S .
cmake --build out --config Release
```

## Limitations

- The hotkey assumes the MacKeys left-Cmd setup (`F23` scancode remap). On a
  stock keyboard without that remap there is currently no way to trigger the
  switcher.
- Windows Store / UWP apps are hosted by *ApplicationFrameHost*, so their icon and
  app name may show as the frame host rather than the actual app.
