#include "config.h"

#include <shellapi.h>
#include <string>

#include "keyboard.h"

Config g_config;

namespace {

constexpr wchar_t kLegacyKey[] = L"Software\\MacKeys";

std::wstring ModuleDir()
{
    wchar_t path[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    std::wstring s = path;
    const size_t cut = s.find_last_of(L'\\');
    return cut == std::wstring::npos ? std::wstring(L".") : s.substr(0, cut);
}

// ------------------------------------------------------------- ini scanning

void Trim(std::string& s)
{
    const size_t b = s.find_first_not_of(" \t");
    if (b == std::string::npos) {
        s.clear();
        return;
    }
    const size_t e = s.find_last_not_of(" \t");
    s = s.substr(b, e - b + 1);
}

// Values are hex digits and lowercase keywords only, so # and ; can never be
// part of one and always start a trailing comment.
void StripComment(std::string& s)
{
    const size_t cut = s.find_first_of("#;");
    if (cut != std::string::npos)
        s.erase(cut);
}

bool ParseHex(const std::string& s, unsigned& out)
{
    if (s.empty() || s.size() > 4)
        return false;
    unsigned v = 0;
    for (char c : s) {
        v <<= 4;
        if (c >= '0' && c <= '9') v |= static_cast<unsigned>(c - '0');
        else if (c >= 'a' && c <= 'f') v |= static_cast<unsigned>(c - 'a' + 10);
        else if (c >= 'A' && c <= 'F') v |= static_cast<unsigned>(c - 'A' + 10);
        else return false;
    }
    out = v;
    return true;
}

bool ParseAction(const std::string& v, Bind& out)
{
    if (v == "none")         { out = Bind(); return true; }
    if (v == "layer")        { out.action = Action::Layer;       out.target = kNoKey; return true; }
    if (v == "winkey")       { out.action = Action::WinKey;      out.target = kNoKey; return true; }
    if (v == "ctrl")         { out.action = Action::CtrlSwap;    out.target = kNoKey; return true; }
    if (v == "desktop:prev") { out.action = Action::DesktopPrev; out.target = kNoKey; return true; }
    if (v == "desktop:next") { out.action = Action::DesktopNext; out.target = kNoKey; return true; }
    if (v.size() > 4 && v.compare(0, 4, "key:") == 0) {
        unsigned target = 0;
        if (!ParseHex(v.substr(4), target) || target == 0)
            return false;
        out.action = Action::Key;
        out.target = static_cast<KeyId>(target);
        return true;
    }
    return false;
}

std::string FormatKeyId(KeyId id)
{
    char buf[8];
    if (KeyIsExtended(id))
        wsprintfA(buf, "E0%02X", id & 0xFF);
    else
        wsprintfA(buf, "%02X", id & 0xFF);
    return buf;
}

std::string FormatAction(const Bind& b)
{
    switch (b.action) {
    case Action::Key:         return "key:" + FormatKeyId(b.target);
    case Action::Layer:       return "layer";
    case Action::WinKey:      return "winkey";
    case Action::CtrlSwap:    return "ctrl";
    case Action::DesktopPrev: return "desktop:prev";
    case Action::DesktopNext: return "desktop:next";
    case Action::None:        break;
    }
    return "none";
}

bool ReadWholeFile(const std::wstring& path, std::string& out)
{
    HANDLE f = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
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

// ------------------------------------------------------------------- legacy

bool LegacyBool(HKEY key, const wchar_t* name, bool fallback)
{
    DWORD value = 0, size = sizeof(value), type = 0;
    if (RegQueryValueExW(key, name, nullptr, &type,
                         reinterpret_cast<BYTE*>(&value), &size) == ERROR_SUCCESS &&
        type == REG_DWORD)
        return value != 0;
    return fallback;
}

// Earlier builds had two booleans instead of a chord list. Ctrl+Space becomes
// an explicit *left* Ctrl chord — the old check merged both Ctrls, and left is
// the one worth keeping. The language switcher hung off whichever key held the
// Ctrl (Mac cmd) role, so it can only be rebuilt when such a key exists.
void SynthesizeLegacyChords(Config& cfg, bool ctrlSpaceStart, bool cmdSpaceLang)
{
    cfg.chords.clear();
    if (ctrlSpaceStart) {
        ChordBinding b;
        b.action = ChordAction::StartMenu;
        b.from.mods[0] = 0x1D;
        b.from.modCount = 1;
        b.from.trigger = 0x39;
        cfg.chords.push_back(b);
    }
    if (!cmdSpaceLang)
        return;
    for (int slot = 0; slot < kKeySlots; ++slot) {
        if (cfg.base[slot].action != Action::CtrlSwap)
            continue;
        ChordBinding b;
        b.action = ChordAction::InputLanguage;
        b.from.mods[0] = static_cast<KeyId>((slot & 0x100) ? (0xE000 | (slot & 0xFF))
                                                           : (slot & 0xFF));
        b.from.modCount = 1;
        b.from.trigger = 0x39;
        cfg.chords.push_back(b);
        return;
    }
}

// Carries the pre-1.1 HKCU settings into the file-based config on first run.
// Only the choices that still mean something on a Windows-layout board move
// across; the cmd/option positions were specific to the MX Keys Mini and are
// left unbound for the picker to assign.
void MigrateLegacySettings(Config& cfg)
{
    HKEY key;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kLegacyKey, 0, KEY_QUERY_VALUE, &key) != ERROR_SUCCESS)
        return;
    if (!LegacyBool(key, L"CapsAsBackspace", true))
        cfg.base[KeySlot(0x3A)] = Bind();
    SynthesizeLegacyChords(cfg, LegacyBool(key, L"CtrlSpaceStart", true),
                           LegacyBool(key, L"CmdSpaceLang", true));
    RegCloseKey(key);
}

void SetBase(Config& cfg, KeyId from, Action action, KeyId target = kNoKey)
{
    cfg.base[KeySlot(from)].action = action;
    cfg.base[KeySlot(from)].target = target;
}

void SetNav(Config& cfg, KeyId from, Action action, KeyId target = kNoKey)
{
    cfg.nav[KeySlot(from)].action = action;
    cfg.nav[KeySlot(from)].target = target;
}

} // namespace

void ResetToDefaults(Config& cfg)
{
    for (int i = 0; i < kKeySlots; ++i) {
        cfg.base[i] = Bind();
        cfg.nav[i] = Bind();
    }
    SetBase(cfg, 0x3A, Action::Key, 0x0E);   // Caps Lock -> Backspace
    SetBase(cfg, 0xE038, Action::Layer);     // Right Alt -> nav layer

    SetNav(cfg, 0x23, Action::Key, 0xE047);  // H -> Home
    SetNav(cfg, 0x24, Action::Key, 0xE04B);  // J -> Left
    SetNav(cfg, 0x25, Action::Key, 0xE050);  // K -> Down
    SetNav(cfg, 0x17, Action::Key, 0xE048);  // I -> Up
    SetNav(cfg, 0x26, Action::Key, 0xE04D);  // L -> Right
    SetNav(cfg, 0x27, Action::Key, 0xE04F);  // ; -> End
    SetNav(cfg, 0x16, Action::DesktopPrev);  // U
    SetNav(cfg, 0x18, Action::DesktopNext);  // O

    // Left Ctrl specifically, not either Ctrl — chords match the physical key.
    cfg.chords.clear();
    ChordBinding start;
    start.action = ChordAction::StartMenu;
    start.from.mods[0] = 0x1D;
    start.from.modCount = 1;
    start.from.trigger = 0x39;
    cfg.chords.push_back(start);

    cfg.layout = BoardLayout::Auto;
}


std::wstring ConfigDir()
{
    // GetEnvironmentVariable rather than SHGetKnownFolderPath: the latter
    // needs CoTaskMemFree, which would put ole32 on the link line.
    wchar_t appdata[MAX_PATH] = {};
    if (GetEnvironmentVariableW(L"APPDATA", appdata, MAX_PATH) == 0)
        return ModuleDir();
    return std::wstring(appdata) + L"\\MacKeys";
}

std::wstring ConfigPath()
{
    return ConfigDir() + L"\\mackeys.ini";
}

void LoadConfig()
{
    ResetToDefaults(g_config);

    std::string text;
    if (!ReadWholeFile(ConfigPath(), text)) {
        MigrateLegacySettings(g_config);
        SaveConfig();
        return;
    }

    // A file that exists is authoritative, including its omissions: a key
    // absent from [base] is unbound, not defaulted.
    for (int i = 0; i < kKeySlots; ++i) {
        g_config.base[i] = Bind();
        g_config.nav[i] = Bind();
    }
    g_config.chords.clear();

    enum class Section { None, Options, Base, Nav, Chords };
    Section section = Section::None;

    // A file predating [chords] still carries the two booleans; they are
    // turned into chords once the whole file is read, when the base table is
    // known and the Ctrl-role key can be found.
    bool sawChordsSection = false;
    bool legacyCtrlSpace = true;
    bool legacyCmdSpace = true;

    size_t pos = 0;
    while (pos < text.size()) {
        size_t eol = text.find('\n', pos);
        if (eol == std::string::npos)
            eol = text.size();
        std::string line = text.substr(pos, eol - pos);
        pos = eol + 1;
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        StripComment(line);
        Trim(line);
        if (line.empty())
            continue;

        if (line.front() == '[') {
            const size_t close = line.find(']');
            const std::string name = close == std::string::npos ? std::string()
                                                                : line.substr(1, close - 1);
            if (name == "options")   section = Section::Options;
            else if (name == "base") section = Section::Base;
            else if (name == "nav")  section = Section::Nav;
            else if (name == "chords") {
                section = Section::Chords;
                sawChordsSection = true;
            } else                   section = Section::None;
            continue;
        }

        const size_t eq = line.find('=');
        if (eq == std::string::npos)
            continue;
        std::string name = line.substr(0, eq);
        std::string value = line.substr(eq + 1);
        Trim(name);
        Trim(value);
        if (name.empty() || value.empty())
            continue;

        if (section == Section::Chords) {
            if (value == "none" || g_config.chords.size() >= kMaxChords)
                continue;
            ChordBinding binding;
            if (name == "start_menu")          binding.action = ChordAction::StartMenu;
            else if (name == "input_language") binding.action = ChordAction::InputLanguage;
            else if (name == "send")           binding.action = ChordAction::SendChord;
            else                               continue;

            std::string fromSpec = value;
            if (binding.action == ChordAction::SendChord) {
                const size_t arrow = value.find('>');
                if (arrow == std::string::npos)
                    continue;
                fromSpec = value.substr(0, arrow);
                std::string toSpec = value.substr(arrow + 1);
                Trim(fromSpec);
                Trim(toSpec);
                if (!ParseChord(toSpec, binding.to))
                    continue;
            }
            if (!ParseChord(fromSpec, binding.from))
                continue;
            g_config.chords.push_back(binding);
            continue;
        }

        if (section == Section::Options) {
            const bool on = value != "0" && value != "false";
            if (name == "ctrl_space_start")
                legacyCtrlSpace = on;
            else if (name == "cmd_space_lang")
                legacyCmdSpace = on;
            else if (name == "layout")
                g_config.layout = value == "ansi" ? BoardLayout::Ansi
                                : value == "iso"  ? BoardLayout::Iso
                                                  : BoardLayout::Auto;
            continue;
        }

        if (section != Section::Base && section != Section::Nav)
            continue;
        unsigned from = 0;
        Bind bind;
        if (!ParseHex(name, from) || from == 0 || !ParseAction(value, bind))
            continue;
        const int slot = KeySlot(static_cast<KeyId>(from));
        if (section == Section::Base)
            g_config.base[slot] = bind;
        else
            g_config.nav[slot] = bind;
    }

    if (!sawChordsSection)
        SynthesizeLegacyChords(g_config, legacyCtrlSpace, legacyCmdSpace);
}

bool SaveConfig()
{
    const std::wstring dir = ConfigDir();
    CreateDirectoryW(dir.c_str(), nullptr);

    std::string out;
    out += "# MacKeys configuration\r\n";
    out += "#\r\n";
    out += "# Keys are set-1 scancodes in hex; E0xx is an extended key\r\n";
    out += "# (E01D = right ctrl). Actions: key:<scancode>, layer, winkey,\r\n";
    out += "# ctrl, desktop:prev, desktop:next, none.\r\n";
    out += "#\r\n";
    out += "# Edited by hand? Restart MacKeys to reload it.\r\n";

    out += "\r\n[options]\r\n";
    out += std::string("layout           = ") +
           (g_config.layout == BoardLayout::Ansi ? "ansi"
            : g_config.layout == BoardLayout::Iso ? "iso"
                                                  : "auto") +
           "\r\n";

    for (int pass = 0; pass < 2; ++pass) {
        const Bind* table = pass == 0 ? g_config.base : g_config.nav;
        out += pass == 0 ? "\r\n[base]\r\n" : "\r\n[nav]\r\n";
        if (pass == 1)
            out += "# Applied only while a layer key is held.\r\n";
        for (int slot = 0; slot < kKeySlots; ++slot) {
            if (table[slot].action == Action::None)
                continue;
            const KeyId id = static_cast<KeyId>((slot & 0x100) ? (0xE000 | (slot & 0xFF))
                                                               : (slot & 0xFF));
            std::string line = FormatKeyId(id) + " = " + FormatAction(table[slot]);
            do {
                line += ' ';
            } while (line.size() < 26);
            out += line + "# " + DescribeBindAscii(id, table[slot]) + "\r\n";
        }
    }

    out += "\r\n[chords]\r\n";
    // ASCII only in here: the file carries no BOM, so a stray UTF-8 character
    // shows up as mojibake in Notepad.
    out += "# <action> = <key>[+<key>...] - the last key is the trigger, the\r\n";
    out += "# rest must be held. Matched exactly: nothing else may be down, so\r\n";
    out += "# 1D+39 is left ctrl and space, and right ctrl will not do.\r\n";
    for (const ChordBinding& c : g_config.chords) {
        if (c.action == ChordAction::None || !ChordValid(c.from))
            continue;
        std::string name = c.action == ChordAction::StartMenu       ? "start_menu"
                           : c.action == ChordAction::InputLanguage ? "input_language"
                                                                    : "send";
        while (name.size() < 14)
            name += ' ';
        std::string line = name + " = " + FormatChord(c.from);
        if (c.action == ChordAction::SendChord)
            line += " > " + FormatChord(c.to);
        // At least one space, however long the line already is, or the comment
        // marker ends up flush against the value.
        do {
            line += ' ';
        } while (line.size() < 40);
        out += line + "# " + DescribeChordBindingAscii(c) + "\r\n";
    }

    HANDLE f = CreateFileW(ConfigPath().c_str(), GENERIC_WRITE, 0, nullptr,
                           CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE)
        return false;
    DWORD written = 0;
    const bool ok = WriteFile(f, out.data(), static_cast<DWORD>(out.size()), &written, nullptr) != 0 &&
                    written == out.size();
    CloseHandle(f);
    return ok;
}

void OpenConfigFolder()
{
    // Write first, so the button never lands on a missing folder or file.
    SaveConfig();

    const std::wstring path = ConfigPath();
    if (GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES) {
        const std::wstring params = L"/select,\"" + path + L"\"";
        ShellExecuteW(nullptr, nullptr, L"explorer.exe", params.c_str(), nullptr, SW_SHOWNORMAL);
        return;
    }
    ShellExecuteW(nullptr, L"open", ConfigDir().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

BoardLayout ResolvedLayout()
{
    return ResolvedLayoutFor(g_config);
}

BoardLayout ResolvedLayoutFor(const Config& cfg)
{
    if (cfg.layout != BoardLayout::Auto)
        return cfg.layout;
    // Scancode 0x56 (the extra key beside left shift) only resolves to a
    // virtual key on ISO layouts. This follows the active *layout*, not the
    // hardware, so [options] layout= overrides it.
    return MapVirtualKeyW(0x56, MAPVK_VSC_TO_VK) != 0 ? BoardLayout::Iso : BoardLayout::Ansi;
}
