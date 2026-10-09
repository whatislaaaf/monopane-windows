# Monopane

Three things a Mac keyboard habit wants on Windows, in one tray app with one
keyboard hook:

- **A Mac-style key map** — Caps Lock as Backspace, a nav layer under a thumb
  key, cmd/option-style editing chords, all configurable on a picture of the
  keyboard. (This was [MacKeys](#the-key-map), before it moved in here.)
- **A searchable Cmd+Tab switcher** — press the hotkey, type a few letters of
  a window's name, press Enter. Inspired by the *Search in Context* feature of
  the [Context](https://contexts.co) app on macOS.
- **A launchpad** — Ctrl+Space opens a grid of the apps you have pinned, with
  a pencil to rearrange them: a Start menu whose pinned area is as big as you
  want it.

And for games: **double-click the tray icon** and all of it pauses — the icon
turns grey with a pause sign — so the keyboard in there is exactly what the
hardware sends. Double-click again to resume.

Native C++ / Win32, single small executable, no dependencies, per-monitor DPI
aware.

## Pausing for games

Double-click the tray icon and the key map and the hotkeys stand down: every
physical key reaches the game untouched, so its own bindings and native
`Alt+Tab` work as they would without Monopane running at all. The icon turns
grey with a pause sign on it while this is in effect, and the tooltip says so.
Double-click again — or use **Pause the key map and hotkeys** in the tray
menu, which does the same — and everything comes back.

Nothing is detected automatically: a game is paused for exactly as long as you
say, and the icon always shows which state you are in.

## The key map

Every mapping is configurable on a picture of the keyboard in **Settings →
Key setup**, and lives in a plain text file at `%APPDATA%\Monopane\keys.ini`.

### Defaults

Out of the box, on a standard Windows-layout board:

| Physical key | Acts as |
| --- | --- |
| Caps Lock | Backspace |
| Right Alt | Nav layer (swallowed when tapped alone) |

Nav layer (hold the layer key):

| Key | Sends |
| --- | --- |
| `j` `k` `i` `l` | Left / Down / Up / Right arrow |
| `h` | Home |
| `;` | End |
| `u` | Ctrl+Win+Left — previous virtual desktop |
| `o` | Ctrl+Win+Right — next virtual desktop |

Chords that span several keys live in the Hotkeys list on the **Key options**
tab rather than on the keyboard picture. None are bound out of the box:
**Left Ctrl + Space**, the Spotlight position, belongs to the launchpad. The
actions on offer there:

| Action | Does |
| --- | --- |
| Open the Start menu | Taps the Win key (costs whatever chord you put it on, e.g. the raw Ctrl+Space used by IDE autocomplete) |
| Switch input language | Sends Win+Space |
| Send another chord | Rewrites one chord as another |

Arrow targets in the nav layer understand Mac-style modifiers, translated to
the Windows equivalents (held Alt/Ctrl are masked as needed so apps see only
the chord):

| Combo | Sends | Mac equivalent |
| --- | --- | --- |
| Alt + `j`/`l` | Ctrl+Left/Right — word jump | option+←/→ |
| Alt + `i`/`k` | Ctrl+Up/Down | option+↑/↓ |
| Ctrl-role key + `j`/`l` | Home / End — line start/end | cmd+←/→ |
| Ctrl-role key + `i`/`k` | Ctrl+Home/End — document start/end | cmd+↑/↓ |

Shift passes through everywhere, so any of the above becomes a selection
(layer+Shift+`j` = select left), matching Karabiner's `"optional": ["any"]`.

### Hotkeys on the key map

**Settings → Key options → Hotkeys** is a list of chords, each captured by
pressing it. **Add…** picks what the chord should do — open the Start menu,
switch input language, or send another chord — then waits for you to press
it. **Set keys…** re-captures the selected one.

Capture works like the key picker: hold the modifiers, press the trigger, let
go. The trigger is whichever key you pressed last.

Two things make these chords more precise than a normal Windows hotkey:

- **Left and right are distinct.** `Left Ctrl + Space` fires on the left Ctrl
  only; right Ctrl + Space passes through to the app untouched.
- **Matching is exact.** Every listed key must be held and nothing else, so
  `Left Ctrl + Space` does not fire on Ctrl+Shift+Space — that still reaches
  your editor.

Chords name **physical keys**, the same as the picker. If Left Ctrl is remapped
to send Alt, a chord on "Left Ctrl" still means the key in the Ctrl position.
That keeps capture honest — you press the keys you mean — and stops chords
drifting when you rebind the bottom row.

`send another chord` lifts whatever the trigger's own modifiers are holding
down before sending the output, so `Left Alt + Q` → `Alt+F4` arrives as Alt+F4
rather than as whatever Alt+Q happened to leave pressed.

### The key picker

**Settings → Key setup** draws the keyboard and marks every key that carries a
mapping. It edits a copy of the config; nothing is applied until OK.

- **Left-click a key** and then press the key it should send. Esc cancels, and
  the capture releases itself after 15 seconds or on any click, so a picker
  waiting for input can never leave the keyboard swallowed.
- **Right-click a key** for the roles that aren't "send another key": *hold for
  nav layer*, *Windows key*, *Ctrl (Mac cmd)*, the virtual-desktop switches, and
  *Clear*.
- **Base layer / Nav layer** switches which of the two tables you're editing —
  `j` sends `j` on the base layer and Left on the nav layer.
- Pointing a key at itself clears it.

A key holding a **role** is marked with a ◆. The distinction matters: a key
*sending* Right Ctrl gives you Ctrl for copy/paste, but only the `Ctrl (Mac cmd)`
**role** drives the layer+arrow line and document jumps, because by the time a
plain remap reaches an app the hook has no idea the Ctrl came from a remap.

`Fn`, `PrtSc` and `Pause` are drawn but can't be bound. Fn is handled inside
the keyboard's own firmware on most boards and never reaches Windows at all;
the other two arrive as multi-scancode sequences.

Keys are identified by **scancode**, not virtual-key code, so left and right
modifiers stay distinct and bindings don't move when the input language does.

### keys.ini

`%APPDATA%\Monopane\keys.ini` — the **Open config folder** button in Settings
opens it with the file selected. Edit it by hand if you prefer; restart
Monopane to reload.

```ini
[options]
layout           = auto      # auto | ansi | iso

[base]
3A = key:0E               # Caps Lock -> Backspace
E038 = layer              # Right Alt -> nav layer

[nav]
23 = key:E047             # H -> Home
16 = desktop:prev         # U -> prev desktop

[chords]
input_language = 38+39           # Left Alt + Space
send           = 38+10 > E038+3E # Left Alt + Q sends Right Alt + F4
start_menu     = E01D+39         # Right Ctrl + Space
```

Keys are set-1 scancodes in hex, with `E0` prefixing extended keys. Actions are
`key:<scancode>`, `layer`, `winkey`, `ctrl`, `desktop:prev`, `desktop:next` and
`none`. A key missing from a section is unbound — the file is authoritative,
including its omissions.

In `[chords]`, keys are joined with `+` and the **last one is the trigger**;
everything before it must be held. `send` takes a second chord after `>`. Up to
four modifiers and sixteen chords.

`layout` picks which keyboard is drawn. `auto` probes the active layout for
scancode `0x56`, the extra key beside left Shift, which only resolves on ISO —
that follows the *layout*, not the hardware, so set it explicitly if the guess
is wrong.

### Win+L and the Scancode Map

`Win+L` is handled by Windows below keyboard hooks (like `Ctrl+Alt+Del`), so a
hook alone cannot stop a physical Win key from locking the PC when it is
chorded. A Win key given a non-Windows job is therefore remapped at the kernel
level instead, via `HKLM\SYSTEM\CurrentControlSet\Control\Keyboard Layout`:

| Physical | Scancode | Becomes |
| --- | --- | --- |
| Left Win | `E0 5B` | F23 (`0x6E`) |
| Right Win | `E0 5C` | F24 (`0x76`) |
| whichever key you gave the Windows-key role | — | Right Win (`E0 5C`) |

Monopane generates and writes this itself. Clicking OK in Settings compares the
installed map with the one your config implies and, if they differ, offers to
write it — one UAC prompt, then a reboot to take effect. The hook aliases the
diverted scancodes back to their physical identity, so bindings stay attached
to the key you clicked on the picture.

**If your layer key is not a Win key, none of this happens.** The plan comes out
empty, the registry value is deleted if present, and the whole admin/reboot step
disappears. The defaults are deliberately arranged that way.

Guard rails, because a malformed map applies at boot before anything clickable
exists:

- Every blob is validated structurally before it is written, and destinations
  are restricted to a three-item whitelist (F23, F24, Right Win) — no key can
  be disabled or turned into something arbitrary.
- **Remove kernel Scancode Map…** in the tray menu, and **Remove** on the
  Key options tab, delete the value outright and restore stock behaviour.
- Manual undo: delete the `Scancode Map` value and reboot.

## The switcher

- **A hotkey you capture by pressing it** opens a searchable overlay (native
  Alt+Tab is left untouched), on the monitor the cursor is on
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

### The hotkeys

There are three, all captured the same way: **Settings → Switcher → Set…**,
then hold the modifiers and press the trigger key and let go. The trigger is
whichever key you pressed last. Esc cancels, and the capture releases itself
after 15 seconds, so it can never leave the keyboard swallowed.

#### Opening the switcher

Out of the box it is **Cmd+Tab**, where Cmd is whichever key the key map gives
the *Ctrl (Mac cmd)* role.

Two things make this more precise than a normal Windows hotkey, both courtesy
of keychord (`external/keychord`):

- **Left and right are distinct**, because keys are identified by scancode
  rather than virtual-key code.
- **A remapped key is distinct from a real one.** Each key in the chord records
  whether it has to arrive as a genuine keypress or as one the key map
  produced. That is what lets the hotkey sit on the remapped Ctrl while a
  physical `Ctrl+Tab` still reaches your editor, so binding it here does not
  cost you that chord everywhere else.

Extra modifiers are tolerated, so `Cmd+Shift+Tab` still cycles backwards. Native
`Alt+Tab` and `Win+Tab` are untouched.

A hotkey captured on a remapped key records the key map's output, so it does
not fire while the key map is paused. Capture a different chord if you want
one that works without it.

#### Rotating the display

**Ctrl+Alt+R** by default, and unlike the switcher hotkey it does not care where
its keys come from: with a remapped Ctrl it reads as Cmd+Alt+R, without one as
a plain physical Ctrl+Alt+R, and either fires it.

Each press turns the monitor **the cursor is on** one step through:

    Landscape → Portrait → Portrait (flipped) → Landscape

Landscape (flipped) is not in the cycle — a monitor that starts there rotates on
to Landscape. Which of the two portrait modes matches the way you physically
turned the screen depends on the monitor, so press it twice if the first one
comes out upside down. The change is written to the registry, so it survives a
reboot, exactly as the Settings app's own dropdown does.

`AltGr+R` is safe: AltGr arrives as *right* Alt, and the chord is bound to the
left one by scancode.

#### Opening the launchpad

**Left Ctrl+Space** by default, taken however the keys arrive, and tolerant of
extra modifiers like the other two. Capture it by pressing the key you think of
as Ctrl: if that key is remapped, the chord records whatever the key map turns
it into, and works all the same.

## The launchpad

A panel on the monitor with the cursor: a search box, a pencil, and a
six-wide grid of icons. It is the same width as the switcher and puts its
search box on exactly the same line — the middle of the screen — so the two
do not jump around each other as you flip between them. Type to filter it,
arrows move the highlight, **Enter** or a click opens the app, **Esc** closes.
Opening an app that already has a window brings that window forward;
**Shift+Enter** or Shift+click starts a fresh instance regardless. Search
memory works here too.

The **pencil** (top right) switches to edit mode:

- **Drag** a tile to move it; the others shuffle out of the way
- The **✕** on a tile removes it (or select it and press **Delete**)
- Click a tile's **name** to rename it (or **F2**); Enter commits, Esc reverts,
  and typing the app's own name back clears the custom one
- The **+** tile adds an app: *Installed app…* lists everything the Start menu
  knows about, Store apps included, with a search box; *Browse for a file…*
  takes any `.exe` or `.lnk`

The pencil again, or Esc, leaves edit mode.

The first time it opens, the grid is seeded with whatever is pinned to your
Start menu, in the same order. After that it is entirely yours: the list lives
in `%APPDATA%\Monopane\launchpad.ini`, next to `keys.ini`, one app per line in
grid order, either a path or `app:<AppUserModelID>`, with an optional `| name`
after it. Edit it by hand if you like; the launchpad rereads it the next time
it opens.

## Usage

| Key | Action |
| --- | --- |
| your hotkey (default `Cmd+Tab`) | Open the switcher (on the monitor with the cursor) |
| `Cmd+Tab` again / `Tab` / `↓` | Move selection down |
| `Cmd+Shift+Tab` / `Shift+Tab` / `↑` | Move selection up |
| type letters | Fuzzy-filter the window list |
| `Enter` | Activate the selected window |
| `Esc` | Dismiss without switching |
| `Ctrl+Alt+R` (default) | Rotate the monitor under the cursor one step |
| `Ctrl+Space` (default) | Open the launchpad (again to close it) |
| in the launchpad: `←` `→` `↑` `↓` | Move the highlight |
| in the launchpad: `Enter` / click | Open the app, or bring its window forward |
| in the launchpad: `Shift+Enter` / Shift+click | Start a fresh instance |
| in edit mode: `Delete` / `F2` | Remove / rename the highlighted tile |

Monopane lives in the system tray. Right-click the tray icon for
**Launchpad**, **Settings…**, **Pause the key map and hotkeys**, **Remove
kernel Scancode Map…** and **Exit**; double-click it to pause and resume.

Settings has three tabs — **Switcher**, **Key setup** and **Key options** —
and, underneath them, **Start with Windows**.
Autostart uses a Task Scheduler logon task named `Monopane` (the classic Run
key proved unreliable and is staggered by a minute or more at logon); toggling
it shows a UAC prompt because logon-trigger tasks require elevation.

Only one instance runs at a time. The hook ignores injected input (from remote
desktop tools, AutoHotkey, etc.) and only remaps physical keystrokes. Mappings
apply to every attached keyboard — the hook cannot tell them apart.

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

Key identity, chord text and hotkey capture live in `external/keychord`, a
library that used to be a submodule shared between the two apps and now simply
lives here.

## Coming from MacKeys and Monopane as separate apps

The two used to be separate programs with separate repositories; this one is
both, with every commit of each.

- On first launch the key map is copied from `%APPDATA%\MacKeys\mackeys.ini`
  to `%APPDATA%\Monopane\keys.ini` if the latter is missing. The old file is
  left alone.
- Exit the old `mackeys.exe` and delete its `MacKeys` logon task (Task
  Scheduler, or `schtasks /Delete /TN MacKeys` from an elevated prompt).
  Monopane warns at startup if it finds MacKeys still running, because two
  remappers would remap every key twice.
- Earlier MacKeys versions hardcoded an MX Keys Mini for Mac layout and kept
  settings in `HKCU\Software\MacKeys`. On first run the Caps Lock choice and
  the Space chords carry over into the file; the cmd/option positions do not,
  because they described Mac keycaps that a Windows-layout board doesn't have.
  Assign the bottom row in the picker instead. A file predating `[chords]` is
  upgraded on load, and an old hand-written Scancode Map is noticed and
  offered for removal.

## The Mac side

[`karabiner/karabiner.json`](karabiner/karabiner.json) is a Karabiner-Elements
config that mirrors the key map on macOS, for the same keyboard on the same
desk through a KVM. Copy it to `~/.config/karabiner/karabiner.json`.

| Physical key | Windows (Monopane) | macOS (Karabiner) |
| --- | --- | --- |
| Caps Lock | Backspace | Backspace |
| Left Ctrl | Left Alt | **Option** |
| Win | Left Ctrl | **Control** |
| Left Alt | Ctrl / Mac-cmd role | **Command** |
| Right Alt | nav layer | nav layer |
| Menu | Windows key | left alone — macOS has no counterpart |
| Right Ctrl | Right Ctrl | Right Control |
| leftmost + Space | the launchpad | **Spotlight** |
| command key + Space | switch input language | **switch input source** |

Nav layer `u` / `o` switch virtual desktops on Windows and Spaces on macOS
(`Ctrl+←` / `Ctrl+→`), which is the same gesture either side.

Two consequences worth knowing:

- **The bottom row is Option, Control, Command** — not a Mac keyboard's
  Control, Option, Command. That is what mirroring your Windows arrangement
  means; positions 1 and 2 stay swapped relative to stock macOS.
- **`Cmd+Space` no longer opens Spotlight**, it switches input source. Spotlight
  moves to the leftmost bottom-row key, matching Windows. Deleting the two
  manipulators in the *Space chords* rule restores the macOS defaults.

The modifier swap lives in `complex_modifications` rather than
`simple_modifications` deliberately: the Space chords have to see the swapped
modifier, and keeping both in the same stage makes that unambiguous.

The Mac-side editing chords need no rules — option+arrow for word jump and
cmd+arrow for line and document ends are native to macOS. The key map has to
synthesise them on Windows, which is why they exist there and not here.

No `devices` block, on purpose: through a KVM the keyboard often reports the
switch's own vendor and product ids rather than the keyboard's, and those can
change with a firmware update or a different port — a device-scoped rule would
then silently stop applying. The Mac mini has no built-in keyboard to protect,
so profile-wide is both simpler and more robust.

## Limitations

- Windows Store / UWP apps are hosted by *ApplicationFrameHost*, so their icon and
  app name may show as the frame host rather than the actual app.
