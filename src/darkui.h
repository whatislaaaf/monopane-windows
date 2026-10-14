#pragma once
#include <windows.h>

#include "theme.h"

// The dark look of the settings dialog.
//
// Win32 dialogs have no dark mode of their own: the frame, the statics and the
// lists take whatever brush the dialog procedure hands back, but checkboxes,
// radios, group boxes and push buttons are painted by the visual-styles
// theme, which only comes in light. Those are drawn here instead, through the
// custom-draw notification every themed button sends before painting itself.
// Combo boxes and scrollbars have dark variants of the theme, and get those.
//
// Every dialog procedure in the settings window calls Handle() first and
// returns its result when it says so, and calls Apply() once on WM_INITDIALOG.

namespace dark {

constexpr COLORREF kBg = CLR_BG;             // the dialog and the pages
constexpr COLORREF kCard = CLR_SEARCH_BG;    // lists, combos, buttons at rest
constexpr COLORREF kLine = RGB(70, 70, 70);  // borders and separators
constexpr COLORREF kText = CLR_TEXT;
constexpr COLORREF kDim = CLR_TEXT_DIM;
constexpr COLORREF kAccent = CLR_SELECTION;

HBRUSH BgBrush();
HBRUSH CardBrush();

// Dark title bar, and the dark theme variants for the children that have one.
void Apply(HWND dlg);

// Colours for WM_CTLCOLOR*, and the custom drawing for buttons. Returns true
// when the message was handled, with the value the dialog procedure should
// return in `result`.
bool Handle(HWND dlg, UINT msg, WPARAM wParam, LPARAM lParam, INT_PTR& result);

// Pixels for a 96-dpi size, at the window's DPI.
int Px(HWND hwnd, int at96);

// A rounded rectangle, filled and outlined.
void RoundBox(HDC dc, const RECT& rc, int radius, COLORREF fill, COLORREF line,
              int lineWidth = 1);

} // namespace dark
