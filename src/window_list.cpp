#include "window_list.h"

#include <dwmapi.h>
#include <shellapi.h>
#include <shlobj.h>
#include <unordered_map>

namespace {

struct AppInfo {
    HICON icon = nullptr;
    std::wstring name;
};

// Keyed by full exe path; lives for the process lifetime.
std::unordered_map<std::wstring, AppInfo> g_appCache;

int g_iconSizePx = 32;

std::wstring ExeStem(const std::wstring& path)
{
    const size_t slash = path.find_last_of(L"\\/");
    std::wstring stem = slash == std::wstring::npos ? path : path.substr(slash + 1);
    const size_t dot = stem.find_last_of(L'.');
    if (dot != std::wstring::npos)
        stem.resize(dot);
    if (!stem.empty())
        stem[0] = static_cast<wchar_t>(towupper(stem[0]));
    return stem;
}

std::wstring FileDescription(const std::wstring& exePath)
{
    DWORD handle = 0;
    const DWORD size = GetFileVersionInfoSizeW(exePath.c_str(), &handle);
    if (size == 0)
        return L"";

    std::vector<BYTE> data(size);
    if (!GetFileVersionInfoW(exePath.c_str(), 0, size, data.data()))
        return L"";

    struct LangCodePage { WORD lang; WORD codePage; };
    LangCodePage* translations = nullptr;
    UINT cb = 0;
    if (!VerQueryValueW(data.data(), L"\\VarFileInfo\\Translation",
                        reinterpret_cast<void**>(&translations), &cb) ||
        cb < sizeof(LangCodePage))
        return L"";

    wchar_t subBlock[64];
    swprintf_s(subBlock, L"\\StringFileInfo\\%04x%04x\\FileDescription",
               translations[0].lang, translations[0].codePage);

    wchar_t* description = nullptr;
    UINT descLen = 0;
    if (VerQueryValueW(data.data(), subBlock,
                       reinterpret_cast<void**>(&description), &descLen) &&
        description && descLen > 0 && description[0] != L'\0')
        return description;

    return L"";
}

const AppInfo& GetAppInfo(const std::wstring& exePath)
{
    auto it = g_appCache.find(exePath);
    if (it != g_appCache.end())
        return it->second;

    AppInfo info;
    // Extract at the exact size the overlay draws, so no scaling (and thus
    // no aliasing) happens at render time.
    HICON icon = nullptr;
    if (SHDefExtractIconW(exePath.c_str(), 0, 0, &icon, nullptr,
                          MAKELONG(g_iconSizePx, g_iconSizePx)) == S_OK && icon) {
        info.icon = icon;
    } else {
        SHFILEINFOW sfi{};
        if (SHGetFileInfoW(exePath.c_str(), 0, &sfi, sizeof(sfi), SHGFI_ICON | SHGFI_LARGEICON))
            info.icon = sfi.hIcon;
    }

    info.name = FileDescription(exePath);
    if (info.name.empty())
        info.name = ExeStem(exePath);

    return g_appCache.emplace(exePath, std::move(info)).first->second;
}

bool IsCloaked(HWND hwnd)
{
    DWORD cloaked = 0;
    if (SUCCEEDED(DwmGetWindowAttribute(hwnd, DWMWA_CLOAKED, &cloaked, sizeof(cloaked))))
        return cloaked != 0;
    return false;
}

bool IsAltTabEligible(HWND hwnd)
{
    if (!IsWindowVisible(hwnd))
        return false;
    if (GetWindowTextLengthW(hwnd) == 0)
        return false;

    const LONG exStyle = GetWindowLongW(hwnd, GWL_EXSTYLE);
    if (exStyle & WS_EX_TOOLWINDOW)
        return false;
    if (exStyle & WS_EX_NOACTIVATE)
        return false;

    if (IsCloaked(hwnd))
        return false;

    wchar_t className[64];
    if (GetClassNameW(hwnd, className, 64)) {
        if (wcscmp(className, L"Progman") == 0 || wcscmp(className, L"WorkerW") == 0)
            return false;
    }

    // A window appears in Alt-Tab only if it is its root owner's last active
    // visible popup (the classic Raymond Chen test).
    HWND walk = GetAncestor(hwnd, GA_ROOTOWNER);
    HWND next;
    while ((next = GetLastActivePopup(walk)) != walk) {
        if (IsWindowVisible(next))
            break;
        walk = next;
    }
    return walk == hwnd;
}

struct EnumContext {
    std::vector<WindowInfo>* out;
    DWORD ownPid;
};

BOOL CALLBACK EnumProc(HWND hwnd, LPARAM lParam)
{
    auto* ctx = reinterpret_cast<EnumContext*>(lParam);

    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (pid == 0 || pid == ctx->ownPid)
        return TRUE;

    if (!IsAltTabEligible(hwnd))
        return TRUE;

    wchar_t title[512];
    if (GetWindowTextW(hwnd, title, 512) == 0)
        return TRUE;

    std::wstring exePath;
    if (HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid)) {
        wchar_t path[MAX_PATH];
        DWORD len = MAX_PATH;
        if (QueryFullProcessImageNameW(process, 0, path, &len))
            exePath = path;
        CloseHandle(process);
    }

    WindowInfo info;
    info.hwnd = hwnd;
    info.title = title;
    if (!exePath.empty()) {
        const AppInfo& app = GetAppInfo(exePath);
        info.appName = app.name;
        info.icon = app.icon;
        info.exePath = std::move(exePath);
    }

    ctx->out->push_back(std::move(info));
    return TRUE;
}

} // namespace

void SetIconSizePx(int px)
{
    if (px == g_iconSizePx)
        return;
    g_iconSizePx = px;
    ClearIconCache();
}

std::vector<WindowInfo> EnumerateAltTabWindows()
{
    std::vector<WindowInfo> windows;
    EnumContext ctx{ &windows, GetCurrentProcessId() };
    EnumWindows(EnumProc, reinterpret_cast<LPARAM>(&ctx));
    return windows;
}

void ActivateWindow(HWND hwnd)
{
    if (!IsWindow(hwnd))
        return;

    if (IsIconic(hwnd))
        ShowWindow(hwnd, SW_RESTORE);

    if (SetForegroundWindow(hwnd))
        return;

    // Foreground-lock workaround: attach to the current foreground thread so
    // the system treats us as entitled to change the foreground window.
    const DWORD fgThread = GetWindowThreadProcessId(GetForegroundWindow(), nullptr);
    const DWORD myThread = GetCurrentThreadId();
    if (fgThread != myThread && AttachThreadInput(myThread, fgThread, TRUE)) {
        SetForegroundWindow(hwnd);
        AttachThreadInput(myThread, fgThread, FALSE);
    } else {
        SetForegroundWindow(hwnd);
    }
}

void ClearIconCache()
{
    for (auto& [path, app] : g_appCache) {
        if (app.icon)
            DestroyIcon(app.icon);
    }
    g_appCache.clear();
}
