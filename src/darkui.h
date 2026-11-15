#pragma once
#include <windows.h>

// The dark look of the settings dialog, after the Windows 11 Settings app.
//
// Win32 dialogs have no dark mode of their own: the frame, the statics and the
// lists take whatever brush the dialog procedure hands back, but checkboxes,
// radios, group boxes and push buttons are painted by the visual-styles
// theme, which only comes in light. Those are drawn here instead, through the
// custom-draw notification every themed button sends before painting itself.
// Combo boxes are painted from a subclass, and scrollbars take the theme's
// dark variant.
//
// A group box is drawn the way Settings groups its options: its caption as a
// heading, and under it a card the controls sit on. The cards are painted
// with the page's background, so every control inside one is given the card's
// colour to paint on instead of the page's.
//
// Every dialog procedure in the settings window calls Handle() first and
// returns its result when it says so, and calls Apply() once on WM_INITDIALOG.

namespace dark {

// Windows 11's dark-mode surfaces, as the Settings app layers them.
constexpr COLORREF kBg = RGB(32, 32, 32);          // the window and the pages
constexpr COLORREF kCard = RGB(43, 43, 43);        // a card of settings
constexpr COLORREF kCardLine = RGB(29, 29, 29);    // its 1px outline
constexpr COLORREF kControl = RGB(55, 55, 55);     // buttons and combos at rest
constexpr COLORREF kControlHot = RGB(60, 60, 60);
constexpr COLORREF kControlDown = RGB(50, 50, 50);
constexpr COLORREF kControlLine = RGB(64, 64, 64);
constexpr COLORREF kList = RGB(36, 36, 36);        // a list set into a card
constexpr COLORREF kLine = RGB(70, 70, 70);        // separators
constexpr COLORREF kText = RGB(255, 255, 255);
constexpr COLORREF kDim = RGB(200, 200, 200);      // secondary text
constexpr COLORREF kDisabled = RGB(120, 120, 120);
constexpr COLORREF kOnAccent = RGB(0, 0, 0);       // text on an accent fill, in dark mode

// The deep shade of the accent, which the key picker marks bound keys with:
// it carries white text, which the light shade below does not.
constexpr COLORREF kAccent = RGB(0, 95, 184);

// The light shade of the user's accent, which Windows 11 fills primary
// buttons and checked boxes with on dark surfaces. Read once per dialog.
COLORREF Accent();

HBRUSH BgBrush();
HBRUSH CardBrush();

// Dark title bar blending into the page, Windows 11's font on every child,
// the dark theme variants for the children that have one.
void Apply(HWND dlg);

// Colours for WM_CTLCOLOR*, the cards behind the group boxes, and the custom
// drawing for buttons. Returns true when the message was handled, with the
// value the dialog procedure should return in `result`.
bool Handle(HWND dlg, UINT msg, WPARAM wParam, LPARAM lParam, INT_PTR& result);

// Pixels for a 96-dpi size, at the window's DPI.
int Px(HWND hwnd, int at96);

// A rounded rectangle, filled and outlined.
void RoundBox(HDC dc, const RECT& rc, int radius, COLORREF fill, COLORREF line,
              int lineWidth = 1);

} // namespace dark
