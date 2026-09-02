#pragma once
#include <windows.h>

// A rounded rectangle filled with `fill`, its corners anti-aliased against
// `back` — the colour already behind it, which both overlays paint first.
//
// GDI's own RoundRect has no anti-aliasing, and a hard-edged corner is exactly
// what gives away a panel whose window corners the compositor has rounded
// smoothly. `radius` is the real corner radius, not the ellipse size RoundRect
// takes; it is clamped to half the shorter side.
void FillRoundRectAA(HDC dc, const RECT& rc, int radius, COLORREF fill, COLORREF back);
