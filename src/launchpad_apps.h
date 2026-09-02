#pragma once
#include <windows.h>
#include <string>
#include <vector>

// The apps pinned to the launchpad, and the file they live in.
//
// %APPDATA%\Monopane\launchpad.ini holds one app per line, in grid order:
//
//     <target>
//     <target> | <name>
//
// where <target> is the path of an .exe or .lnk (environment variables are
// expanded), or "app:" followed by the name the shell's Applications folder
// parses it by — an AppUserModelID for Store apps. The optional <name> is
// what the tile shows instead of the app's own name.

struct LaunchpadApp {
    std::wstring target;       // as written in the file
    std::wstring customName;   // the "| <name>" part; empty = the app's own name

    // Resolved from the target, not stored.
    std::wstring displayName;  // the shell's name for it
    std::wstring exePath;      // the executable behind it, when known
    std::wstring aumid;        // its AppUserModelID, when it has one
    HBITMAP icon = nullptr;    // 32-bit with alpha, square; see FreeLaunchpadIcons

    const std::wstring& Name() const { return customName.empty() ? displayName : customName; }
};

std::wstring LaunchpadIniPath();

// Opens Explorer on launchpad.ini, writing the file first if it is missing.
void OpenLaunchpadFolder(const std::vector<LaunchpadApp>& apps);

// Rereads launchpad.ini into `apps` if it has changed since the last load (or
// `force`), resolving every entry and freeing the icons of the old list. The
// first time ever, the file is seeded from the Start menu's pinned apps.
// Returns true when `apps` was replaced.
bool LoadLaunchpad(std::vector<LaunchpadApp>& apps, int iconPx, bool force);
void SaveLaunchpad(const std::vector<LaunchpadApp>& apps);

// Fills in the resolved fields of one entry, replacing any icon it had.
void ResolveLaunchpadApp(LaunchpadApp& app, int iconPx);
void FreeLaunchpadIcons(std::vector<LaunchpadApp>& apps);

// Runs it, as the shell would from a shortcut or the Start menu.
bool LaunchLaunchpadApp(const LaunchpadApp& app);

// Everything the shell's Applications folder lists: Start menu shortcuts and
// Store apps alike, sorted by name. Web links are left out. The caller owns
// the icons.
struct InstalledApp {
    std::wstring name;
    std::wstring target;   // what to write to launchpad.ini to pin it
    HBITMAP icon = nullptr;
};
std::vector<InstalledApp> EnumerateInstalledApps(int iconPx);
