#include "SlotMeta.h"

#include <algorithm>
#include <cstdlib>
#include <sstream>

namespace HookCrashers::Save::Slots {
namespace {

std::string Clean(const std::string& value) {
    std::string result;
    result.reserve(value.size());
    for (const char ch : value) {
        result.push_back(ch == '\n' || ch == '\r' || ch == '\t' ? ' ' : ch);
    }
    return result;
}

std::string ToHex(const Layout::CharacterBytes& bytes) {
    static const char kDigits[] = "0123456789abcdef";
    std::string hex;
    hex.reserve(bytes.size() * 2);
    for (const std::uint8_t byte : bytes) {
        hex.push_back(kDigits[byte >> 4]);
        hex.push_back(kDigits[byte & 15]);
    }
    return hex;
}

int HexDigit(char ch) {
    if (ch >= '0' && ch <= '9') return ch - '0';
    if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
    if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
    return -1;
}

bool FromHex(const std::string& hex, Layout::CharacterBytes& bytes) {
    if (hex.size() != bytes.size() * 2) return false;
    for (std::size_t i = 0; i < bytes.size(); ++i) {
        const int high = HexDigit(hex[2 * i]);
        const int low = HexDigit(hex[2 * i + 1]);
        if (high < 0 || low < 0) return false;
        bytes[i] = static_cast<std::uint8_t>(high * 16 + low);
    }
    return true;
}

bool SplitTab(const std::string& value, std::string& left, std::string& right) {
    const std::size_t tab = value.find('\t');
    if (tab == std::string::npos) return false;
    left = value.substr(0, tab);
    right = value.substr(tab + 1);
    return true;
}

bool ParseInt(const std::string& value, std::int64_t& out) {
    if (value.empty()) return false;
    char* end = nullptr;
    const long long parsed = std::strtoll(value.c_str(), &end, 10);
    if (!end || *end != '\0') return false;
    out = parsed;
    return true;
}

} // namespace

std::string ClampUtf8(const std::string& text, std::size_t maxBytes) {
    if (text.size() <= maxBytes) return text;
    std::size_t cut = maxBytes;
    while (cut > 0 && (static_cast<unsigned char>(text[cut]) & 0xC0) == 0x80) --cut;
    return text.substr(0, cut);
}

std::string Serialize(const SlotMeta& meta) {
    std::ostringstream out;
    out << "hcslot=" << meta.index << '\n';
    out << "name=" << Clean(ClampUtf8(meta.name, kMaxNameBytes)) << '\n';
    out << "created=" << meta.created << '\n';
    out << "played=" << meta.played << '\n';
    out << "hc=" << Clean(meta.hcVersion) << '\n';
    out << "layout=" << (meta.layoutKnown ? "known" : "inferred") << '\n';
    for (const std::string& character : meta.characters) {
        out << "char=" << Clean(character) << '\n';
    }
    for (const ModEntry& mod : meta.mods) {
        out << "mod=" << Clean(mod.name) << '\t' << Clean(mod.version) << '\n';
    }
    for (const auto& [key, bytes] : meta.stash) {
        out << "stash=" << Clean(key) << '\t' << ToHex(bytes) << '\n';
    }
    return out.str();
}

bool Parse(const std::string& text, SlotMeta& meta) {
    SlotMeta parsed;
    bool sawHeader = false;
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        const std::size_t equals = line.find('=');
        if (equals == std::string::npos) continue;
        const std::string key = line.substr(0, equals);
        const std::string value = line.substr(equals + 1);
        std::int64_t number = 0;
        if (key == "hcslot") {
            if (!ParseInt(value, number) || number < 1 || number > kMaxSlots) return false;
            parsed.index = static_cast<int>(number);
            sawHeader = true;
        } else if (key == "name") {
            parsed.name = ClampUtf8(value, kMaxNameBytes);
        } else if (key == "created") {
            if (ParseInt(value, number)) parsed.created = number;
        } else if (key == "played") {
            if (ParseInt(value, number)) parsed.played = number;
        } else if (key == "hc") {
            parsed.hcVersion = value;
        } else if (key == "layout") {
            parsed.layoutKnown = value == "known";
        } else if (key == "char") {
            parsed.characters.push_back(value);
        } else if (key == "mod") {
            ModEntry mod;
            if (SplitTab(value, mod.name, mod.version) && !mod.name.empty()) parsed.mods.push_back(mod);
        } else if (key == "stash") {
            std::string name, hex;
            Layout::CharacterBytes bytes {};
            if (SplitTab(value, name, hex) && !name.empty() && FromHex(hex, bytes)) parsed.stash[name] = bytes;
        }
    }
    if (!sawHeader || Layout::MakeKeys(parsed.characters) != parsed.characters) return false;
    meta = std::move(parsed);
    return true;
}

std::string SaveFileName(int index) {
    return "hc_slot" + std::to_string(index) + ".dat";
}

std::string BackupFileName(int index) {
    return SaveFileName(index) + ".bak";
}

std::string MetaFileName(int index) {
    return "hc_slot" + std::to_string(index) + ".meta";
}

std::vector<ModComparison> CompareMods(const std::vector<ModEntry>& saved, const std::vector<ModEntry>& current) {
    std::vector<ModComparison> result;
    for (const ModEntry& mod : saved) {
        ModComparison entry;
        entry.name = mod.name;
        entry.savedVersion = mod.version;
        const auto found = std::find_if(current.begin(), current.end(),
                                        [&](const ModEntry& c) { return c.name == mod.name; });
        if (found == current.end()) {
            entry.state = ModState::Missing;
        } else {
            entry.currentVersion = found->version;
            entry.state = found->version == mod.version ? ModState::Match : ModState::Outdated;
        }
        result.push_back(entry);
    }
    for (const ModEntry& mod : current) {
        const bool known = std::any_of(saved.begin(), saved.end(),
                                       [&](const ModEntry& s) { return s.name == mod.name; });
        if (!known) {
            ModComparison entry;
            entry.name = mod.name;
            entry.currentVersion = mod.version;
            entry.state = ModState::Added;
            result.push_back(entry);
        }
    }
    return result;
}

ModSummary Summarize(const std::vector<ModComparison>& comparison) {
    ModSummary summary;
    for (const ModComparison& entry : comparison) {
        switch (entry.state) {
        case ModState::Match: ++summary.match; break;
        case ModState::Outdated: ++summary.outdated; break;
        case ModState::Missing: ++summary.missing; break;
        case ModState::Added: ++summary.added; break;
        }
    }
    return summary;
}

} // namespace HookCrashers::Save::Slots
