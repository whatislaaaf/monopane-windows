# MacKeys

A tiny Windows tray app that replicates a Karabiner-Elements setup for an
MX Keys Mini for Mac. Native Win32, no dependencies, single exe.

## Mappings

Physical key names below refer to the Mac keycaps; in parentheses is what the
keyboard actually sends to Windows.

| Physical key | Acts as |
| --- | --- |
| Caps Lock | Backspace |
| Left cmd (F23 via Scancode Map) | Left Ctrl — cmd+C / cmd+V feel like Mac |
| Right cmd (F24 via Scancode Map) | Nav layer (swallowed when tapped alone) |
| Right option (Right Win via Scancode Map) | Real Windows key — Win+L, Win+E, snapping (settings can make it a second nav-layer key instead) |
| Left cmd+Space | Win+Space — switches input language, like the Mac input-source switcher (toggleable in settings) |
| Ctrl+Space (physical control key) | Taps the Win key to open the Start menu, like Spotlight (toggleable in settings; costs the raw Ctrl+Space chord used by IDE autocomplete) |

Nav layer (hold either right-side key):

| Key | Sends |
| --- | --- |
| `j` `k` `i` `l` | Left / Down / Up / Right arrow |
| `h` | Home |
| `;` | End |
| `u` | Ctrl+Win+Left — previous virtual desktop |
| `o` | Ctrl+Win+Right — next virtual desktop |

Arrow keys understand Mac-style modifiers, translated to the Windows
equivalents (held Alt/Ctrl are masked as needed so apps see only the chord):

| Combo | Sends | Mac equivalent |
| --- | --- | --- |
| left option + `j`/`l` | Ctrl+Left/Right — word jump | option+←/→ |
| left option + `i`/`k` | Ctrl+Up/Down | option+↑/↓ |
| left cmd + `j`/`l` | Home / End — line start/end | cmd+←/→ |
| left cmd + `i`/`k` | Ctrl+Home/End — document start/end | cmd+↑/↓ |

Shift passes through everywhere, so any of the above becomes a selection
(layer+Shift+`j` = select left), matching Karabiner's `"optional": ["any"]`.

## Win+L and the Scancode Map

`Win+L` is handled by Windows below keyboard hooks (like `Ctrl+Alt+Del`), so
a hook alone cannot stop the physical Win keys (both cmd keys) from locking
the PC when chorded with `l`. The cmd keys are therefore remapped at the
kernel level, and right option becomes the one real Windows key:

| Physical | Scancode | Becomes |
| --- | --- | --- |
| Right cmd | `E0 5C` | F24 (`0x76`) |
| Left cmd | `E0 5B` | F23 (`0x6E`) |
| Right option | `E0 38` | Right Win (`E0 5C`) |

```
HKLM\SYSTEM\CurrentControlSet\Control\Keyboard Layout
  "Scancode Map" = hex: 00 00 00 00 00 00 00 00 04 00 00 00
                        76 00 5C E0 6E 00 5B E0 5C E0 38 E0 00 00 00 00
```

Requires admin to write and a reboot to take effect. The app also accepts the
pre-remap identities (`VK_RWIN` as layer key, `VK_LWIN` as left cmd) so it
works on machines without the Scancode Map. To undo, delete the
`Scancode Map` value and reboot.

## Build

Requires Visual Studio with the C++ workload.

```
build.bat
```

Produces `build\mackeys.exe`.

## Run

Run `build\mackeys.exe`. It lives in the system tray (right-click for the
menu, double-click for settings):

- **Settings…** — toggle each remap (Caps-as-Backspace, left-cmd-as-Ctrl,
  right-option-as-layer) and autostart; persisted under HKCU `Software\MacKeys`
- **Pause remapping** — temporarily restore stock keyboard behavior
- **Exit**

Autostart uses a Task Scheduler logon task named `MacKeys` (the classic Run
key proved unreliable and is staggered by a minute or more at logon). It is
created automatically on first launch; toggling the checkbox in settings
shows a UAC prompt because logon-trigger tasks require elevation.

Only one instance runs at a time. The hook ignores injected input (from
remote desktop tools, AutoHotkey, etc.) and only remaps physical keystrokes.
The remapping applies to every attached keyboard, not just the MX Keys.
