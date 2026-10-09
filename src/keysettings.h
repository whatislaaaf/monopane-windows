#pragma once
#include <windows.h>

#include "config.h"

// The two Settings tabs that belong to the remapper: the key picker, and the
// hotkey list with the keyboard-layout and Scancode Map options beside it.
// Both edit one copy of g_config, which the host dialog commits on OK.

struct KeysEdit;

KeysEdit* BeginKeysEdit(); // a copy of g_config to edit
HWND CreateKeysTab(HWND host, HINSTANCE instance, KeysEdit* edit);
HWND CreateKeyOptionsTab(HWND host, HINSTANCE instance, KeysEdit* edit);

// On OK: stores the edit into g_config, saves keys.ini, hands the hook its new
// tables and offers to write the Scancode Map if the config now calls for one.
void CommitKeysEdit(HWND owner, KeysEdit* edit);
// Frees the edit, cancelling any capture still in flight.
void EndKeysEdit(KeysEdit* edit);

// True while the hotkey list is capturing a chord. Its chords are matched
// against physical keys before any remapping, so the hook feeds that capture
// raw keys; the switcher's captures are fed after the remap stage instead.
bool KeysChordCaptureArmed();

// Compares the installed kernel Scancode Map with the one g_config implies
// and, if they differ, offers to write or remove it. Returns true when a
// reboot is now pending.
bool SyncScancodeMap(HWND owner);
