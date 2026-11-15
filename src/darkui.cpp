#include "darkui.h"

#include <commctrl.h>
#include <dwmapi.h>
#include <uxtheme.h>

#include <algorithm>
#include <string>
#include <vector>

#include "paint.h"

namespace dark {
namespace {

HBRUSH g_bgBrush = nullptr;
HBRUSH g_cardBrush = nullptr;
HBRUSH g_listBrush = nullptr;
COLORREF g_accent = RGB(76, 194, 255);

// Windows 11 rounds controls and the cards they sit on by 4px.
constexpr int kRadius = 4;

// What SetSecondary leaves in a static's GWLP_USERDATA, which statics do not
// use themselves.
constexpr LONG_PTR kSecondaryMark = 0x4D53'4543;  // "MSEC"

bool IsSecondary(HWND ctl)
{
    return GetWindowLongPtrW(ctl, GWLP_USERDATA) == kSecondaryMark;
}

COLORREF Blend(COLORREF a, COLORREF b, int pct)
{
    return RGB((GetRValue(a) * (100 - pct) + GetRValue(b) * pct) / 100,
               (GetGValue(a) * (100 - pct) + GetGValue(b) * pct) / 100,
               (GetBValue(a) * (100 - pct) + GetBValue(b) * pct) / 100);
}

bool ClassIs(HWND hwnd, const wchar_t* name)
{
    wchar_t cls[32] = {};
    GetClassNameW(hwnd, cls, 32);
    return lstrcmpiW(cls, name) == 0;
}

bool IsGroupBox(HWND hwnd)
{
    return ClassIs(hwnd, L"Button") && (GetWindowLongW(hwnd, GWL_STYLE) & BS_TYPEMASK) == BS_GROUPBOX;
}

std::wstring Text(HWND hwnd)
{
    wchar_t buf[256] = {};
    GetWindowTextW(hwnd, buf, 256);
    return buf;
}

// ------------------------------------------------------------------- fonts

// Fonts made here live as long as the process: a handful of sizes at most,
// shared by every dialog that asks for the same one.
struct CachedFont {
    LONG height;
    LONG weight;
    bool glyph;
    HFONT font;
};
std::vector<CachedFont> g_fonts;

HFONT CachedUiFont(LONG height, LONG weight, bool glyph)
{
    for (const CachedFont& f : g_fonts)
        if (f.height == height && f.weight == weight && f.glyph == glyph)
            return f.font;

    static const bool variable = FontInstalled(L"Segoe UI Variable Text");
    static const bool fluent = FontInstalled(L"Segoe Fluent Icons");
    LOGFONTW lf{};
    lf.lfHeight = height;
    lf.lfWeight = weight;
    lf.lfCharSet = DEFAULT_CHARSET;
    lf.lfQuality = CLEARTYPE_QUALITY;
    wcscpy_s(lf.lfFaceName, glyph      ? (fluent ? L"Segoe Fluent Icons" : L"Segoe MDL2 Assets")
                            : variable ? L"Segoe UI Variable Text"
                                       : L"Segoe UI");
    HFONT font = CreateFontIndirectW(&lf);
    g_fonts.push_back({ height, weight, glyph, font });
    return font;
}

LONG FontHeight(HWND hwnd)
{
    LOGFONTW lf{};
    HFONT font = reinterpret_cast<HFONT>(SendMessageW(hwnd, WM_GETFONT, 0, 0));
    if (!font || !GetObjectW(font, sizeof(lf), &lf) || lf.lfHeight == 0)
        return -Px(hwnd, 12);
    return lf.lfHeight;
}

// The control's own size in Windows 11's face, at the given weight.
HFONT UiFont(HWND hwnd, LONG weight)
{
    return CachedUiFont(FontHeight(hwnd), weight, false);
}

// ------------------------------------------------------------------- cards

// The heading a group box's caption becomes, and the card under it, in the
// parent's client coordinates.
int HeadingHeight(HWND groupBox)
{
    HDC dc = GetDC(groupBox);
    HGDIOBJ old = SelectObject(dc, UiFont(groupBox, FW_SEMIBOLD));
    TEXTMETRICW tm{};
    GetTextMetricsW(dc, &tm);
    SelectObject(dc, old);
    ReleaseDC(groupBox, dc);
    return tm.tmHeight;
}

RECT GroupRect(HWND groupBox)
{
    RECT rc;
    GetWindowRect(groupBox, &rc);
    MapWindowPoints(nullptr, GetParent(groupBox), reinterpret_cast<POINT*>(&rc), 2);
    return rc;
}

RECT CardRect(HWND groupBox)
{
    RECT rc = GroupRect(groupBox);
    rc.top += HeadingBlock(groupBox);
    return rc;
}

// What a control paints its background with: the card's colour when it sits
// on one, the page's otherwise.
COLORREF BackOf(HWND ctl)
{
    HWND parent = GetParent(ctl);
    RECT rc;
    GetWindowRect(ctl, &rc);
    MapWindowPoints(nullptr, parent, reinterpret_cast<POINT*>(&rc), 2);
    const POINT centre{ (rc.left + rc.right) / 2, (rc.top + rc.bottom) / 2 };
    for (HWND s = GetWindow(parent, GW_CHILD); s; s = GetWindow(s, GW_HWNDNEXT)) {
        if (s == ctl || !IsGroupBox(s) || !IsWindowVisible(s))
            continue;
        const RECT card = CardRect(s);
        if (PtInRect(&card, centre))
            return kCard;
    }
    return kBg;
}

// The page, then each group box's heading and card on it. The group boxes
// themselves paint nothing; the controls inside them are siblings painted
// after this, on top of the card.
void PaintBackground(HWND dlg, HDC dc)
{
    RECT client;
    GetClientRect(dlg, &client);
    FillRect(dc, &client, BgBrush());

    SetBkMode(dc, TRANSPARENT);
    for (HWND gb = GetWindow(dlg, GW_CHILD); gb; gb = GetWindow(gb, GW_HWNDNEXT)) {
        if (!IsGroupBox(gb) || !IsWindowVisible(gb))
            continue;
        const RECT group = GroupRect(gb);
        const RECT card = CardRect(gb);

        HGDIOBJ oldFont = SelectObject(dc, UiFont(gb, FW_SEMIBOLD));
        SetTextColor(dc, kText);
        RECT heading{ group.left, group.top, group.right, card.top };
        DrawTextW(dc, Text(gb).c_str(), -1, &heading, DT_LEFT | DT_TOP | DT_SINGLELINE | DT_NOPREFIX);
        SelectObject(dc, oldFont);

        OutlineRoundRectAA(dc, card, Px(gb, kRadius), kCard, kCardLine, kBg);
    }
}

LRESULT CALLBACK GroupBoxProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam,
                              UINT_PTR id, DWORD_PTR)
{
    switch (msg) {
    case WM_PAINT:
        // Drawn by the parent, under everything; see PaintBackground.
        ValidateRect(hwnd, nullptr);
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_NCDESTROY:
        RemoveWindowSubclass(hwnd, GroupBoxProc, id);
        break;
    }
    return DefSubclassProc(hwnd, msg, wParam, lParam);
}

// -------------------------------------------------------------- combo box

// A drop-down list combo box: the plate with the current choice and a chevron.
// The control's own look is the one from two decades ago whichever theme it
// wears, so the closed box is painted here from a subclass; the list it drops
// is a window of its own and keeps the dark theme's.
void PaintCombo(HWND hwnd)
{
    PAINTSTRUCT ps;
    HDC dc = BeginPaint(hwnd, &ps);
    RECT rc;
    GetClientRect(hwnd, &rc);
    const COLORREF back = BackOf(hwnd);
    HBRUSH backBrush = CreateSolidBrush(back);
    FillRect(dc, &rc, backBrush);
    DeleteObject(backBrush);

    const bool disabled = !IsWindowEnabled(hwnd);
    const bool open = SendMessageW(hwnd, CB_GETDROPPEDSTATE, 0, 0) != 0;
    const bool focused = GetFocus() == hwnd;
    POINT cursor;
    GetCursorPos(&cursor);
    const bool hot = !disabled && WindowFromPoint(cursor) == hwnd;

    const COLORREF fill = disabled ? back : open ? kControlDown : hot ? kControlHot : kControl;
    OutlineRoundRectAA(dc, rc, Px(hwnd, kRadius), fill, kControlLine, back);
    // Focus shows as the accent along the bottom edge, as a Windows 11 field
    // does.
    if (focused || open) {
        HBRUSH accent = CreateSolidBrush(Accent());
        const RECT bar{ rc.left + Px(hwnd, kRadius), rc.bottom - Px(hwnd, 2),
                        rc.right - Px(hwnd, kRadius), rc.bottom };
        FillRect(dc, &bar, accent);
        DeleteObject(accent);
    }

    HGDIOBJ oldFont = SelectObject(dc, reinterpret_cast<HFONT>(SendMessageW(hwnd, WM_GETFONT, 0, 0)));
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, disabled ? kDisabled : kText);
    const LRESULT sel = SendMessageW(hwnd, CB_GETCURSEL, 0, 0);
    wchar_t text[128] = {};
    if (sel >= 0 && SendMessageW(hwnd, CB_GETLBTEXTLEN, sel, 0) < 128)
        SendMessageW(hwnd, CB_GETLBTEXT, sel, reinterpret_cast<LPARAM>(text));
    RECT tr{ rc.left + Px(hwnd, 10), rc.top, rc.right - Px(hwnd, 28), rc.bottom };
    DrawTextW(dc, text, -1, &tr, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);

    // The chevron, Windows 11's own glyph.
    RECT chevron{ rc.right - Px(hwnd, 28), rc.top, rc.right - Px(hwnd, 8), rc.bottom };
    SelectObject(dc, CachedUiFont(-Px(hwnd, 10), FW_NORMAL, true));
    SetTextColor(dc, disabled ? kDisabled : kDim);
    DrawTextW(dc, L"\xE70D", -1, &chevron, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

    SelectObject(dc, oldFont);
    EndPaint(hwnd, &ps);
}

LRESULT CALLBACK ComboProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam, UINT_PTR id, DWORD_PTR)
{
    switch (msg) {
    case WM_PAINT:
        PaintCombo(hwnd);
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_MOUSEMOVE: {
        // Hover is shown by repainting on the way in and the way out.
        TRACKMOUSEEVENT tme{ sizeof(tme), TME_LEAVE, hwnd, 0 };
        TrackMouseEvent(&tme);
        InvalidateRect(hwnd, nullptr, FALSE);
        break;
    }
    case WM_MOUSELEAVE:
    case WM_SETFOCUS:
    case WM_KILLFOCUS:
        InvalidateRect(hwnd, nullptr, FALSE);
        break;
    case WM_NCDESTROY:
        RemoveWindowSubclass(hwnd, ComboProc, id);
        break;
    }
    return DefSubclassProc(hwnd, msg, wParam, lParam);
}

// ----------------------------------------------------------------- buttons

// Everything a themed Button control would have painted light: the box, the
// dot or the plate, and the text. CDRF_SKIPDEFAULT then keeps the theme's own
// painting off.
LRESULT DrawButton(const NMCUSTOMDRAW& cd)
{
    if (cd.dwDrawStage != CDDS_PREPAINT)
        return CDRF_DODEFAULT;

    HWND hwnd = cd.hdr.hwndFrom;
    HDC dc = cd.hdc;
    RECT rc;
    GetClientRect(hwnd, &rc);
    const LONG type = GetWindowLongW(hwnd, GWL_STYLE) & BS_TYPEMASK;
    const bool disabled = !IsWindowEnabled(hwnd);
    const bool hot = (cd.uItemState & CDIS_HOT) != 0;
    const bool pressed = (cd.uItemState & CDIS_SELECTED) != 0;
    const bool focused = (cd.uItemState & CDIS_FOCUS) != 0 &&
                         !(SendMessageW(hwnd, WM_QUERYUISTATE, 0, 0) & UISF_HIDEFOCUS);
    const std::wstring text = Text(hwnd);
    const COLORREF back = BackOf(hwnd);
    const COLORREF accent = Accent();

    HGDIOBJ oldFont = SelectObject(dc, reinterpret_cast<HFONT>(SendMessageW(hwnd, WM_GETFONT, 0, 0)));
    SetBkMode(dc, TRANSPARENT);
    HBRUSH backBrush = CreateSolidBrush(back);
    FillRect(dc, &rc, backBrush);
    DeleteObject(backBrush);

    switch (type) {
    case BS_CHECKBOX:
    case BS_AUTOCHECKBOX:
    case BS_3STATE:
    case BS_AUTO3STATE:
    case BS_RADIOBUTTON:
    case BS_AUTORADIOBUTTON: {
        const bool radio = type == BS_RADIOBUTTON || type == BS_AUTORADIOBUTTON;
        const bool checked = SendMessageW(hwnd, BM_GETCHECK, 0, 0) == BST_CHECKED;
        const int size = Px(hwnd, 16);
        const RECT box{ rc.left, (rc.top + rc.bottom - size) / 2,
                        rc.left + size, (rc.top + rc.bottom - size) / 2 + size };
        const int radius = radio ? size / 2 : Px(hwnd, kRadius);

        if (checked) {
            // Filled with the accent, the mark in the colour text takes on it.
            const COLORREF fill = disabled ? kDisabled
                                : pressed  ? Blend(accent, back, 20)
                                : hot      ? Blend(accent, back, 10)
                                           : accent;
            FillRoundRectAA(dc, box, radius, fill, back);
            if (radio) {
                RECT dot = box;
                const int in = size * 3 / 10;
                InflateRect(&dot, -in, -in);
                FillRoundRectAA(dc, dot, (dot.right - dot.left) / 2, kOnAccent, fill);
            } else {
                HGDIOBJ prev = SelectObject(dc, CachedUiFont(-size * 3 / 4, FW_NORMAL, true));
                SetTextColor(dc, kOnAccent);
                RECT mark = box;
                DrawTextW(dc, L"\xE73E", -1, &mark, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
                SelectObject(dc, prev);
            }
        } else {
            // An empty box: a shade darker than what it sits on, outlined.
            const COLORREF fill = hot && !disabled ? Blend(back, RGB(255, 255, 255), 5)
                                                   : Blend(back, RGB(0, 0, 0), 10);
            const COLORREF line = disabled ? kDisabled : hot ? kDim : RGB(150, 150, 150);
            OutlineRoundRectAA(dc, box, radius, fill, line, back);
        }

        RECT tr = rc;
        tr.left = box.right + Px(hwnd, 8);
        SetTextColor(dc, disabled ? kDisabled : kText);
        DrawTextW(dc, text.c_str(), -1, &tr, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        if (focused) {
            SIZE sz{};
            GetTextExtentPoint32W(dc, text.c_str(), static_cast<int>(text.size()), &sz);
            RECT fr{ tr.left - 1, tr.top, tr.left + sz.cx + 2, tr.bottom };
            DrawFocusRect(dc, &fr);
        }
        break;
    }

    default: {
        // Push buttons. The default one is filled with the accent, the way a
        // primary button is; the dialog manager moves BS_DEFPUSHBUTTON to
        // whichever button has the focus, so the fill follows it.
        const bool primary = type == BS_DEFPUSHBUTTON && !disabled;
        COLORREF textColor;
        if (primary) {
            const COLORREF fill = pressed ? Blend(accent, back, 20)
                                : hot     ? Blend(accent, back, 10)
                                          : accent;
            FillRoundRectAA(dc, rc, Px(hwnd, kRadius), fill, back);
            textColor = pressed ? Blend(kOnAccent, fill, 30) : kOnAccent;
        } else {
            const COLORREF fill = disabled ? Blend(kControl, back, 50)
                                : pressed  ? kControlDown
                                : hot      ? kControlHot
                                           : kControl;
            OutlineRoundRectAA(dc, rc, Px(hwnd, kRadius), fill, kControlLine, back);
            textColor = disabled ? kDisabled : pressed ? kDim : kText;
        }
        SetTextColor(dc, textColor);
        DrawTextW(dc, text.c_str(), -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        if (focused) {
            RECT fr = rc;
            InflateRect(&fr, -Px(hwnd, 3), -Px(hwnd, 3));
            DrawFocusRect(dc, &fr);
        }
        break;
    }
    }

    SelectObject(dc, oldFont);
    return CDRF_SKIPDEFAULT;
}

} // namespace

COLORREF Accent()
{
    return g_accent;
}

HBRUSH BgBrush()
{
    if (!g_bgBrush)
        g_bgBrush = CreateSolidBrush(kBg);
    return g_bgBrush;
}

HBRUSH CardBrush()
{
    if (!g_cardBrush)
        g_cardBrush = CreateSolidBrush(kCard);
    return g_cardBrush;
}

int Px(HWND hwnd, int at96)
{
    return MulDiv(at96, GetDpiForWindow(hwnd), 96);
}

void RoundBox(HDC dc, const RECT& rc, int radius, COLORREF fill, COLORREF line, int lineWidth)
{
    HBRUSH brush = CreateSolidBrush(fill);
    HPEN pen = CreatePen(PS_SOLID, lineWidth, line);
    HGDIOBJ oldBrush = SelectObject(dc, brush);
    HGDIOBJ oldPen = SelectObject(dc, pen);
    RoundRect(dc, rc.left, rc.top, rc.right, rc.bottom, radius, radius);
    SelectObject(dc, oldBrush);
    SelectObject(dc, oldPen);
    DeleteObject(brush);
    DeleteObject(pen);
}

void Apply(HWND dlg)
{
    g_accent = SystemAccentLight();

    if (!(GetWindowLongW(dlg, GWL_STYLE) & WS_CHILD)) {
        // DWMWA_USE_IMMERSIVE_DARK_MODE is 20 from Windows 10 20H1 on and was
        // 19 for a few builds before that.
        const BOOL on = TRUE;
        if (FAILED(DwmSetWindowAttribute(dlg, 20, &on, sizeof(on))))
            DwmSetWindowAttribute(dlg, 19, &on, sizeof(on));
        // The title bar in the page's own colour, so the window reads as one
        // surface the way Settings does (Windows 11; ignored before it).
        const COLORREF caption = kBg;
        DwmSetWindowAttribute(dlg, 35 /*DWMWA_CAPTION_COLOR*/, &caption, sizeof(caption));
    }

    // Windows 11's face at the size the dialog template asked for, on the
    // dialog itself (the tab strip measures with it) and on every child.
    SendMessageW(dlg, WM_SETFONT, reinterpret_cast<WPARAM>(UiFont(dlg, FW_NORMAL)), FALSE);

    for (HWND child = GetWindow(dlg, GW_CHILD); child; child = GetWindow(child, GW_HWNDNEXT)) {
        if (!ClassIs(child, L"MonopaneKeyboard") && !(GetWindowLongW(child, GWL_STYLE) & DS_CONTROL))
            SendMessageW(child, WM_SETFONT, reinterpret_cast<WPARAM>(UiFont(child, FW_NORMAL)), FALSE);

        if (ClassIs(child, L"ComboBox")) {
            // The dark theme for the list it drops; the closed box is painted
            // by the subclass.
            SetWindowTheme(child, L"DarkMode_CFD", nullptr);
            SetWindowSubclass(child, ComboProc, 1, 0);
        } else if (ClassIs(child, L"ListBox")) {
            // The dark scrollbar, and no frame: the border a list box draws is
            // a light one whatever the theme, and the fill is contrast enough.
            SetWindowTheme(child, L"DarkMode_Explorer", nullptr);
            SetWindowLongW(child, GWL_STYLE, GetWindowLongW(child, GWL_STYLE) & ~WS_BORDER);
            SetWindowLongW(child, GWL_EXSTYLE,
                           GetWindowLongW(child, GWL_EXSTYLE) & ~(WS_EX_CLIENTEDGE | WS_EX_STATICEDGE));
            SetWindowPos(child, nullptr, 0, 0, 0, 0,
                         SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);
            // Rounded like the controls around it. A list box cannot be told
            // to paint rounded corners, but it can be clipped to them.
            RECT rc;
            GetWindowRect(child, &rc);
            const int diameter = 2 * Px(child, kRadius);
            SetWindowRgn(child, CreateRoundRectRgn(0, 0, rc.right - rc.left + 1,
                                                   rc.bottom - rc.top + 1, diameter, diameter), TRUE);
        } else if (IsGroupBox(child)) {
            SetWindowSubclass(child, GroupBoxProc, 1, 0);
        }
    }
}

bool Handle(HWND dlg, UINT msg, WPARAM wParam, LPARAM lParam, INT_PTR& result)
{
    switch (msg) {
    case WM_CTLCOLORDLG:
        result = reinterpret_cast<INT_PTR>(BgBrush());
        return true;

    case WM_ERASEBKGND:
        PaintBackground(dlg, reinterpret_cast<HDC>(wParam));
        SetWindowLongPtrW(dlg, DWLP_MSGRESULT, TRUE);
        result = TRUE;
        return true;

    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLORBTN: {
        HDC dc = reinterpret_cast<HDC>(wParam);
        HWND ctl = reinterpret_cast<HWND>(lParam);
        const bool onCard = BackOf(ctl) == kCard;
        SetTextColor(dc, !IsWindowEnabled(ctl)            ? kDisabled
                         : IsSecondary(ctl)               ? kDim
                                                          : kText);
        SetBkColor(dc, onCard ? kCard : kBg);
        result = reinterpret_cast<INT_PTR>(onCard ? CardBrush() : BgBrush());
        return true;
    }

    case WM_CTLCOLORLISTBOX:
    case WM_CTLCOLOREDIT: {
        if (!g_listBrush)
            g_listBrush = CreateSolidBrush(kList);
        HDC dc = reinterpret_cast<HDC>(wParam);
        SetTextColor(dc, kText);
        SetBkColor(dc, kList);
        result = reinterpret_cast<INT_PTR>(g_listBrush);
        return true;
    }

    case WM_NOTIFY: {
        const NMHDR* hdr = reinterpret_cast<const NMHDR*>(lParam);
        if (hdr->code == NM_CUSTOMDRAW && ClassIs(hdr->hwndFrom, L"Button")) {
            SetWindowLongPtrW(dlg, DWLP_MSGRESULT,
                              DrawButton(*reinterpret_cast<const NMCUSTOMDRAW*>(lParam)));
            result = TRUE;
            return true;
        }
        return false;
    }
    }
    return false;
}

// ------------------------------------------------------------------ layout

void SetSecondary(HWND ctl)
{
    SetWindowLongPtrW(ctl, GWLP_USERDATA, kSecondaryMark);
}

namespace {

// A DC with the control's font selected, for measuring.
struct FontDC {
    explicit FontDC(HWND ctl) : hwnd(ctl), dc(GetDC(ctl))
    {
        old = SelectObject(dc, reinterpret_cast<HFONT>(SendMessageW(ctl, WM_GETFONT, 0, 0)));
    }
    ~FontDC()
    {
        SelectObject(dc, old);
        ReleaseDC(hwnd, dc);
    }
    HWND hwnd;
    HDC dc;
    HGDIOBJ old;
};

} // namespace

int LineHeight(HWND ctl)
{
    FontDC f(ctl);
    TEXTMETRICW tm{};
    GetTextMetricsW(f.dc, &tm);
    return tm.tmHeight;
}

int TextWidth(HWND ctl)
{
    FontDC f(ctl);
    const std::wstring text = Text(ctl);
    SIZE sz{};
    GetTextExtentPoint32W(f.dc, text.c_str(), static_cast<int>(text.size()), &sz);
    return sz.cx;
}

int TextHeight(HWND ctl, int width)
{
    FontDC f(ctl);
    const std::wstring text = Text(ctl);
    RECT rc{ 0, 0, width, 0 };
    DrawTextW(f.dc, text.c_str(), -1, &rc, DT_CALCRECT | DT_WORDBREAK | DT_NOPREFIX);
    return rc.bottom;
}

int ButtonWidth(HWND button)
{
    return std::max(Px(button, kButtonW), TextWidth(button) + Px(button, 32));
}

int CheckWidth(HWND button)
{
    // The box (16), the gap to the text (8), and a little for the focus rect.
    return Px(button, 16 + 8 + 4) + TextWidth(button);
}

HWND ChildByText(HWND parent, const wchar_t* text, bool prefix)
{
    const size_t len = wcslen(text);
    for (HWND c = GetWindow(parent, GW_CHILD); c; c = GetWindow(c, GW_HWNDNEXT)) {
        const std::wstring t = Text(c);
        if (prefix ? t.compare(0, len, text) == 0 : t == text)
            return c;
    }
    return nullptr;
}

void Place(HWND ctl, int x, int y, int w, int h)
{
    if (ctl)
        SetWindowPos(ctl, nullptr, x, y, w, h, SWP_NOZORDER | SWP_NOACTIVATE);
}

void PlaceText(HWND ctl, int x, int rowY, int w, int rowH)
{
    if (!ctl)
        return;
    const int lh = LineHeight(ctl);
    Place(ctl, x, rowY + (rowH - lh) / 2, w, lh);
}

void PlaceCombo(HWND combo, int x, int y, int w, int h)
{
    if (!combo)
        return;
    // A drop-down list's own height is the closed plate plus the list it
    // drops, and the plate is sized by its item height. So: set the item
    // height for the plate, see what the plate came to, and correct once.
    const int listH = Px(combo, 240);
    SendMessageW(combo, CB_SETITEMHEIGHT, static_cast<WPARAM>(-1), h - Px(combo, 6));
    Place(combo, x, y, w, h + listH);
    RECT rc;
    GetWindowRect(combo, &rc);
    const int plate = rc.bottom - rc.top;
    if (plate != h) {
        const LRESULT item = SendMessageW(combo, CB_GETITEMHEIGHT, static_cast<WPARAM>(-1), 0);
        SendMessageW(combo, CB_SETITEMHEIGHT, static_cast<WPARAM>(-1), item + (h - plate));
        Place(combo, x, y, w, h + listH);
    }
}

int HeadingBlock(HWND groupBox)
{
    return HeadingHeight(groupBox) + Px(groupBox, kHeadingGap);
}

Flow::Flow(HWND page) : page_(page)
{
    RECT rc;
    GetClientRect(page, &rc);
    left = inL = Px(kMargin);
    right = inR = rc.right - Px(kMargin);
}

int Flow::Px(int at96) const
{
    return dark::Px(page_, at96);
}

void Flow::BeginSection(HWND groupBox)
{
    group_ = groupBox;
    headingTop_ = y;
    y += HeadingBlock(groupBox) + Px(kCardPad);
    inL = left + Px(kCardPad);
    inR = right - Px(kCardPad);
    afterSection_ = false;
}

void Flow::EndSection()
{
    y += Px(kCardPad);
    Place(group_, left, headingTop_, right - left, y - headingTop_);
    group_ = nullptr;
    inL = left;
    inR = right;
    y += Px(kSectionGap);
    afterSection_ = true;
}

int Flow::Height() const
{
    return afterSection_ ? y - Px(kSectionGap) : y;
}

} // namespace dark
