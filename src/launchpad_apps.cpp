#include "launchpad_apps.h"

#include <initguid.h>  // ahead of the shell headers: defines the PKEY_* and FOLDERID_* it needs
#include <knownfolders.h>
#include <propkey.h>
#include <propvarutil.h>
#include <shellapi.h>
#include <shlguid.h>  // BHID_EnumItems
#include <shlobj.h>
#include <shobjidl.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <cwctype>

#include "window_list.h"

namespace {

constexpr wchar_t kAppPrefix[] = L"app:";
constexpr size_t kAppPrefixLen = 4;

// ------------------------------------------------------------------ strings

// Carriage returns count as whitespace here: the file is written with CRLF
// line endings and split on the LF, so every line arrives with the CR still
// on it — including the blank one under the header, which would otherwise
// parse as an app whose target is a lone control character.
std::wstring Trim(const std::wstring& s)
{
    constexpr wchar_t kSpace[] = L" \t\r\n";
    const size_t b = s.find_first_not_of(kSpace);
    if (b == std::wstring::npos)
        return L"";
    const size_t e = s.find_last_not_of(kSpace);
    return s.substr(b, e - b + 1);
}

std::wstring Utf8ToWide(const std::string& s)
{
    if (s.empty())
        return L"";
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring out(static_cast<size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), &out[0], n);
    return out;
}

std::string WideToUtf8(const std::wstring& s)
{
    if (s.empty())
        return "";
    const int n = WideCharToMultiByte(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0,
                                      nullptr, nullptr);
    std::string out(static_cast<size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), &out[0], n, nullptr,
                        nullptr);
    return out;
}

bool EndsWithNoCase(const std::wstring& s, const wchar_t* suffix)
{
    const size_t n = wcslen(suffix);
    if (s.size() < n)
        return false;
    return _wcsnicmp(s.c_str() + s.size() - n, suffix, n) == 0;
}

bool IsAppTarget(const std::wstring& target)
{
    return _wcsnicmp(target.c_str(), kAppPrefix, kAppPrefixLen) == 0;
}

// The Applications folder parses classic desktop apps by their exe path and
// everything else by an AppUserModelID; a drive letter tells the two apart.
bool LooksLikePath(const std::wstring& s)
{
    return s.size() > 2 && s[1] == L':' && (s[2] == L'\\' || s[2] == L'/');
}

std::wstring ExpandEnv(const std::wstring& s)
{
    wchar_t buf[MAX_PATH * 2];
    const DWORD n = ExpandEnvironmentStringsW(s.c_str(), buf, MAX_PATH * 2);
    if (n == 0 || n > MAX_PATH * 2)
        return s;
    return buf;
}

// What the shell should be asked to open or describe for this target.
std::wstring ParsingNameFor(const std::wstring& target)
{
    if (!IsAppTarget(target))
        return ExpandEnv(target);
    const std::wstring id = target.substr(kAppPrefixLen);
    return LooksLikePath(id) ? id : L"shell:AppsFolder\\" + id;
}

std::wstring FileStem(const std::wstring& path)
{
    const size_t slash = path.find_last_of(L"\\/");
    std::wstring stem = slash == std::wstring::npos ? path : path.substr(slash + 1);
    const size_t dot = stem.find_last_of(L'.');
    if (dot != std::wstring::npos && dot > 0)
        stem.resize(dot);
    return stem;
}

// --------------------------------------------------------------------- file

std::wstring ConfigDir()
{
    wchar_t appdata[MAX_PATH] = {};
    if (GetEnvironmentVariableW(L"APPDATA", appdata, MAX_PATH) == 0) {
        wchar_t exe[MAX_PATH] = {};
        GetModuleFileNameW(nullptr, exe, MAX_PATH);
        std::wstring dir = exe;
        const size_t slash = dir.find_last_of(L'\\');
        return slash == std::wstring::npos ? dir : dir.substr(0, slash);
    }
    return std::wstring(appdata) + L"\\Monopane";
}

bool ReadWholeFile(const std::wstring& path, std::string& out)
{
    HANDLE f = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE)
        return false;
    LARGE_INTEGER size = {};
    if (!GetFileSizeEx(f, &size) || size.QuadPart > (1 << 20)) {
        CloseHandle(f);
        return false;
    }
    out.resize(static_cast<size_t>(size.QuadPart));
    DWORD read = 0;
    const bool ok = out.empty() ||
                    (ReadFile(f, &out[0], static_cast<DWORD>(out.size()), &read, nullptr) != 0 &&
                     read == out.size());
    CloseHandle(f);
    return ok;
}

bool WriteWholeFile(const std::wstring& path, const std::string& text)
{
    CreateDirectoryW(ConfigDir().c_str(), nullptr);
    HANDLE f = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE)
        return false;
    DWORD written = 0;
    const bool ok = WriteFile(f, text.data(), static_cast<DWORD>(text.size()), &written, nullptr) != 0 &&
                    written == text.size();
    CloseHandle(f);
    return ok;
}

bool FileWriteTime(const std::wstring& path, FILETIME& out)
{
    WIN32_FILE_ATTRIBUTE_DATA data{};
    if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &data))
        return false;
    out = data.ftLastWriteTime;
    return true;
}

// The write time of the file as last read or written here, so a hand edit is
// noticed on the next open while our own saves are not re-read.
FILETIME g_loadedWriteTime{};
bool g_loadedOnce = false;

void ParseIni(const std::string& text, std::vector<LaunchpadApp>& apps)
{
    apps.clear();
    size_t pos = 0;
    if (text.size() >= 3 && static_cast<unsigned char>(text[0]) == 0xEF &&
        static_cast<unsigned char>(text[1]) == 0xBB && static_cast<unsigned char>(text[2]) == 0xBF)
        pos = 3;  // a BOM Notepad may have added

    while (pos < text.size()) {
        size_t eol = text.find('\n', pos);
        if (eol == std::string::npos)
            eol = text.size();
        std::wstring line = Utf8ToWide(text.substr(pos, eol - pos));
        pos = eol + 1;

        // A comment is a line starting with '#', or a '#' after whitespace —
        // a bare '#' can legitimately sit inside a path.
        line = Trim(line);
        if (line.empty() || line[0] == L'#')
            continue;
        for (size_t i = 1; i < line.size(); ++i) {
            if (line[i] == L'#' && (line[i - 1] == L' ' || line[i - 1] == L'\t')) {
                line.resize(i);
                break;
            }
        }

        LaunchpadApp app;
        const size_t bar = line.find(L'|');  // never part of a path or an AppUserModelID
        if (bar == std::wstring::npos) {
            app.target = Trim(line);
        } else {
            app.target = Trim(line.substr(0, bar));
            app.customName = Trim(line.substr(bar + 1));
        }
        if (!app.target.empty())
            apps.push_back(std::move(app));
    }
}

std::string FormatIni(const std::vector<LaunchpadApp>& apps)
{
    std::string out;
    out += "# Monopane launchpad - one app per line, in grid order.\r\n";
    out += "#\r\n";
    out += "#   <target>            an .exe or .lnk path (environment variables are\r\n";
    out += "#                       expanded), or app:<AppUserModelID> for a Store app\r\n";
    out += "#   <target> | <name>   the same, shown under a name of your choosing\r\n";
    out += "#\r\n";
    out += "# Edited by hand? The launchpad rereads this file the next time it opens.\r\n";
    out += "\r\n";
    for (const LaunchpadApp& app : apps) {
        std::string line = WideToUtf8(app.target);
        if (!app.customName.empty())
            line += " | " + WideToUtf8(app.customName);
        // An AppUserModelID says nothing about which app it is, so the name
        // rides along as a comment.
        if (IsAppTarget(app.target) && app.customName.empty() && !app.displayName.empty())
            line += "   # " + WideToUtf8(app.displayName);
        out += line + "\r\n";
    }
    return out;
}

// ------------------------------------------------------------------ seeding

// Pulls a JSON string value out of `json` following each occurrence of
// `key` (given with its quotes and colon, e.g. "\"desktopAppLink\":").
// Enough of a parser for Export-StartLayout's flat output.
std::vector<std::wstring> JsonStringsAfter(const std::string& json, const char* key)
{
    std::vector<std::wstring> out;
    const size_t keyLen = strlen(key);
    size_t pos = 0;
    while ((pos = json.find(key, pos)) != std::string::npos) {
        pos += keyLen;
        while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t'))
            ++pos;
        if (pos >= json.size() || json[pos] != '"')
            continue;
        ++pos;
        std::string value;
        while (pos < json.size() && json[pos] != '"') {
            char c = json[pos++];
            if (c == '\\' && pos < json.size()) {
                const char e = json[pos++];
                switch (e) {
                case 'n': c = '\n'; break;
                case 't': c = '\t'; break;
                case 'u': {
                    // \uXXXX: only the BMP, which is all a path needs
                    unsigned code = 0;
                    for (int i = 0; i < 4 && pos < json.size(); ++i, ++pos) {
                        const char h = json[pos];
                        code <<= 4;
                        if (h >= '0' && h <= '9') code |= static_cast<unsigned>(h - '0');
                        else if (h >= 'a' && h <= 'f') code |= static_cast<unsigned>(h - 'a' + 10);
                        else if (h >= 'A' && h <= 'F') code |= static_cast<unsigned>(h - 'A' + 10);
                    }
                    const wchar_t wc = static_cast<wchar_t>(code);
                    value += WideToUtf8(std::wstring(1, wc));
                    continue;
                }
                default: c = e; break;  // \" \\ \/
                }
            }
            value += c;
        }
        out.push_back(Utf8ToWide(value));
    }
    return out;
}

// The Start menu's pinned apps, in their pinned order, as launchpad targets.
// Windows 11 keeps the pins in an encrypted file, so the supported route is
// PowerShell's Export-StartLayout, which writes them as JSON.
std::vector<LaunchpadApp> StartMenuPins()
{
    std::vector<LaunchpadApp> apps;

    wchar_t temp[MAX_PATH] = {};
    if (GetTempPathW(MAX_PATH, temp) == 0)
        return apps;
    const std::wstring jsonPath = std::wstring(temp) + L"monopane-startlayout.json";
    DeleteFileW(jsonPath.c_str());

    std::wstring cmd = L"powershell.exe -NoProfile -NonInteractive -ExecutionPolicy Bypass "
                       L"-Command \"Export-StartLayout -Path '" + jsonPath + L"'\"";
    STARTUPINFOW si{ sizeof(si) };
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION pi{};
    if (!CreateProcessW(nullptr, &cmd[0], nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr,
                        nullptr, &si, &pi))
        return apps;
    WaitForSingleObject(pi.hProcess, 20000);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);

    std::string json;
    if (!ReadWholeFile(jsonPath, json)) {
        return apps;
    }
    DeleteFileW(jsonPath.c_str());

    // The two kinds are interleaved in pin order, so walk the file once and
    // take whichever key comes next.
    size_t pos = 0;
    while (pos < json.size()) {
        const size_t link = json.find("\"desktopAppLink\":", pos);
        const size_t pkg = json.find("\"packagedAppId\":", pos);
        if (link == std::string::npos && pkg == std::string::npos)
            break;
        const bool isLink = link != std::string::npos && (pkg == std::string::npos || link < pkg);
        const size_t at = isLink ? link : pkg;
        const char* key = isLink ? "\"desktopAppLink\":" : "\"packagedAppId\":";
        const std::vector<std::wstring> values = JsonStringsAfter(json.substr(at), key);
        if (!values.empty() && !values[0].empty()) {
            LaunchpadApp app;
            app.target = isLink ? values[0] : std::wstring(kAppPrefix) + values[0];
            apps.push_back(std::move(app));
        }
        pos = at + strlen(key);
    }
    return apps;
}

// -------------------------------------------------------------------- shell

// Reads a 32-bit bitmap's pixels as top-down BGRA, alpha included.
//
// GetDIBits is the obvious way to do this and the wrong one: it hands back the
// colour channels with the alpha zeroed, and premultiplied art with no alpha
// left draws as nothing at all. A DIB section can be read where it lies
// instead; anything else is blitted into one first, which copies all four
// channels rather than interpreting them.
bool ReadBitmapPixels(HBITMAP bitmap, int w, int h, std::vector<uint8_t>& out)
{
    out.assign(static_cast<size_t>(w) * h * 4, 0);

    DIBSECTION ds{};
    if (GetObjectW(bitmap, sizeof(ds), &ds) == sizeof(ds) && ds.dsBm.bmBits &&
        ds.dsBm.bmBitsPixel == 32 && ds.dsBm.bmWidth == w && ds.dsBm.bmHeight == h) {
        const auto* bits = static_cast<const uint8_t*>(ds.dsBm.bmBits);
        const size_t stride = static_cast<size_t>(ds.dsBm.bmWidthBytes);
        const bool bottomUp = ds.dsBmih.biHeight > 0;
        for (int y = 0; y < h; ++y) {
            const size_t row = static_cast<size_t>(bottomUp ? h - 1 - y : y);
            memcpy(out.data() + static_cast<size_t>(y) * w * 4, bits + row * stride,
                   static_cast<size_t>(w) * 4);
        }
        return true;
    }

    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(info.bmiHeader);
    info.bmiHeader.biWidth = w;
    info.bmiHeader.biHeight = -h;  // top-down
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;

    HDC screen = GetDC(nullptr);
    void* bits = nullptr;
    HBITMAP copy = CreateDIBSection(screen, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
    bool ok = false;
    if (copy && bits) {
        HDC srcDC = CreateCompatibleDC(screen);
        HDC dstDC = CreateCompatibleDC(screen);
        HGDIOBJ oldSrc = SelectObject(srcDC, bitmap);
        HGDIOBJ oldDst = SelectObject(dstDC, copy);
        ok = BitBlt(dstDC, 0, 0, w, h, srcDC, 0, 0, SRCCOPY) != FALSE;
        SelectObject(srcDC, oldSrc);
        SelectObject(dstDC, oldDst);
        DeleteDC(srcDC);
        DeleteDC(dstDC);
        if (ok)
            memcpy(out.data(), bits, out.size());
    }
    if (copy)
        DeleteObject(copy);
    ReleaseDC(nullptr, screen);
    return ok;
}

// Alpha at or above which a pixel counts as part of the icon rather than as
// the soft edge around it.
constexpr int kSolidAlpha = 160;

// Turns what the shell hands back into a bitmap AlphaBlend can actually draw,
// no larger than dstPx square.
//
// `minFillPercent` rejects a source whose solid art covers less than that
// share of its canvas, by returning null. See ShellIcon: a bigger canvas is
// not the same thing as bigger art.
//
// Two things are wrong with it as it arrives. The alpha is *straight*, not
// premultiplied — measured, not assumed: a NordVPN edge pixel comes back
// B=255 G=255 R=255 A=1, and every icon tried had every one of its
// part-transparent pixels carrying colour above its own alpha. AlphaBlend
// takes its source as premultiplied, so it lays almost the whole of that white
// over the background, which is the bright fringe that traces an icon's edge.
// Multiplying the colour by the alpha first is all that takes.
//
// And the frame is whatever size the app ships, stretched to the size asked
// for. A 32-pixel frame pulled up to 48 is what makes an icon look
// stair-stepped, so this asks for something much larger and averages it down.
HBITMAP MakeIconBitmap(HBITMAP src, int dstPx, int minFillPercent)
{
    BITMAP bm{};
    if (dstPx <= 0 || !GetObjectW(src, sizeof(bm), &bm) || bm.bmWidth <= 0 || bm.bmHeight <= 0)
        return nullptr;
    const int sw = bm.bmWidth;
    const int sh = bm.bmHeight;

    std::vector<uint8_t> pixels;
    if (!ReadBitmapPixels(src, sw, sh, pixels))
        return nullptr;

    // A pixel whose colour is above its own alpha cannot be premultiplied, so
    // one of those settles which kind this is. The tolerance is for rounding
    // in art that genuinely is premultiplied, which must not be multiplied a
    // second time.
    int maxAlpha = 0;
    bool straight = false;
    for (size_t i = 0; i + 3 < pixels.size(); i += 4) {
        const int a = pixels[i + 3];
        if (a > maxAlpha)
            maxAlpha = a;
        if (pixels[i] > a + 1 || pixels[i + 1] > a + 1 || pixels[i + 2] > a + 1)
            straight = true;
    }

    if (maxAlpha == 0) {
        // No alpha anywhere is an absence of transparency information rather
        // than a wholly invisible icon, and it covers the whole canvas.
        for (size_t i = 3; i < pixels.size(); i += 4)
            pixels[i] = 255;
    } else if (minFillPercent > 0) {
        int left = sw, top = sh, right = -1, bottom = -1;
        for (int y = 0; y < sh; ++y) {
            for (int x = 0; x < sw; ++x) {
                if (pixels[(static_cast<size_t>(y) * sw + x) * 4 + 3] < kSolidAlpha)
                    continue;
                if (x < left) left = x;
                if (x > right) right = x;
                if (y < top) top = y;
                if (y > bottom) bottom = y;
            }
        }
        const int span = std::max(right - left + 1, bottom - top + 1);
        if (right < 0 || span * 100 < std::max(sw, sh) * minFillPercent)
            return nullptr;
    }

    if (maxAlpha != 0 && straight) {
        for (size_t i = 0; i + 3 < pixels.size(); i += 4) {
            const unsigned a = pixels[i + 3];
            for (int c = 0; c < 3; ++c)
                pixels[i + c] = static_cast<uint8_t>((pixels[i + c] * a + 127) / 255);
        }
    }

    // Only ever shrink. Upscaling here would just be a blockier version of
    // what the shell already does, so a smaller frame stays its own size and
    // is centred in the tile by the caller.
    const bool shrink = sw > dstPx && sh > dstPx;
    const int dw = shrink ? dstPx : sw;
    const int dh = shrink ? dstPx : sh;

    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(info.bmiHeader);
    info.bmiHeader.biWidth = dw;
    info.bmiHeader.biHeight = -dh;  // top-down
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;

    HDC screen = GetDC(nullptr);
    void* dstBits = nullptr;
    HBITMAP dst = CreateDIBSection(screen, &info, DIB_RGB_COLORS, &dstBits, nullptr, 0);
    ReleaseDC(nullptr, screen);
    if (!dst || !dstBits) {
        if (dst)
            DeleteObject(dst);
        return nullptr;
    }

    auto* out = static_cast<uint8_t*>(dstBits);
    if (!shrink) {
        memcpy(out, pixels.data(), pixels.size());
        return dst;
    }

    // A box filter over the source pixels each destination pixel covers.
    // Averaging premultiplied channels is linear, so by this point it needs no
    // un-premultiply dance.
    for (int y = 0; y < dh; ++y) {
        const int y0 = y * sh / dh;
        const int y1 = std::max(y0 + 1, (y + 1) * sh / dh);
        for (int x = 0; x < dw; ++x) {
            const int x0 = x * sw / dw;
            const int x1 = std::max(x0 + 1, (x + 1) * sw / dw);
            unsigned sum[4] = {};
            for (int sy = y0; sy < y1; ++sy) {
                const uint8_t* row = pixels.data() + (static_cast<size_t>(sy) * sw + x0) * 4;
                for (int sx = x0; sx < x1; ++sx, row += 4) {
                    sum[0] += row[0];
                    sum[1] += row[1];
                    sum[2] += row[2];
                    sum[3] += row[3];
                }
            }
            const unsigned n = static_cast<unsigned>(x1 - x0) * static_cast<unsigned>(y1 - y0);
            uint8_t* pixel = out + (static_cast<size_t>(y) * dw + x) * 4;
            for (int c = 0; c < 4; ++c)
                pixel[c] = static_cast<uint8_t>((sum[c] + n / 2) / n);
        }
    }
    return dst;
}

HBITMAP ShellIcon(IShellItem* item, int px)
{
    IShellItemImageFactory* factory = nullptr;
    if (FAILED(item->QueryInterface(IID_PPV_ARGS(&factory))))
        return nullptr;

    // Ask for art well above the size it is drawn at, and let the shell hand
    // back something larger still rather than shrink it first: the bigger the
    // frame the average starts from, the smoother the result.
    //
    // But a bigger canvas is not the same as bigger art. Asked for 192, the
    // shell returns some icons as their native 48 sitting in the middle of a
    // 192 canvas rather than scaled up to fill it — measured: PowerToys and a
    // plain .exe both came back covering a quarter of the width. Averaging
    // that down to 48 would leave a 12-pixel icon adrift in the tile, so the
    // big frame is only worth having when the art really does fill it.
    HBITMAP result = nullptr;
    HBITMAP raw = nullptr;
    const SIZE large{ px * 4, px * 4 };
    if (SUCCEEDED(factory->GetImage(large, SIIGBF_ICONONLY | SIIGBF_BIGGERSIZEOK, &raw)) && raw) {
        BITMAP bm{};
        if (GetObjectW(raw, sizeof(bm), &bm) && bm.bmWidth >= px && bm.bmHeight >= px)
            result = MakeIconBitmap(raw, px, 75);
        DeleteObject(raw);
        raw = nullptr;
    }

    // Nothing bigger to work from, or nothing that used the room: let the
    // shell do the sizing, and take whatever it produces at that size.
    if (!result) {
        const SIZE exact{ px, px };
        if (SUCCEEDED(factory->GetImage(exact, SIIGBF_ICONONLY | SIIGBF_RESIZETOFIT, &raw)) && raw) {
            result = MakeIconBitmap(raw, px, 0);
            if (result)
                DeleteObject(raw);
            else
                result = raw;  // unreadable, but drawing it is better than a blank tile
        }
    }

    factory->Release();
    return result;
}

std::wstring ItemString(IShellItem2* item, const PROPERTYKEY& key)
{
    LPWSTR s = nullptr;
    std::wstring out;
    if (SUCCEEDED(item->GetString(key, &s)) && s) {
        out = s;
        CoTaskMemFree(s);
    }
    return out;
}

std::wstring ItemDisplayName(IShellItem* item, SIGDN kind)
{
    LPWSTR s = nullptr;
    std::wstring out;
    if (SUCCEEDED(item->GetDisplayName(kind, &s)) && s) {
        out = s;
        CoTaskMemFree(s);
    }
    return out;
}

} // namespace

// ---------------------------------------------------------------------------

std::wstring LaunchpadIniPath()
{
    return ConfigDir() + L"\\launchpad.ini";
}

void OpenLaunchpadFolder(const std::vector<LaunchpadApp>& apps)
{
    const std::wstring path = LaunchpadIniPath();
    if (GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES)
        SaveLaunchpad(apps);
    if (GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES) {
        const std::wstring params = L"/select,\"" + path + L"\"";
        ShellExecuteW(nullptr, nullptr, L"explorer.exe", params.c_str(), nullptr, SW_SHOWNORMAL);
        return;
    }
    ShellExecuteW(nullptr, L"open", ConfigDir().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

void ResolveLaunchpadApp(LaunchpadApp& app, int iconPx)
{
    if (app.icon) {
        DeleteObject(app.icon);
        app.icon = nullptr;
    }
    app.displayName.clear();
    app.exePath.clear();
    app.aumid.clear();

    const bool isApp = IsAppTarget(app.target);
    const std::wstring parse = ParsingNameFor(app.target);

    IShellItem2* item = nullptr;
    if (SUCCEEDED(SHCreateItemFromParsingName(parse.c_str(), nullptr, IID_PPV_ARGS(&item)))) {
        app.displayName = ItemDisplayName(item, SIGDN_NORMALDISPLAY);
        app.aumid = ItemString(item, PKEY_AppUserModel_ID);
        // For a shortcut, or an Applications-folder entry backed by one, the
        // exe it points at — what a running window is matched by.
        app.exePath = ItemString(item, PKEY_Link_TargetParsingPath);
        app.icon = ShellIcon(item, iconPx);
        item->Release();
    }

    if (app.exePath.empty() && EndsWithNoCase(parse, L".exe"))
        app.exePath = parse;
    if (!isApp && EndsWithNoCase(parse, L".exe")) {
        // The shell names an exe by its file name; its version resource
        // usually has something friendlier.
        const std::wstring described = ExeDisplayName(parse);
        if (!described.empty())
            app.displayName = described;
    }
    if (app.displayName.empty())
        app.displayName = FileStem(isApp ? app.target.substr(kAppPrefixLen) : parse);
}

void FreeLaunchpadIcons(std::vector<LaunchpadApp>& apps)
{
    for (LaunchpadApp& app : apps) {
        if (app.icon)
            DeleteObject(app.icon);
        app.icon = nullptr;
    }
}

bool LoadLaunchpad(std::vector<LaunchpadApp>& apps, int iconPx, bool force)
{
    const std::wstring path = LaunchpadIniPath();
    FILETIME onDisk{};
    const bool exists = FileWriteTime(path, onDisk);

    if (g_loadedOnce && !force && exists && CompareFileTime(&onDisk, &g_loadedWriteTime) == 0)
        return false;

    std::vector<LaunchpadApp> loaded;
    std::string text;
    if (exists && ReadWholeFile(path, text)) {
        ParseIni(text, loaded);
    } else if (!g_loadedOnce) {
        loaded = StartMenuPins();
    }

    for (LaunchpadApp& app : loaded)
        ResolveLaunchpadApp(app, iconPx);

    FreeLaunchpadIcons(apps);
    apps = std::move(loaded);
    g_loadedOnce = true;

    if (!exists)
        SaveLaunchpad(apps);  // so the seeding happens once, even if it found nothing
    else
        g_loadedWriteTime = onDisk;
    return true;
}

void SaveLaunchpad(const std::vector<LaunchpadApp>& apps)
{
    const std::wstring path = LaunchpadIniPath();
    if (WriteWholeFile(path, FormatIni(apps)))
        FileWriteTime(path, g_loadedWriteTime);
}

bool LaunchLaunchpadApp(const LaunchpadApp& app)
{
    const std::wstring file = ParsingNameFor(app.target);

    std::wstring dir;
    if (!IsAppTarget(app.target) && EndsWithNoCase(file, L".exe")) {
        const size_t slash = file.find_last_of(L"\\/");
        if (slash != std::wstring::npos)
            dir = file.substr(0, slash);
    }

    SHELLEXECUTEINFOW sei{ sizeof(sei) };
    sei.fMask = SEE_MASK_NOASYNC | SEE_MASK_FLAG_NO_UI;
    sei.lpFile = file.c_str();
    sei.lpDirectory = dir.empty() ? nullptr : dir.c_str();
    sei.nShow = SW_SHOWNORMAL;
    return ShellExecuteExW(&sei) != FALSE;
}

std::vector<InstalledApp> EnumerateInstalledApps(int iconPx)
{
    std::vector<InstalledApp> apps;

    IShellItem* folder = nullptr;
    if (FAILED(SHGetKnownFolderItem(FOLDERID_AppsFolder, KF_FLAG_DEFAULT, nullptr,
                                    IID_PPV_ARGS(&folder))))
        return apps;

    IEnumShellItems* items = nullptr;
    if (SUCCEEDED(folder->BindToHandler(nullptr, BHID_EnumItems, IID_PPV_ARGS(&items)))) {
        IShellItem* item = nullptr;
        while (items->Next(1, &item, nullptr) == S_OK && item) {
            const std::wstring parsing = ItemDisplayName(item, SIGDN_PARENTRELATIVEPARSING);
            // Web links ("Visit Java.com") sit in the folder too.
            const bool isUrl = _wcsnicmp(parsing.c_str(), L"http", 4) == 0;
            if (!parsing.empty() && !isUrl) {
                InstalledApp app;
                app.name = ItemDisplayName(item, SIGDN_NORMALDISPLAY);
                app.target = LooksLikePath(parsing) ? parsing : std::wstring(kAppPrefix) + parsing;
                app.icon = ShellIcon(item, iconPx);
                if (app.name.empty())
                    app.name = FileStem(parsing);
                apps.push_back(std::move(app));
            }
            item->Release();
            item = nullptr;
        }
        items->Release();
    }
    folder->Release();

    std::sort(apps.begin(), apps.end(), [](const InstalledApp& a, const InstalledApp& b) {
        return CompareStringOrdinal(a.name.c_str(), -1, b.name.c_str(), -1, TRUE) == CSTR_LESS_THAN;
    });
    return apps;
}
