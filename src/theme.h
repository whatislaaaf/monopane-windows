#pragma once
#include <windows.h>

// The chrome the switcher and the launchpad share.
//
// They are one panel with two kinds of contents, and switching between them
// should not look like moving between two different windows: same width, same
// search box, in the same place on screen. What differs is only what hangs
// below the search box — a list of windows, or a grid of apps.

// Palette.
constexpr COLORREF CLR_BG = RGB(30, 30, 30);
constexpr COLORREF CLR_SEARCH_BG = RGB(45, 45, 45);
constexpr COLORREF CLR_SELECTION = RGB(0, 95, 184);
constexpr COLORREF CLR_TEXT = RGB(242, 242, 242);
constexpr COLORREF CLR_TEXT_DIM = RGB(170, 170, 170);

// Base (96-dpi) metrics of the panel itself. The launchpad's grid is measured
// out of the width rather than the other way round, so the two panels are the
// same number of pixels wide at every DPI.
constexpr int BASE_PANEL_WIDTH = 680;
constexpr int BASE_SEARCH_H = 56;
constexpr int BASE_PAD = 10;

// What Windows 11 rounds a window's own corners by, so the search box can be
// rounded to match the frame around it.
constexpr int BASE_CORNER = 8;

// Where to put the top of a panel whose search plate is `plate`, so that the
// plate's middle lands on the middle of `work`.
//
// Both overlays anchor on the search box rather than on the frame, because the
// search box is the part that has to hold still: the frame is a different
// height in each of them, and a different height again as a list filters down
// or a grid grows. Anchoring the frame instead is what used to make the box
// jump 76 pixels between the two.
inline int PanelTop(const RECT& work, const RECT& plate)
{
    return work.top + (work.bottom - work.top) / 2 - (plate.top + plate.bottom) / 2;
}

// How many rows of `rowH` fit below that search plate without running off the
// bottom of the work area. At least one, however small the screen.
inline int RowsThatFit(const RECT& work, int top, int searchH, int pad, int rowH)
{
    if (rowH <= 0)
        return 1;
    const int room = work.bottom - top - searchH - pad;
    return room / rowH < 1 ? 1 : room / rowH;
}
