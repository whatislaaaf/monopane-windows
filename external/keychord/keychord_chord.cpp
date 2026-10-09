#include "keychord.h"

namespace keychord {
namespace {

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

// "injected:1D" / "physical:1D" / "1D"
bool ParseKeyToken(std::string token, KeyId& key, KeyOrigin& origin)
{
    Trim(token);
    origin = KeyOrigin::Any;
    const size_t colon = token.find(':');
    if (colon != std::string::npos) {
        std::string tag = token.substr(0, colon);
        Trim(tag);
        if (tag == "injected")      origin = KeyOrigin::Injected;
        else if (tag == "physical") origin = KeyOrigin::Physical;
        else                        return false;
        token = token.substr(colon + 1);
        Trim(token);
    }
    unsigned value = 0;
    if (!ParseHex(token, value) || value == 0)
        return false;
    key = static_cast<KeyId>(value);
    return true;
}

std::string FormatKeyToken(KeyId key, KeyOrigin origin)
{
    char buf[8];
    if (KeyIsExtended(key))
        wsprintfA(buf, "E0%02X", key & 0xFF);
    else
        wsprintfA(buf, "%02X", key & 0xFF);
    const char* tag = origin == KeyOrigin::Injected  ? "injected:"
                      : origin == KeyOrigin::Physical ? "physical:"
                                                   : "";
    return std::string(tag) + buf;
}

} // namespace

bool ParseChord(const std::string& text, KeyChord& out)
{
    out = KeyChord();
    size_t pos = 0;
    while (pos <= text.size()) {
        size_t plus = text.find('+', pos);
        if (plus == std::string::npos)
            plus = text.size();

        KeyId key = kNoKey;
        KeyOrigin origin = KeyOrigin::Any;
        if (!ParseKeyToken(text.substr(pos, plus - pos), key, origin))
            return false;

        // Whatever was parsed before this becomes a modifier; the newest token
        // holds the trigger slot until something later displaces it.
        if (ChordValid(out)) {
            if (out.modCount >= kMaxChordMods)
                return false;
            out.mods[out.modCount] = out.trigger;
            out.modOrigin[out.modCount] = out.triggerOrigin;
            ++out.modCount;
        }
        out.trigger = key;
        out.triggerOrigin = origin;

        if (plus == text.size())
            break;
        pos = plus + 1;
    }
    return ChordValid(out);
}

std::string FormatChord(const KeyChord& c)
{
    std::string out;
    for (uint8_t i = 0; i < c.modCount; ++i)
        out += FormatKeyToken(c.mods[i], c.modOrigin[i]) + "+";
    return out + FormatKeyToken(c.trigger, c.triggerOrigin);
}

std::wstring DescribeChord(const KeyChord& c)
{
    if (!ChordValid(c))
        return L"(none)";
    std::wstring out;
    for (uint8_t i = 0; i < c.modCount; ++i)
        out += std::wstring(KeyName(c.mods[i])) + L" + ";
    return out + KeyName(c.trigger);
}

std::string DescribeChordAscii(const KeyChord& c)
{
    if (!ChordValid(c))
        return "(none)";
    std::string out;
    for (uint8_t i = 0; i < c.modCount; ++i)
        out += KeyNameAscii(c.mods[i]) + " + ";
    return out + KeyNameAscii(c.trigger);
}

bool ChordMatches(const KeyChord& c, const KeyOrigin* held, int heldCount, bool exact)
{
    if (exact && heldCount != c.modCount + 1)
        return false;
    for (uint8_t i = 0; i < c.modCount; ++i) {
        const KeyOrigin down = held[KeySlot(c.mods[i])];
        if (down == KeyOrigin::Any)
            return false; // not held at all
        if (c.modOrigin[i] != KeyOrigin::Any && c.modOrigin[i] != down)
            return false; // held, but not the way this chord requires
    }
    return true;
}

} // namespace keychord
