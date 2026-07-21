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

## Original Karabiner-Elements config (Mac)

MacKeys replicates this `karabiner.json` (Karabiner-Elements on macOS) —
copy it to `~/.config/karabiner/karabiner.json` if you want the same setup
on a Mac. The nav layer lives on `right_command` there too; the
device-specific `simple_modifications` blocks rearrange cmd/option per
keyboard and won't apply to other hardware (they match on vendor/product id).

```json
{
    "global": { "show_in_menu_bar": false },
    "profiles": [
        {
            "complex_modifications": {
                "rules": [
                    {
                        "description": "Right command and ijkl + home/end",
                        "manipulators": [
                            {
                                "from": {
                                    "key_code": "h",
                                    "modifiers": {
                                        "mandatory": ["right_command"],
                                        "optional": ["any"]
                                    }
                                },
                                "to": [{ "key_code": "home" }],
                                "type": "basic"
                            },
                            {
                                "from": {
                                    "key_code": "semicolon",
                                    "modifiers": {
                                        "mandatory": ["right_command"],
                                        "optional": ["any"]
                                    }
                                },
                                "to": [{ "key_code": "end" }],
                                "type": "basic"
                            },
                            {
                                "from": {
                                    "key_code": "j",
                                    "modifiers": {
                                        "mandatory": ["right_command"],
                                        "optional": ["any"]
                                    }
                                },
                                "to": [{ "key_code": "left_arrow" }],
                                "type": "basic"
                            },
                            {
                                "from": {
                                    "key_code": "k",
                                    "modifiers": {
                                        "mandatory": ["right_command"],
                                        "optional": ["any"]
                                    }
                                },
                                "to": [{ "key_code": "down_arrow" }],
                                "type": "basic"
                            },
                            {
                                "from": {
                                    "key_code": "i",
                                    "modifiers": {
                                        "mandatory": ["right_command"],
                                        "optional": ["any"]
                                    }
                                },
                                "to": [{ "key_code": "up_arrow" }],
                                "type": "basic"
                            },
                            {
                                "from": {
                                    "key_code": "l",
                                    "modifiers": {
                                        "mandatory": ["right_command"],
                                        "optional": ["any"]
                                    }
                                },
                                "to": [{ "key_code": "right_arrow" }],
                                "type": "basic"
                            }
                        ]
                    },
                    {
                        "manipulators": [
                            {
                                "description": "moving between workspaces left",
                                "from": {
                                    "key_code": "u",
                                    "modifiers": {
                                        "mandatory": ["right_command"],
                                        "optional": ["any"]
                                    }
                                },
                                "to": [
                                    {
                                        "key_code": "left_arrow",
                                        "modifiers": ["left_control"]
                                    }
                                ],
                                "type": "basic"
                            },
                            {
                                "description": "moving between workspaces right",
                                "from": {
                                    "key_code": "o",
                                    "modifiers": {
                                        "mandatory": ["right_command"],
                                        "optional": ["any"]
                                    }
                                },
                                "to": [
                                    {
                                        "key_code": "right_arrow",
                                        "modifiers": ["left_control"]
                                    }
                                ],
                                "type": "basic"
                            }
                        ]
                    }
                ]
            },
            "devices": [
                {
                    "identifiers": {
                        "is_keyboard": true,
                        "product_id": 64112,
                        "vendor_id": 9639
                    },
                    "simple_modifications": [
                        {
                            "from": { "key_code": "left_command" },
                            "to": [{ "key_code": "right_option" }]
                        },
                        {
                            "from": { "key_code": "left_option" },
                            "to": [{ "key_code": "left_command" }]
                        },
                        {
                            "from": { "key_code": "right_command" },
                            "to": [{ "key_code": "right_option" }]
                        },
                        {
                            "from": { "key_code": "right_option" },
                            "to": [{ "key_code": "right_command" }]
                        }
                    ]
                },
                {
                    "identifiers": {
                        "is_keyboard": true,
                        "product_id": 50504,
                        "vendor_id": 1133
                    },
                    "simple_modifications": [
                        {
                            "from": { "key_code": "right_option" },
                            "to": [{ "key_code": "right_command" }]
                        },
                        {
                            "from": { "key_code": "left_option" },
                            "to": [{ "key_code": "left_command" }]
                        },
                        {
                            "from": { "key_code": "left_command" },
                            "to": [{ "key_code": "left_option" }]
                        }
                    ]
                }
            ],
            "fn_function_keys": [
                {
                    "from": { "key_code": "f3" },
                    "to": [{ "key_code": "mission_control" }]
                },
                {
                    "from": { "key_code": "f4" },
                    "to": [{ "key_code": "launchpad" }]
                },
                {
                    "from": { "key_code": "f5" },
                    "to": [{ "key_code": "illumination_decrement" }]
                },
                {
                    "from": { "key_code": "f6" },
                    "to": [{ "key_code": "illumination_increment" }]
                },
                {
                    "from": { "key_code": "f9" },
                    "to": [{ "consumer_key_code": "fastforward" }]
                }
            ],
            "name": "Default profile",
            "selected": true,
            "simple_modifications": [
                {
                    "from": { "key_code": "caps_lock" },
                    "to": [{ "key_code": "delete_or_backspace" }]
                }
            ],
            "virtual_hid_keyboard": {
                "country_code": 0,
                "keyboard_type_v2": "iso"
            }
        }
    ]
}
```

Differences between this config and MacKeys, in both directions:

- MacKeys adds Mac-style editing chords inside the layer (option = word jump,
  cmd = line/document jump) — on the Mac these come from the OS itself, so
  the Karabiner config doesn't need them.
- MacKeys adds left cmd+Space (input language) and Ctrl+Space (Start menu);
  the Mac equivalents are system shortcuts, not Karabiner rules.
- The `fn_function_keys` block (Mission Control, Launchpad, illumination) has
  no Windows equivalent and is not replicated.

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
