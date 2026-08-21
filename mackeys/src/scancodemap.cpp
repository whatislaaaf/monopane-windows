#include "scancodemap.h"

#include <shellapi.h>

#include "keyboard.h"

namespace {

constexpr wchar_t kMapKeyPath[] = L"SYSTEM\\CurrentControlSet\\Control\\Keyboard Layout";
constexpr wchar_t kMapValueName[] = L"Scancode Map";

constexpr size_t kMaxEntries = 8;

// Everything the app is ever allowed to turn a key into. Keeping the
// destination side to a three-item whitelist is what makes a bad write hard:
// no key can be disabled (destination 0) or turned into something surprising.
bool AllowedDestination(KeyId dst)
{
    return dst == kSpareF23 || dst == kSpareF24 || dst == kScRightWin;
}

void AppendDword(std::vector<BYTE>& blob, DWORD v)
{
    blob.push_back(static_cast<BYTE>(v & 0xFF));
    blob.push_back(static_cast<BYTE>((v >> 8) & 0xFF));
    blob.push_back(static_cast<BYTE>((v >> 16) & 0xFF));
    blob.push_back(static_cast<BYTE>((v >> 24) & 0xFF));
}

DWORD ReadDword(const std::vector<BYTE>& blob, size_t offset)
{
    return static_cast<DWORD>(blob[offset]) |
           (static_cast<DWORD>(blob[offset + 1]) << 8) |
           (static_cast<DWORD>(blob[offset + 2]) << 16) |
           (static_cast<DWORD>(blob[offset + 3]) << 24);
}

std::wstring Note(KeyId from, const wchar_t* becomes)
{
    return std::wstring(KeyName(from)) + L" \x2192 " + becomes;
}

// Runs reg.exe elevated and waits for it. Returns the exit code, or -1 if the
// user dismissed the UAC prompt.
int RunElevatedReg(const std::wstring& params)
{
    SHELLEXECUTEINFOW sei = { sizeof(sei) };
    sei.fMask = SEE_MASK_NOCLOSEPROCESS;
    sei.lpVerb = L"runas";
    sei.lpFile = L"reg.exe";
    sei.lpParameters = params.c_str();
    sei.nShow = SW_HIDE;
    if (!ShellExecuteExW(&sei) || !sei.hProcess)
        return -1;
    WaitForSingleObject(sei.hProcess, 15000);
    DWORD code = 1;
    GetExitCodeProcess(sei.hProcess, &code);
    CloseHandle(sei.hProcess);
    return static_cast<int>(code);
}

std::wstring QuotedMapKey()
{
    return std::wstring(L"\"HKLM\\") + kMapKeyPath + L"\"";
}

} // namespace

ScancodeMapPlan BuildScancodeMap(const Config& cfg)
{
    ScancodeMapPlan plan;

    // Pass 1 — a Win key doing a non-Windows job has to stop being a Win key
    // before Windows sees it. Left keeps F23 and right keeps F24, matching the
    // historical map so an existing install is not disturbed.
    const KeyId winKeys[2] = { kScLeftWin, kScRightWin };
    const KeyId spares[2] = { kSpareF23, kSpareF24 };
    for (int i = 0; i < 2; ++i) {
        const Bind& b = cfg.base[KeySlot(winKeys[i])];
        if (b.action == Action::None || b.action == Action::WinKey)
            continue;
        plan.diverts.push_back({ winKeys[i], spares[i] });
        plan.notes.push_back(Note(winKeys[i], i == 0 ? L"F23 (so it stops locking the PC)"
                                                     : L"F24 (so it stops locking the PC)"));
    }

    // Pass 2 — whichever key was given the Windows-key job becomes the real
    // one. A Win key already holding that job needs no entry.
    bool winAssigned = false;
    std::vector<Divert> winEntry;
    for (int slot = 0; slot < kKeySlots; ++slot) {
        if (cfg.base[slot].action != Action::WinKey)
            continue;
        const KeyId id = static_cast<KeyId>((slot & 0x100) ? (0xE000 | (slot & 0xFF))
                                                           : (slot & 0xFF));
        if (id == kScLeftWin || id == kScRightWin)
            continue; // already a Windows key
        if (winAssigned) {
            plan.overflow = true;
            continue;
        }
        winAssigned = true;
        winEntry.push_back({ id, kScRightWin });
        plan.notes.push_back(Note(id, L"the Windows key"));
    }

    std::vector<Divert> entries = plan.diverts;
    entries.insert(entries.end(), winEntry.begin(), winEntry.end());
    if (entries.empty())
        return plan; // empty blob: the value should not exist

    if (entries.size() > kMaxEntries) {
        entries.resize(kMaxEntries);
        plan.overflow = true;
    }

    plan.blob.reserve(16 + 4 * entries.size());
    AppendDword(plan.blob, 0); // version
    AppendDword(plan.blob, 0); // flags
    AppendDword(plan.blob, static_cast<DWORD>(entries.size() + 1));
    // Each entry is one DWORD: the low word is the scancode Windows will see,
    // the high word the one the hardware sent. (The historical MX Keys map
    // reads 76 00 5C E0 — F24 in the low word, right Win in the high word.)
    for (const Divert& e : entries)
        AppendDword(plan.blob, (static_cast<DWORD>(e.from) << 16) | e.to);
    AppendDword(plan.blob, 0); // terminator

    if (!ValidateScancodeMap(plan.blob)) {
        // Should be unreachable; refusing to hand back a blob is the safe
        // failure, since an unvalidated map would apply at boot.
        plan.blob.clear();
        plan.diverts.clear();
        plan.notes.clear();
        plan.overflow = true;
    }
    return plan;
}

bool ValidateScancodeMap(const std::vector<BYTE>& blob)
{
    if (blob.size() < 20 || blob.size() % 4 != 0)
        return false;
    const size_t entries = blob.size() / 4 - 4;
    if (entries < 1 || entries > kMaxEntries)
        return false;
    if (ReadDword(blob, 0) != 0 || ReadDword(blob, 4) != 0)
        return false;
    if (ReadDword(blob, 8) != entries + 1)
        return false;
    if (ReadDword(blob, blob.size() - 4) != 0)
        return false;

    std::vector<KeyId> seen;
    for (size_t i = 0; i < entries; ++i) {
        const DWORD e = ReadDword(blob, 12 + i * 4);
        const KeyId dst = static_cast<KeyId>(e & 0xFFFF);
        const KeyId src = static_cast<KeyId>((e >> 16) & 0xFFFF);
        if (src == 0 || dst == 0 || !AllowedDestination(dst))
            return false;
        for (KeyId s : seen)
            if (s == src)
                return false;
        seen.push_back(src);
    }
    return true;
}

bool ReadCurrentScancodeMap(std::vector<BYTE>& out)
{
    out.clear();
    HKEY key;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, kMapKeyPath, 0, KEY_QUERY_VALUE, &key) != ERROR_SUCCESS)
        return false;
    DWORD type = 0, size = 0;
    bool found = false;
    if (RegQueryValueExW(key, kMapValueName, nullptr, &type, nullptr, &size) == ERROR_SUCCESS &&
        type == REG_BINARY && size > 0 && size < 4096) {
        out.resize(size);
        found = RegQueryValueExW(key, kMapValueName, nullptr, &type, out.data(), &size) ==
                ERROR_SUCCESS;
        if (!found)
            out.clear();
    }
    RegCloseKey(key);
    return found;
}

bool ScancodeMapIsCurrent(const ScancodeMapPlan& plan)
{
    std::vector<BYTE> current;
    const bool present = ReadCurrentScancodeMap(current);
    if (plan.blob.empty())
        return !present;
    return present && current == plan.blob;
}

bool ForeignScancodeMapPresent(const ScancodeMapPlan& plan)
{
    std::vector<BYTE> current;
    if (!ReadCurrentScancodeMap(current))
        return false;
    return current != plan.blob;
}

bool ApplyScancodeMap(const std::vector<BYTE>& blob)
{
    if (blob.empty())
        return ResetScancodeMap();
    if (!ValidateScancodeMap(blob))
        return false;

    std::wstring hex;
    hex.reserve(blob.size() * 2);
    for (BYTE b : blob) {
        wchar_t pair[3];
        wsprintfW(pair, L"%02x", b);
        hex += pair;
    }

    const std::wstring params = L"add " + QuotedMapKey() + L" /v \"" + kMapValueName +
                                L"\" /t REG_BINARY /d " + hex + L" /f";
    return RunElevatedReg(params) == 0;
}

bool ResetScancodeMap()
{
    std::vector<BYTE> current;
    if (!ReadCurrentScancodeMap(current))
        return true; // already absent

    const std::wstring params = L"delete " + QuotedMapKey() + L" /v \"" + kMapValueName + L"\" /f";
    return RunElevatedReg(params) == 0;
}
