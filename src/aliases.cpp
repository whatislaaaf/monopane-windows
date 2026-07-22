#include "aliases.h"

#include <windows.h>
#include <cwctype>
#include <unordered_map>

namespace {

constexpr wchar_t kAliasKey[] = L"Software\\Monopane\\Aliases";

std::unordered_map<std::wstring, std::wstring> g_aliases;

std::wstring ToLower(const std::wstring& s)
{
    std::wstring out = s;
    for (wchar_t& c : out)
        c = towlower(c);
    return out;
}

} // namespace

void LoadAliases()
{
    g_aliases.clear();
    HKEY key;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kAliasKey, 0, KEY_QUERY_VALUE, &key) != ERROR_SUCCESS)
        return;

    for (DWORD i = 0;; ++i) {
        wchar_t name[256];
        wchar_t data[1024];
        DWORD nameLen = 256;
        DWORD dataLen = sizeof(data) - sizeof(wchar_t);
        DWORD type = 0;
        if (RegEnumValueW(key, i, name, &nameLen, nullptr, &type,
                          reinterpret_cast<BYTE*>(data), &dataLen) != ERROR_SUCCESS)
            break;
        if (type != REG_SZ)
            continue;
        data[dataLen / sizeof(wchar_t)] = L'\0';  // REG_SZ may lack a terminator
        g_aliases[name] = data;
    }
    RegCloseKey(key);
}

void SaveAlias(const std::wstring& query, const std::wstring& exePath)
{
    if (query.empty() || exePath.empty())
        return;
    const std::wstring lowered = ToLower(query);
    g_aliases[lowered] = exePath;

    HKEY key;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, kAliasKey, 0, nullptr, 0,
                        KEY_SET_VALUE, nullptr, &key, nullptr) != ERROR_SUCCESS)
        return;
    RegSetValueExW(key, lowered.c_str(), 0, REG_SZ,
                   reinterpret_cast<const BYTE*>(exePath.c_str()),
                   static_cast<DWORD>((exePath.size() + 1) * sizeof(wchar_t)));
    RegCloseKey(key);
}

std::wstring LookupAlias(const std::wstring& query)
{
    if (query.empty())
        return L"";
    const auto it = g_aliases.find(ToLower(query));
    return it == g_aliases.end() ? L"" : it->second;
}

void ClearAliases()
{
    g_aliases.clear();
    RegDeleteKeyW(HKEY_CURRENT_USER, kAliasKey);
}
