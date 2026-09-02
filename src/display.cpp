#include "display.h"

#include <utility>

namespace {

// The display device ("\.\DISPLAY1") for the monitor under the cursor — the
// same monitor the switcher opens on, so the two hotkeys agree about which
// screen "this one" means.
bool DeviceUnderCursor(wchar_t (&name)[CCHDEVICENAME])
{
    POINT cursor;
    GetCursorPos(&cursor);
    HMONITOR monitor = MonitorFromPoint(cursor, MONITOR_DEFAULTTOPRIMARY);
    MONITORINFOEXW mi{};
    mi.cbSize = sizeof(mi);
    if (!GetMonitorInfoW(monitor, &mi))
        return false;
    wcscpy_s(name, mi.szDevice);
    return true;
}

bool CurrentMode(const wchar_t* device, DEVMODEW& out)
{
    out = DEVMODEW{};
    out.dmSize = sizeof(out);
    return EnumDisplaySettingsW(device, ENUM_CURRENT_SETTINGS, &out) != FALSE;
}

bool IsPortrait(DWORD orientation) { return (orientation & 1) != 0; }

} // namespace

Orientation CurrentOrientation()
{
    wchar_t device[CCHDEVICENAME];
    DEVMODEW dm;
    if (!DeviceUnderCursor(device) || !CurrentMode(device, dm))
        return Orientation::Landscape;
    return static_cast<Orientation>(dm.dmDisplayOrientation);
}

bool CycleOrientation()
{
    wchar_t device[CCHDEVICENAME];
    DEVMODEW dm;
    if (!DeviceUnderCursor(device) || !CurrentMode(device, dm))
        return false;

    DWORD next = DMDO_DEFAULT;
    switch (dm.dmDisplayOrientation) {
    case DMDO_DEFAULT: next = DMDO_90; break;
    case DMDO_90:      next = DMDO_270; break;
    default:           next = DMDO_DEFAULT; break;  // DMDO_270 and DMDO_180
    }

    // A quarter turn transposes the desktop, so the mode has to carry the
    // swapped dimensions or the driver rejects it.
    if (IsPortrait(dm.dmDisplayOrientation) != IsPortrait(next))
        std::swap(dm.dmPelsWidth, dm.dmPelsHeight);
    dm.dmDisplayOrientation = next;
    dm.dmFields |= DM_DISPLAYORIENTATION | DM_PELSWIDTH | DM_PELSHEIGHT;

    // CDS_UPDATEREGISTRY so the rotation survives a reboot, the way the
    // Settings app's own dropdown does.
    return ChangeDisplaySettingsExW(device, &dm, nullptr, CDS_UPDATEREGISTRY, nullptr) ==
           DISP_CHANGE_SUCCESSFUL;
}

const wchar_t* OrientationName(Orientation o)
{
    switch (o) {
    case Orientation::Portrait:         return L"Portrait";
    case Orientation::LandscapeFlipped: return L"Landscape (flipped)";
    case Orientation::PortraitFlipped:  return L"Portrait (flipped)";
    default:                            return L"Landscape";
    }
}
