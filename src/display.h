#pragma once
#include <windows.h>

// Display orientation, in the DEVMODE dmDisplayOrientation values. Odd values
// are the portrait ones, which is what tells the code below when the pixel
// dimensions have to be swapped along with the rotation.
enum class Orientation : DWORD {
    Landscape = DMDO_DEFAULT,        // 0
    Portrait = DMDO_90,              // 1
    LandscapeFlipped = DMDO_180,     // 2
    PortraitFlipped = DMDO_270,      // 3
};

// The orientation of the monitor the cursor is on.
Orientation CurrentOrientation();

// Rotates the monitor the cursor is on to the next of Landscape → Portrait →
// Portrait (flipped) → Landscape. Landscape (flipped) is not part of the cycle,
// but a monitor already in it rotates on to Landscape.
//
// Returns false if the mode change was refused, in which case nothing moved.
bool CycleOrientation();

// "Landscape", "Portrait", "Portrait (flipped)" — for the tray tooltip and
// anything else that wants to say where the screen ended up.
const wchar_t* OrientationName(Orientation o);
