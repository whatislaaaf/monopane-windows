#include "darkui.h"

#include <commctrl.h>
#include <dwmapi.h>
#include <uxtheme.h>

#include <string>

namespace dark {
namespace {

HBRUSH g_bgBrush = nullptr;
HBRUSH g_cardBrush = nullptr;

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

std::wstring Text(HWND hwnd)
{
    wchar_t buf[256] = {};
    GetWindowTextW(hwnd, buf, 256);
    return buf;
}

void DrawCheckMark(HDC dc, const RECT& box, COLORREF color, int dpi)
{
    HPEN pen = CreatePen(PS_SOLID, MulDiv(2, dpi, 96), color);
    HGDIOBJ old = SelectObject(dc, pen);
    const int w = box.right - box.left;
    const int h = box.bottom - box.top;
    const POINT pts[3] = {
        { box.left + w * 22 / 100, box.top + h * 52 / 100 },
        { box.left + w * 42 / 100, box.top + h * 72 / 100 },
        { box.left + w * 78 / 100, box.top + h * 30 / 100 },
    };
    Polyline(dc, pts, 3);
    SelectObject(dc, old);
    DeleteObject(pen);
}

// A group box: a rounded frame with the caption sitting on its top edge.
// Group boxes do not send the custom-draw notification the other buttons do,
// so this is painted from a subclass instead; the interior is left alone,
// since the controls inside it are siblings painted after it.
void PaintGroupBox(HWND hwnd)
{
    PAINTSTRUCT ps;
    HDC dc = BeginPaint(hwnd, &ps);
    RECT rc;
    GetClientRect(hwnd, &rc);
    HGDIOBJ oldFont = SelectObject(dc, reinterpret_cast<HFONT>(SendMessageW(hwnd, WM_GETFONT, 0, 0)));
    SetBkMode(dc, TRANSPARENT);

    TEXTMETRICW tm{};
    GetTextMetricsW(dc, &tm);
    RECT frame = rc;
    frame.top += tm.tmHeight / 2;
    frame.right -= 1;
    frame.bottom -= 1;
    {
        HPEN pen = CreatePen(PS_SOLID, 1, kLine);
        HGDIOBJ oldPen = SelectObject(dc, pen);
        HGDIOBJ oldBrush = SelectObject(dc, GetStockObject(NULL_BRUSH));
        const int radius = Px(hwnd, 8);
        RoundRect(dc, frame.left, frame.top, frame.right, frame.bottom, radius, radius);
        SelectObject(dc, oldBrush);
        SelectObject(dc, oldPen);
        DeleteObject(pen);
    }
    const std::wstring text = Text(hwnd);
    if (!text.empty()) {
        SIZE sz{};
        GetTextExtentPoint32W(dc, text.c_str(), static_cast<int>(text.size()), &sz);
        RECT tr{ rc.left + Px(hwnd, 10), rc.top, rc.right, rc.top + tm.tmHeight };
        RECT pad{ tr.left - Px(hwnd, 4), tr.top, tr.left + sz.cx + Px(hwnd, 4), tr.bottom };
        FillRect(dc, &pad, BgBrush());
        SetTextColor(dc, kDim);
        DrawTextW(dc, text.c_str(), -1, &tr, DT_LEFT | DT_TOP | DT_SINGLELINE | DT_NOPREFIX);
    }
    SelectObject(dc, oldFont);
    EndPaint(hwnd, &ps);
}

LRESULT CALLBACK GroupBoxProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam,
                              UINT_PTR id, DWORD_PTR)
{
    switch (msg) {
    case WM_PAINT:
        PaintGroupBox(hwnd);
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_NCDESTROY:
        RemoveWindowSubclass(hwnd, GroupBoxProc, id);
        break;
    }
    return DefSubclassProc(hwnd, msg, wParam, lParam);
}

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
    FillRect(dc, &rc, BgBrush());

    const bool disabled = !IsWindowEnabled(hwnd);
    const bool open = SendMessageW(hwnd, CB_GETDROPPEDSTATE, 0, 0) != 0;
    const bool focused = GetFocus() == hwnd;
    POINT cursor;
    GetCursorPos(&cursor);
    const bool hot = !disabled && WindowFromPoint(cursor) == hwnd;

    RECT plate = rc;
    plate.right -= 1;
    plate.bottom -= 1;
    const COLORREF fill = disabled ? RGB(38, 38, 38) : hot || open ? RGB(64, 64, 64) : kCard;
    const COLORREF line = open || focused ? kAccent : hot ? RGB(96, 96, 96) : RGB(78, 78, 78);
    RoundBox(dc, plate, Px(hwnd, 10), fill, line);

    HGDIOBJ oldFont = SelectObject(dc, reinterpret_cast<HFONT>(SendMessageW(hwnd, WM_GETFONT, 0, 0)));
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, disabled ? kDim : kText);
    const LRESULT sel = SendMessageW(hwnd, CB_GETCURSEL, 0, 0);
    wchar_t text[128] = {};
    if (sel >= 0 && SendMessageW(hwnd, CB_GETLBTEXTLEN, sel, 0) < 128)
        SendMessageW(hwnd, CB_GETLBTEXT, sel, reinterpret_cast<LPARAM>(text));
    RECT tr{ rc.left + Px(hwnd, 9), rc.top, rc.right - Px(hwnd, 26), rc.bottom };
    DrawTextW(dc, text, -1, &tr, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
    SelectObject(dc, oldFont);

    // The chevron: a small open V, pointing down.
    {
        const int cx = rc.right - Px(hwnd, 14);
        const int cy = (rc.top + rc.bottom) / 2;
        const int half = Px(hwnd, 4);
        HPEN pen = CreatePen(PS_SOLID, GetDpiForWindow(hwnd) >= 144 ? 2 : 1, disabled ? kDim : kText);
        HGDIOBJ oldPen = SelectObject(dc, pen);
        const POINT pts[3] = { { cx - half, cy - half / 2 }, { cx, cy + half / 2 }, { cx + half, cy - half / 2 } };
        Polyline(dc, pts, 3);
        SelectObject(dc, oldPen);
        DeleteObject(pen);
    }
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
    const int dpi = GetDpiForWindow(hwnd);
    const LONG type = GetWindowLongW(hwnd, GWL_STYLE) & BS_TYPEMASK;
    const bool disabled = !IsWindowEnabled(hwnd);
    const bool hot = (cd.uItemState & CDIS_HOT) != 0;
    const bool pressed = (cd.uItemState & CDIS_SELECTED) != 0;
    const bool focused = (cd.uItemState & CDIS_FOCUS) != 0 &&
                         !(SendMessageW(hwnd, WM_QUERYUISTATE, 0, 0) & UISF_HIDEFOCUS);
    const std::wstring text = Text(hwnd);
    const COLORREF textColor = disabled ? kDim : kText;

    HGDIOBJ oldFont = SelectObject(dc, reinterpret_cast<HFONT>(SendMessageW(hwnd, WM_GETFONT, 0, 0)));
    SetBkMode(dc, TRANSPARENT);
    FillRect(dc, &rc, BgBrush());

    switch (type) {
    case BS_CHECKBOX:
    case BS_AUTOCHECKBOX:
    case BS_3STATE:
    case BS_AUTO3STATE:
    case BS_RADIOBUTTON:
    case BS_AUTORADIOBUTTON: {
        const bool radio = type == BS_RADIOBUTTON || type == BS_AUTORADIOBUTTON;
        const bool checked = SendMessageW(hwnd, BM_GETCHECK, 0, 0) == BST_CHECKED;
        const int size = Px(hwnd, 13);
        const RECT box{ rc.left, (rc.top + rc.bottom - size) / 2,
                        rc.left + size, (rc.top + rc.bottom - size) / 2 + size };
        const COLORREF fill = checked ? kAccent : hot ? RGB(60, 60, 60) : kCard;
        const COLORREF line = checked ? kAccent : hot ? kDim : RGB(110, 110, 110);
        if (radio) {
            HBRUSH brush = CreateSolidBrush(fill);
            HPEN pen = CreatePen(PS_SOLID, 1, line);
            HGDIOBJ oldBrush = SelectObject(dc, brush);
            HGDIOBJ oldPen = SelectObject(dc, pen);
            Ellipse(dc, box.left, box.top, box.right, box.bottom);
            if (checked) {
                HBRUSH dot = CreateSolidBrush(kText);
                SelectObject(dc, dot);
                SelectObject(dc, GetStockObject(NULL_PEN));
                const int in = size * 30 / 100;
                Ellipse(dc, box.left + in, box.top + in, box.right - in + 1, box.bottom - in + 1);
                SelectObject(dc, oldBrush);
                DeleteObject(dot);
            } else {
                SelectObject(dc, oldBrush);
            }
            SelectObject(dc, oldPen);
            DeleteObject(brush);
            DeleteObject(pen);
        } else {
            RoundBox(dc, box, Px(hwnd, 4), fill, line);
            if (checked)
                DrawCheckMark(dc, box, kText, dpi);
        }
        RECT tr = rc;
        tr.left = box.right + Px(hwnd, 6);
        SetTextColor(dc, textColor);
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
        const bool def = type == BS_DEFPUSHBUTTON && !disabled;
        RECT plate = rc;
        plate.right -= 1;
        plate.bottom -= 1;
        COLORREF fill;
        if (def)
            fill = pressed ? Blend(kAccent, RGB(0, 0, 0), 25)
                 : hot     ? Blend(kAccent, RGB(255, 255, 255), 15)
                           : kAccent;
        else
            fill = disabled ? RGB(38, 38, 38)
                 : pressed  ? RGB(32, 32, 32)
                 : hot      ? RGB(64, 64, 64)
                            : kCard;
        RoundBox(dc, plate, Px(hwnd, 10), fill, def ? fill : hot ? RGB(96, 96, 96) : RGB(78, 78, 78));
        SetTextColor(dc, def ? RGB(255, 255, 255) : textColor);
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
    // DWMWA_USE_IMMERSIVE_DARK_MODE is 20 from Windows 10 20H1 on and was 19
    // for a few builds before that.
    const BOOL on = TRUE;
    if (FAILED(DwmSetWindowAttribute(dlg, 20, &on, sizeof(on))))
        DwmSetWindowAttribute(dlg, 19, &on, sizeof(on));

    for (HWND child = GetWindow(dlg, GW_CHILD); child; child = GetWindow(child, GW_HWNDNEXT)) {
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
            // Rounded like the plates around it. A list box cannot be told to
            // paint rounded corners, but it can be clipped to them.
            RECT rc;
            GetWindowRect(child, &rc);
            const int radius = Px(child, 10);
            SetWindowRgn(child, CreateRoundRectRgn(0, 0, rc.right - rc.left + 1,
                                                   rc.bottom - rc.top + 1, radius, radius), TRUE);
        }
        else if (ClassIs(child, L"Button") &&
                 (GetWindowLongW(child, GWL_STYLE) & BS_TYPEMASK) == BS_GROUPBOX)
            SetWindowSubclass(child, GroupBoxProc, 1, 0);
    }
}

bool Handle(HWND dlg, UINT msg, WPARAM wParam, LPARAM lParam, INT_PTR& result)
{
    switch (msg) {
    case WM_CTLCOLORDLG:
        result = reinterpret_cast<INT_PTR>(BgBrush());
        return true;

    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLORBTN: {
        HDC dc = reinterpret_cast<HDC>(wParam);
        SetTextColor(dc, kText);
        SetBkColor(dc, kBg);
        result = reinterpret_cast<INT_PTR>(BgBrush());
        return true;
    }

    case WM_CTLCOLORLISTBOX:
    case WM_CTLCOLOREDIT: {
        HDC dc = reinterpret_cast<HDC>(wParam);
        SetTextColor(dc, kText);
        SetBkColor(dc, kCard);
        result = reinterpret_cast<INT_PTR>(CardBrush());
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

} // namespace dark
