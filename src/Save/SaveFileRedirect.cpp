#include "SaveFileRedirect.h"

#include "../Core/Logger.h"
#include "../Core/Memory.h"

#include <array>
#include <cstdint>
#include <cstring>
#include <limits>
#include <string>
#include <vector>

namespace HookCrashers::Save::SaveFileRedirect {
namespace {

constexpr std::array<std::uintptr_t, 8> kMainFilenameReferences = {
    0xE8EFB, 0xE8F17, 0xE8F4A, 0xE9107,
    0xE9138, 0xE919C, 0xE93B5, 0x1C46F0
};
constexpr std::array<std::uintptr_t, 3> kBackupFilenameReferences = {
    0xE91D0, 0xE91E7, 0x1C46F4
};
constexpr std::array<std::uintptr_t, 5> kPbpConfigFilenameReferences = {
    0xEA241, 0xEA25D, 0xEA281, 0xEA403, 0x1C46EC
};
constexpr std::uintptr_t kVanillaPbpConfigFilenameRva = 0x1CD140;
// "cc_save.dat" and "cc_save.dat.bak" in .rdata (build 0x6A4D42BF).
constexpr std::uintptr_t kVanillaMainFilenameRva = 0x1CD124;
constexpr std::uintptr_t kVanillaBackupFilenameRva = 0x1CD130;

// The game keeps raw pointers to these buffers (patched into its code), so they
// never move; SetSlotFileNames() rewrites them in place for a hot slot switch.
constexpr std::size_t kNameCapacity = 64;
char g_MainName[kNameCapacity] = "hc_mod.dat";
char g_BackupName[kNameCapacity] = "hc_mod.dat.bak";
char g_PbpName[kNameCapacity] = "hcpbp_mod.dat";
std::string g_SaveFileName = g_MainName;
std::string g_BackupFileName = g_BackupName;
std::string g_PbpConfigFileName = g_PbpName;
bool g_Installed = false;

bool CopyName(char (&target)[kNameCapacity], const std::string& value) {
    if (value.empty() || value.size() >= kNameCapacity) {
        Core::Logger::Error("Save filename '%s' does not fit the %u-byte buffer",
                            value.c_str(), static_cast<unsigned>(kNameCapacity));
        return false;
    }
    std::memset(target, 0, kNameCapacity);
    std::memcpy(target, value.data(), value.size());
    return true;
}

std::string SanitizeManifestName(const std::string& input) {
    constexpr char kHookCrashers[] = "hookcrashers";
    std::string sanitized;
    sanitized.reserve(input.size());

    for (std::size_t index = 0; index < input.size();) {
        bool isHookCrashers = index + sizeof(kHookCrashers) - 1 <= input.size();
        for (std::size_t offset = 0; isHookCrashers && offset < sizeof(kHookCrashers) - 1; ++offset) {
            unsigned char ch = static_cast<unsigned char>(input[index + offset]);
            if (ch >= 'A' && ch <= 'Z') {
                ch = static_cast<unsigned char>(ch - 'A' + 'a');
            }
            isHookCrashers = ch == static_cast<unsigned char>(kHookCrashers[offset]);
        }
        if (isHookCrashers) {
            index += sizeof(kHookCrashers) - 1;
            continue;
        }

        unsigned char ch = static_cast<unsigned char>(input[index++]);
        if (ch >= 'A' && ch <= 'Z') {
            ch = static_cast<unsigned char>(ch - 'A' + 'a');
        }
        if ((ch >= 'a' && ch <= 'z') ||
            (ch >= '0' && ch <= '9') ||
            ch == '_' || ch == '-') {
            sanitized.push_back(static_cast<char>(ch));
        }
    }
    return sanitized.empty() ? "mod" : sanitized;
}

bool PointerValue(const char* value, std::uint32_t& result) {
    const std::uintptr_t pointer = reinterpret_cast<std::uintptr_t>(value);
    if (pointer > std::numeric_limits<std::uint32_t>::max()) {
        Core::Logger::Error("Save filename pointer does not fit the x86 address space: %p", value);
        return false;
    }
    result = static_cast<std::uint32_t>(pointer);
    return true;
}

struct SavedPatch {
    std::uintptr_t rva;
    std::uint32_t original;
    std::uint32_t replacement;
};

template <std::size_t N>
bool AppendPatches(const std::array<std::uintptr_t, N>& references,
                   std::uint32_t replacement,
                   std::vector<SavedPatch>& patches) {
    for (std::uintptr_t rva : references) {
        SavedPatch patch { rva, 0, replacement };
        const void* address = reinterpret_cast<const void*>(Core::Memory::Va(rva));
        if (!address) {
            Core::Logger::Error("Cannot read save filename reference rva=0x%08X",
                                static_cast<unsigned int>(rva));
            return false;
        }
        std::memcpy(&patch.original, address, sizeof(patch.original));
        patches.push_back(patch);
    }
    return true;
}

// Like AppendPatches, but every reference must currently point at the vanilla
// string; otherwise nothing is queued (the vanilla save must never be redirected
// half-way or through an unrelated pointer).
template <std::size_t N>
bool AppendVerifiedPatches(const std::array<std::uintptr_t, N>& references,
                           std::uintptr_t vanillaStringRva,
                           std::uint32_t replacement,
                           const char* label,
                           std::vector<SavedPatch>& patches) {
    const auto expected = static_cast<std::uint32_t>(Core::Memory::Va(vanillaStringRva));
    const std::size_t originalSize = patches.size();
    if (!AppendPatches(references, replacement, patches)) {
        return false;
    }
    for (std::size_t index = originalSize; index < patches.size(); ++index) {
        if (patches[index].original != expected) {
            Core::Logger::Error("Unexpected %s filename reference at rva=0x%08X: expected=%p current=%p",
                                label,
                                static_cast<unsigned int>(patches[index].rva),
                                reinterpret_cast<void*>(static_cast<std::uintptr_t>(expected)),
                                reinterpret_cast<void*>(static_cast<std::uintptr_t>(patches[index].original)));
            patches.resize(originalSize);
            return false;
        }
    }
    return true;
}

bool ApplyPatches(const std::vector<SavedPatch>& patches) {
    std::size_t applied = 0;
    for (; applied < patches.size(); ++applied) {
        const SavedPatch& patch = patches[applied];
        if (!Core::Memory::WriteU32(patch.rva, patch.replacement)) {
            Core::Logger::Error("Save filename patch failed at rva=0x%08X; rolling back",
                                static_cast<unsigned int>(patch.rva));
            while (applied > 0) {
                --applied;
                const SavedPatch& rollback = patches[applied];
                Core::Memory::WriteU32(rollback.rva, rollback.original);
            }
            return false;
        }
    }
    return true;
}

bool AppendPbpConfigPatches(std::uint32_t replacement,
                            std::vector<SavedPatch>& patches) {
    const std::uint32_t expected =
        static_cast<std::uint32_t>(Core::Memory::Va(kVanillaPbpConfigFilenameRva));
    const std::size_t originalSize = patches.size();
    if (!AppendPatches(kPbpConfigFilenameReferences, replacement, patches)) {
        return false;
    }

    for (std::size_t index = originalSize; index < patches.size(); ++index) {
        if (patches[index].original != expected) {
            Core::Logger::Error(
                "Unexpected PBP config filename reference at rva=0x%08X: expected=%p current=%p",
                static_cast<unsigned int>(patches[index].rva),
                reinterpret_cast<void*>(static_cast<std::uintptr_t>(expected)),
                reinterpret_cast<void*>(static_cast<std::uintptr_t>(patches[index].original)));
            patches.resize(originalSize);
            return false;
        }
    }
    return true;
}

} // namespace

bool Install(const std::string& mainName, const std::string& backupName, const std::string& pbpName) {
    if (g_Installed) {
        Core::Logger::Info("Save filename redirect already installed: main='%s' backup='%s' pbp='%s'",
                           g_SaveFileName.c_str(),
                           g_BackupFileName.c_str(),
                           g_PbpConfigFileName.c_str());
        return true;
    }

    if (!CopyName(g_MainName, mainName) || !CopyName(g_BackupName, backupName) || !CopyName(g_PbpName, pbpName)) {
        return false;
    }
    g_SaveFileName = mainName;
    g_BackupFileName = backupName;
    g_PbpConfigFileName = pbpName;

    std::uint32_t mainPointer = 0;
    std::uint32_t backupPointer = 0;
    std::uint32_t pbpConfigPointer = 0;
    if (!PointerValue(g_MainName, mainPointer) ||
        !PointerValue(g_BackupName, backupPointer) ||
        !PointerValue(g_PbpName, pbpConfigPointer)) {
        return false;
    }

    std::vector<SavedPatch> patches;
    patches.reserve(kMainFilenameReferences.size() +
                    kBackupFilenameReferences.size() +
                    kPbpConfigFilenameReferences.size());
    if (!AppendVerifiedPatches(kMainFilenameReferences, kVanillaMainFilenameRva, mainPointer, "save", patches) ||
        !AppendVerifiedPatches(kBackupFilenameReferences, kVanillaBackupFilenameRva, backupPointer, "backup", patches) ||
        !AppendPbpConfigPatches(pbpConfigPointer, patches) ||
        !ApplyPatches(patches)) {
        return false;
    }

    g_Installed = true;
    Core::Logger::Info("Save filename redirect installed: main='%s' backup='%s' pbp='%s'",
                       g_SaveFileName.c_str(),
                       g_BackupFileName.c_str(),
                       g_PbpConfigFileName.c_str());
    return true;
}

bool SetSlotFileNames(const std::string& mainName, const std::string& backupName) {
    if (!g_Installed) return false;
    char main[kNameCapacity];
    char backup[kNameCapacity];
    if (!CopyName(main, mainName) || !CopyName(backup, backupName)) return false;
    std::memcpy(g_MainName, main, kNameCapacity);
    std::memcpy(g_BackupName, backup, kNameCapacity);
    g_SaveFileName = mainName;
    g_BackupFileName = backupName;
    Core::Logger::Info("Save filename redirect now targets main='%s' backup='%s'", mainName.c_str(), backupName.c_str());
    return true;
}

std::string LegacyManifestSaveName(const std::string& manifestName) {
    return "hc_" + SanitizeManifestName(manifestName) + ".dat";
}

std::string LegacyManifestPbpName(const std::string& manifestName) {
    return "hcpbp_" + SanitizeManifestName(manifestName) + ".dat";
}

const std::string& SaveFileName() {
    return g_SaveFileName;
}

const std::string& BackupFileName() {
    return g_BackupFileName;
}

const std::string& PbpConfigFileName() {
    return g_PbpConfigFileName;
}

} // namespace HookCrashers::Save::SaveFileRedirect
