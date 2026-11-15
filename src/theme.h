#pragma once
#include <windows.h>

// The switcher's chrome, after the Windows 11 Start menu's search: a header
// band holding a pill-shaped search box, a hairline under it, and a lighter
// body below with a section heading over the list. The colours are the Start
// menu's own in dark mode, read off its pixels.

// Palette.
constexpr COLORREF CLR_HEADER_BG = RGB(33, 33, 33);     // the band the search box sits in
constexpr COLORREF CLR_DIVIDER = RGB(29, 29, 29);       // the hairline under that band
constexpr COLORREF CLR_BG = RGB(41, 41, 41);            // the body, behind the list
constexpr COLORREF CLR_SEARCH_BG = RGB(31, 31, 31);     // inside the search box
constexpr COLORREF CLR_SEARCH_BORDER = RGB(50, 50, 50); // its 1px outline
constexpr COLORREF CLR_ROW_SELECTED = RGB(55, 55, 55);  // a white wash of ~6% over CLR_BG
constexpr COLORREF CLR_TEXT = RGB(255, 255, 255);
constexpr COLORREF CLR_TEXT_DIM = RGB(200, 200, 200);   // window titles: secondary text
constexpr COLORREF CLR_TEXT_HINT = RGB(150, 150, 150);  // the search box's placeholder

// The light shade of the system accent that Windows 11 uses on dark surfaces
// (the selection pill, the search glyph). The default blue's is #4CC2FF.
constexpr COLORREF CLR_ACCENT_FALLBACK = RGB(76, 194, 255);

// Base (96-dpi) metrics of the panel itself.
constexpr int BASE_PANEL_WIDTH = 680;
constexpr int BASE_HEADER_H = 68;    // the band: search box plus 16px above and below
constexpr int BASE_SEARCH_INSET = 16;
constexpr int BASE_SEARCH_BOX_H = 36;
constexpr int BASE_SECTION_H = 40;   // the "Open windows" heading
constexpr int BASE_BOTTOM_PAD = 8;
constexpr int BASE_ROW_INSET = 8;    // a row's highlight, in from the panel's sides
constexpr int BASE_ROW_RADIUS = 4;   // what Windows 11 rounds list items by

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

// How many rows of `rowH` fit below the top `listTop` pixels of a panel at
// `top`, leaving `bottomPad` under them, without running off the bottom of
// the work area. At least one, however small the screen.
inline int RowsThatFit(const RECT& work, int top, int listTop, int bottomPad, int rowH)
{
    if (rowH <= 0)
        return 1;
    const int room = work.bottom - top - listTop - bottomPad;
    return room / rowH < 1 ? 1 : room / rowH;
}
