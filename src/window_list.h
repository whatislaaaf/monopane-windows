#pragma once
#include <windows.h>
#include <string>
#include <vector>

struct WindowInfo {
    HWND hwnd = nullptr;
    std::wstring title;
    std::wstring appName;   // friendly app name (FileDescription or exe stem)
    std::wstring exePath;   // full path of the owning process's executable
    HICON icon = nullptr;   // owned by the icon cache, do not destroy
};

// Sets the pixel size icons are extracted at (call before enumerating, and
// again on DPI changes). Extracting at the exact size the UI draws avoids
// jagged scaling. Changing the size clears the icon cache.
void SetIconSizePx(int px);

// Returns all Alt-Tab-eligible top-level windows in z-order (topmost first),
// excluding windows belonging to this process.
std::vector<WindowInfo> EnumerateAltTabWindows();

// Restores (if minimized) and brings the window to the foreground.
void ActivateWindow(HWND hwnd);

// Destroys all cached icons. Call once at shutdown.
void ClearIconCache();
