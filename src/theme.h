#pragma once
#include <windows.h>

// The switcher's chrome.

// Palette.
constexpr COLORREF CLR_BG = RGB(30, 30, 30);
constexpr COLORREF CLR_SEARCH_BG = RGB(45, 45, 45);
constexpr COLORREF CLR_SELECTION = RGB(0, 95, 184);
constexpr COLORREF CLR_TEXT = RGB(242, 242, 242);
constexpr COLORREF CLR_TEXT_DIM = RGB(170, 170, 170);

// Base (96-dpi) metrics of the panel itself.
constexpr int BASE_PANEL_WIDTH = 680;
constexpr int BASE_SEARCH_H = 56;
constexpr int BASE_PAD = 10;

// What Windows 11 rounds a window's own corners by, so the search box can be
// rounded to match the frame around it.
constexpr int BASE_CORNER = 8;

// Where to put the top of a panel whose search plate is `plate`, so that the
// plate's middle lands on the middle of `work`.
//
// The panel anchors on the search box rather than on the frame, because the
// search box is the part that has to hold still: the frame is a different
// height every time the list filters down.
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
