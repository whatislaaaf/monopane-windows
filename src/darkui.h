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

// ------------------------------------------------------------------ layout
//
// The settings pages are laid out in code, in pixels on Windows 11's 4px
// grid, rather than in the template's dialog units: a dialog unit is a
// fraction of the font's size, so no two gaps come out the same and none
// lands on the grid. The template only says which controls exist. The values
// are Microsoft's spacing guidance for Windows apps, in 96-dpi pixels.

constexpr int kMargin = 24;     // window edge to content, all round
constexpr int kHeadingGap = 8;  // a section heading to its card (header to control)
constexpr int kCardPad = 16;    // a card's edge to what is on it (surface to edge text)
constexpr int kRowH = 32;       // a row with a button or a combo box: their height
constexpr int kCheckH = 24;     // a row with a checkbox or radio button
constexpr int kGap = 8;         // between buttons, and between rows
constexpr int kLabelGap = 12;   // a control to its label or its description
constexpr int kSectionGap = 24; // a card to the next section's heading
constexpr int kButtonW = 120;   // the narrowest a push button gets

// Drawn in the secondary text colour: descriptions and hints.
void SetSecondary(HWND ctl);

// Measurements in the control's own font.
int LineHeight(HWND ctl);
int TextWidth(HWND ctl);
int TextHeight(HWND ctl, int width);  // its text wrapped to `width`
int ButtonWidth(HWND button);         // its text plus padding, at least kButtonW
int CheckWidth(HWND button);          // the box, the gap and its text

// The first child whose text is or starts with `text`, for controls the
// template gives no ID.
HWND ChildByText(HWND parent, const wchar_t* text, bool prefix = false);

void Place(HWND ctl, int x, int y, int w, int h);
// One line of text, centred down a row of `rowH`.
void PlaceText(HWND ctl, int x, int rowY, int w, int rowH);
// A drop-down combo box whose closed plate is `h` tall.
void PlaceCombo(HWND combo, int x, int y, int w, int h);

// Stacks sections down a page: a heading, a card, the next heading. Each
// section is a group box, which is sized here to cover its heading and card;
// the controls placed between BeginSection and EndSection go on the card,
// between inL and inR.
class Flow {
public:
    explicit Flow(HWND page);
    int Px(int at96) const;

    void BeginSection(HWND groupBox);
    void EndSection();
    // Down to the end of the last thing laid out, with no gap after it.
    int Height() const;

    int y = 0;
    int left = 0, right = 0;  // the page's content edges
    int inL = 0, inR = 0;     // the current card's, or the page's between sections

private:
    HWND page_;
    HWND group_ = nullptr;
    int headingTop_ = 0;
    bool afterSection_ = false;
};

// A section heading's height and the gap under it: where its card starts.
int HeadingBlock(HWND groupBox);

} // namespace dark
