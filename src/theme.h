#pragma once
#include <windows.h>

// The chrome the switcher and the launchpad share, after the Windows 11 Start
// menu: a header band holding a pill-shaped search box, a hairline under it,
// and a lighter body below with a section heading over its contents. The
// colours are the Start menu's own in dark mode, read off its pixels.
//
// The two are one panel with two kinds of contents, and switching between
// them should not look like moving between two different windows: same
// width, same search box, in the same place on screen. What differs is only
// what hangs below the search box — a list of windows, or a grid of apps.

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

// Base (96-dpi) metrics, on a 4px grid, and the same in both panels. Two
// vertical lines run down each panel:
//
//   16px in: the search box, the edge of every highlight (a selected row, a
//            selected tile), and the launchpad's "Pinned" heading and Edit
//            button, which line up with its tiles' highlights;
//   32px in: in the switcher, the section heading and each row's icon.
//
// Down the panel: the 64px header band (a 32px search box with 16px above and
// below), the section heading (a 40px band in the switcher; the launchpad's
// is taller, to hold its Edit button), the rows or tiles, then
// 16px to the bottom, as much as the highlight keeps off the sides. A
// highlight stands 2px in from the top and bottom of its row or tile (and the
// sides of a tile), so neighbouring highlights are 4px apart either way.
//
// 668 is the launchpad's eight 80px columns, plus 14px each side so the
// first and last tiles' highlights land on the 16px line.
constexpr int BASE_PANEL_WIDTH = 668;
constexpr int BASE_HEADER_H = 64;
constexpr int BASE_SEARCH_BOX_H = 32;
constexpr int BASE_SEARCH_INSET = 16;     // panel side to the search box
constexpr int BASE_SECTION_H = 40;
constexpr int BASE_HIGHLIGHT_INSET = 16;  // panel side to a highlight's edge
constexpr int BASE_CONTENT_INSET = 32;    // panel side to content
constexpr int BASE_HIGHLIGHT_GAP = 2;     // a highlight, in from its row or tile
constexpr int BASE_BOTTOM_PAD = 16;
constexpr int BASE_SCREEN_MARGIN = 24;    // the least a panel keeps off the screen's bottom
constexpr int BASE_ROW_RADIUS = 4;        // what Windows 11 rounds list items by

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
