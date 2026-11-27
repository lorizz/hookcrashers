#include "SaveLayout.h"

#include "SaveCrypto.h"

#include <algorithm>
#include <cstring>

namespace HookCrashers::Save::Layout {

int AddonCountForSize(std::size_t size) {
    if (size < kVanillaSize || (size - kVanillaSize) % kCharacterSize != 0) {
        return -1;
    }
    return static_cast<int>((size - kVanillaSize) / kCharacterSize);
}

std::vector<std::string> MakeKeys(const std::vector<std::string>& names) {
    std::vector<std::string> keys;
    keys.reserve(names.size());
    std::map<std::string, int> seen;
    for (const std::string& name : names) {
        const int count = ++seen[name];
        keys.push_back(count == 1 ? name : name + "#" + std::to_string(count));
    }
    return keys;
}

bool SameLayout(const std::vector<std::string>& a, const std::vector<std::string>& b) {
    return a == b;
}

bool Remap(const std::vector<std::uint8_t>& plain,
           const std::vector<std::string>& oldKeys,
           const std::vector<std::string>& newKeys,
           Stash& stash,
           const InitAddon& initAddon,
           std::vector<std::uint8_t>& out,
           RemapReport& report,
           std::string& error) {
    report = {};
    const std::size_t oldCount = oldKeys.size();
    const std::size_t newCount = newKeys.size();
    if (plain.size() != SizeFor(oldCount)) {
        error = "save size does not match its recorded character list";
        return false;
    }
    if (MakeKeys(oldKeys) != oldKeys || MakeKeys(newKeys) != newKeys) {
        error = "character keys are not unique";
        return false;
    }

    std::vector<std::uint8_t> result(SizeFor(newCount), 0);
    std::memcpy(result.data(), plain.data(), kFirstAddonOffset);

    std::vector<bool> used(oldCount, false);
    for (std::size_t n = 0; n < newCount; ++n) {
        std::uint8_t* target = &result[kFirstAddonOffset + n * kCharacterSize];
        const auto found = std::find(oldKeys.begin(), oldKeys.end(), newKeys[n]);
        if (found != oldKeys.end()) {
            const std::size_t o = static_cast<std::size_t>(found - oldKeys.begin());
            std::memcpy(target, &plain[kFirstAddonOffset + o * kCharacterSize], kCharacterSize);
            used[o] = true;
            ++report.kept;
            continue;
        }
        const auto stashed = stash.find(newKeys[n]);
        if (stashed != stash.end()) {
            std::memcpy(target, stashed->second.data(), kCharacterSize);
            stash.erase(stashed);
            ++report.restored;
            continue;
        }
        if (initAddon) {
            initAddon(n, target);
        }
        ++report.created;
    }

    for (std::size_t o = 0; o < oldCount; ++o) {
        if (used[o]) {
            continue;
        }
        CharacterBytes bytes {};
        std::memcpy(bytes.data(), &plain[kFirstAddonOffset + o * kCharacterSize], kCharacterSize);
        stash[oldKeys[o]] = bytes;
        ++report.stashed;
    }

    const std::size_t workshopBytes = kWorkshopCharacters * kCharacterSize;
    std::memcpy(&result[kFirstAddonOffset + newCount * kCharacterSize],
                &plain[kFirstAddonOffset + oldCount * kCharacterSize],
                workshopBytes + kTailSize);

    Crypto::StoreChecksum(result);
    out.swap(result);
    return true;
}

} // namespace HookCrashers::Save::Layout
