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

// The same with a 1px outline in `line`: the outline's shape, and the fill's a
// pixel inside it, both anti-aliased.
void OutlineRoundRectAA(HDC dc, const RECT& rc, int radius, COLORREF fill, COLORREF line,
                        COLORREF back);

// The light shade of the user's accent colour, the one Windows 11 draws on
// dark surfaces (#4CC2FF for the default blue). The shell keeps the whole
// ramp in AccentPalette, lightest first; this is its second entry.
COLORREF SystemAccentLight();

// Whether a font family is installed, for preferring Windows 11's faces
// (Segoe UI Variable, Segoe Fluent Icons) and falling back on Windows 10.
bool FontInstalled(const wchar_t* face);
