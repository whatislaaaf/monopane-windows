#include "paint.h"

#include <algorithm>
#include <cmath>

namespace {

// `t` is how much of `fill` shows through, 0..1.
COLORREF Blend(COLORREF fill, COLORREF back, double t)
{
    const auto mix = [t](int f, int b) {
        return static_cast<int>(b + (f - b) * t + 0.5);
    };
    return RGB(mix(GetRValue(fill), GetRValue(back)),
               mix(GetGValue(fill), GetGValue(back)),
               mix(GetBValue(fill), GetBValue(back)));
}

} // namespace

void FillRoundRectAA(HDC dc, const RECT& rc, int radius, COLORREF fill, COLORREF back)
{
    const int w = rc.right - rc.left;
    const int h = rc.bottom - rc.top;
    if (w <= 0 || h <= 0)
        return;
    radius = std::clamp(radius, 0, std::min(w, h) / 2);

    HBRUSH brush = CreateSolidBrush(fill);
    if (radius == 0) {
        FillRect(dc, &rc, brush);
        DeleteObject(brush);
        return;
    }

    // Everything but the four corner squares is a straight fill.
    const RECT middle{ rc.left + radius, rc.top, rc.right - radius, rc.bottom };
    const RECT left{ rc.left, rc.top + radius, rc.left + radius, rc.bottom - radius };
    const RECT right{ rc.right - radius, rc.top + radius, rc.right, rc.bottom - radius };
    FillRect(dc, &middle, brush);
    FillRect(dc, &left, brush);
    FillRect(dc, &right, brush);
    DeleteObject(brush);

    // The corners, a pixel at a time: coverage is how far inside the corner
    // circle the pixel's centre falls, smoothed over the last pixel of it.
    for (int corner = 0; corner < 4; ++corner) {
        const bool onRight = (corner & 1) != 0;
        const bool onBottom = (corner & 2) != 0;
        const int originX = onRight ? rc.right - radius : rc.left;
        const int originY = onBottom ? rc.bottom - radius : rc.top;
        const double centreX = onRight ? rc.right - radius : rc.left + radius;
        const double centreY = onBottom ? rc.bottom - radius : rc.top + radius;

        for (int y = 0; y < radius; ++y) {
            for (int x = 0; x < radius; ++x) {
                const double dx = originX + x + 0.5 - centreX;
                const double dy = originY + y + 0.5 - centreY;
                const double coverage =
                    std::clamp(radius - std::sqrt(dx * dx + dy * dy) + 0.5, 0.0, 1.0);
                if (coverage > 0.0)
                    SetPixel(dc, originX + x, originY + y, Blend(fill, back, coverage));
            }
        }
    }
}
