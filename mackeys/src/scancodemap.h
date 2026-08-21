#pragma once
#include <windows.h>

#include <string>
#include <vector>

#include "config.h"
#include "keys.h"

// The kernel-level Scancode Map exists for one reason: Win+L is handled below
// keyboard hooks, so a hook cannot stop a physical Win key from locking the PC
// when it is chorded. Any Win key given a non-Windows job is therefore diverted
// onto a spare scancode before Windows sees it. Nothing else needs the map, so
// on a board whose layer key is not a Win key the plan comes out empty and the
// value is deleted.

struct Divert {
    KeyId from; // what the hardware sends
    KeyId to;   // what Windows will see instead
};

struct ScancodeMapPlan {
    // Empty means the registry value should be absent, not that nothing is
    // planned — deleting it is a valid plan.
    std::vector<BYTE> blob;
    std::vector<Divert> diverts;
    std::vector<std::wstring> notes;
    // More keys wanted diverting than there are spare scancodes, or more than
    // one key claimed the real Windows key. The extras are dropped.
    bool overflow = false;
};

ScancodeMapPlan BuildScancodeMap(const Config& cfg);

// Structural and whitelist checks. A malformed map applies at boot, before
// anything clickable exists, so nothing is written without passing this.
bool ValidateScancodeMap(const std::vector<BYTE>& blob);

bool ReadCurrentScancodeMap(std::vector<BYTE>& out);
bool ScancodeMapIsCurrent(const ScancodeMapPlan& plan);

// Writes or deletes HKLM\...\Keyboard Layout\Scancode Map through an elevated
// reg.exe, so each call raises one UAC prompt. Takes effect on reboot.
bool ApplyScancodeMap(const std::vector<BYTE>& blob);
bool ResetScancodeMap();

// A map is installed that this config did not ask for — e.g. the hand-written
// MX Keys map from before the app owned this key.
bool ForeignScancodeMapPresent(const ScancodeMapPlan& plan);
