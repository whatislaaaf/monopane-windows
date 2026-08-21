# MacKeys

A tiny Windows tray app that gives any keyboard a Mac-style nav layer and
editing chords. Native Win32, no dependencies, single exe.

Every mapping is configurable on a picture of the keyboard in Settings, and
lives in a plain text file at `%APPDATA%\MacKeys\mackeys.ini`.

## Defaults

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

Plus one hotkey, which spans several keys and so lives in the Hotkeys list on
the Options tab rather than on the keyboard picture:

| Combo | Does |
| --- | --- |
| **Left** Ctrl + Space | Taps the Win key to open the Start menu, like Spotlight (costs the raw Ctrl+Space chord used by IDE autocomplete) |

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

## Hotkeys

**Settings → Options → Hotkeys** is a list of chords, each captured by pressing
it. **Add…** picks what the chord should do — open the Start menu, switch input
language, or send another chord — then waits for you to press it. **Set keys…**
re-captures the selected one.

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

## The key picker

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

## Configuration file

`%APPDATA%\MacKeys\mackeys.ini` — the **Open config folder** button in Settings
opens it with the file selected. Edit it by hand if you prefer; restart MacKeys
to reload.

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
start_menu     = 1D+39           # Left Ctrl + Space
input_language = 38+39           # Left Alt + Space
send           = 38+10 > E038+3E # Left Alt + Q sends Right Alt + F4
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

## Win+L and the Scancode Map

`Win+L` is handled by Windows below keyboard hooks (like `Ctrl+Alt+Del`), so a
hook alone cannot stop a physical Win key from locking the PC when it is
chorded. A Win key given a non-Windows job is therefore remapped at the kernel
level instead, via `HKLM\SYSTEM\CurrentControlSet\Control\Keyboard Layout`:

| Physical | Scancode | Becomes |
| --- | --- | --- |
| Left Win | `E0 5B` | F23 (`0x6E`) |
| Right Win | `E0 5C` | F24 (`0x76`) |
| whichever key you gave the Windows-key role | — | Right Win (`E0 5C`) |

MacKeys generates and writes this itself. Clicking OK in Settings compares the
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
  Options tab, delete the value outright and restore stock behaviour.
- Manual undo: delete the `Scancode Map` value and reboot.

## Migrating from the MX Keys Mini version

Earlier versions hardcoded an MX Keys Mini for Mac layout and kept settings in
`HKCU\Software\MacKeys`. On first run the Caps Lock choice and the two Space
chords carry over into the new file; the cmd/option positions do not, because
they described Mac keycaps that a Windows-layout board doesn't have. Assign the
bottom row in the picker instead.

For reference, what Windows actually received from that board (the Karabiner
config in this repo is Mac-only and never applied here) was, left to right:

| Physical (Mac legend) | Windows saw | Role |
| --- | --- | --- |
| control | Left Ctrl | untouched |
| option | Left Alt | the "option" of option+arrow word jump |
| **command** (touching space) | Left Win → F23 | **Ctrl (Mac cmd)** |
| command (right of space) | Right Win → F24 | nav layer |
| option (rightmost) | Right Alt → Right Win | real Windows key |

The G915 bottom row is Ctrl, **Win**, **Alt** where the MX Keys sent Ctrl,
**Alt**, **Win** — positions 2 and 3 are swapped. A position-faithful port
therefore puts Alt on the Win key and the Ctrl (Mac cmd) role on the Alt key:

```ini
[base]
E05B = key:38             # Win -> Left Alt (the "option" for word jump)
38   = ctrl               # Alt -> Ctrl (Mac cmd), touching space
```

A file predating `[chords]` is upgraded on load: `ctrl_space_start` becomes an
explicit **left** Ctrl + Space chord (the old check merged both Ctrls), and
`cmd_space_lang` follows whichever key holds the Ctrl role. If no key holds
that role yet, the language chord is dropped — add it in the Hotkeys list.

If the old hand-written Scancode Map is still installed, MacKeys will notice it
no longer matches and offer to remove it.

## Build

Requires Visual Studio with the C++ workload.

Key identity, chord text and hotkey capture live in the
[keychord](https://github.com/whatislaaaf/keychord) submodule, shared with
[Monopane](https://github.com/whatislaaaf/monopane-windows), so a fresh clone
needs:

```
git submodule update --init
```

Then:

```
build.bat
```

Produces `build\mackeys.exe`.

## Run

Run `build\mackeys.exe`. It lives in the system tray (right-click for the menu,
double-click for settings):

- **Settings…** — the key picker, the hotkey list, layout, autostart, and the
  Scancode Map status
- **Pause remapping** — temporarily restore stock keyboard behavior
- **Remove kernel Scancode Map…** — the escape hatch described above
- **Exit**

Autostart uses a Task Scheduler logon task named `MacKeys` (the classic Run key
proved unreliable and is staggered by a minute or more at logon). It is created
automatically on first launch; toggling the checkbox in settings shows a UAC
prompt because logon-trigger tasks require elevation.

Only one instance runs at a time. The hook ignores injected input (from remote
desktop tools, AutoHotkey, etc.) and only remaps physical keystrokes. Mappings
apply to every attached keyboard — the hook cannot tell them apart.
