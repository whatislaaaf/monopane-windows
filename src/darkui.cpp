#include "darkui.h"

#include <commctrl.h>
#include <dwmapi.h>
#include <uxtheme.h>

#include <string>

namespace dark {
namespace {

HBRUSH g_bgBrush = nullptr;
HBRUSH g_cardBrush = nullptr;

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
            RoundBox(dc, box, Px(hwnd, 3), fill, line);
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
        // Push buttons. The dialog manager moves BS_DEFPUSHBUTTON to whichever
        // button has the focus, so the accent outline follows it.
        const bool def = type == BS_DEFPUSHBUTTON;
        RECT plate = rc;
        plate.right -= 1;
        plate.bottom -= 1;
        const COLORREF fill = disabled ? RGB(38, 38, 38)
                              : pressed ? RGB(32, 32, 32)
                              : hot     ? RGB(62, 62, 62)
                                        : kCard;
        RoundBox(dc, plate, Px(hwnd, 6), fill, def && !disabled ? kAccent : kLine);
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
        if (ClassIs(child, L"ComboBox"))
            SetWindowTheme(child, L"DarkMode_CFD", nullptr);
        else if (ClassIs(child, L"ListBox")) {
            // The dark scrollbar, and no frame: the border a list box draws is
            // a light one whatever the theme, and the fill is contrast enough.
            SetWindowTheme(child, L"DarkMode_Explorer", nullptr);
            SetWindowLongW(child, GWL_STYLE, GetWindowLongW(child, GWL_STYLE) & ~WS_BORDER);
            SetWindowLongW(child, GWL_EXSTYLE,
                           GetWindowLongW(child, GWL_EXSTYLE) & ~(WS_EX_CLIENTEDGE | WS_EX_STATICEDGE));
            SetWindowPos(child, nullptr, 0, 0, 0, 0,
                         SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);
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
