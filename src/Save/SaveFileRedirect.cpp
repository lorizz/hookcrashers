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

std::string g_SaveFileName = "hc_mod.dat";
std::string g_BackupFileName = "hc_mod.dat.bak";
bool g_Installed = false;

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

} // namespace

bool Install(const std::string& manifestName) {
    if (g_Installed) {
        Core::Logger::Info("Save filename redirect already installed: main='%s' backup='%s'",
                           g_SaveFileName.c_str(),
                           g_BackupFileName.c_str());
        return true;
    }

    const std::string saveName = SanitizeManifestName(manifestName);
    g_SaveFileName = "hc_" + saveName + ".dat";
    g_BackupFileName = g_SaveFileName + ".bak";

    std::uint32_t mainPointer = 0;
    std::uint32_t backupPointer = 0;
    if (!PointerValue(g_SaveFileName.c_str(), mainPointer) ||
        !PointerValue(g_BackupFileName.c_str(), backupPointer)) {
        return false;
    }

    std::vector<SavedPatch> patches;
    patches.reserve(kMainFilenameReferences.size() + kBackupFilenameReferences.size());
    if (!AppendPatches(kMainFilenameReferences, mainPointer, patches) ||
        !AppendPatches(kBackupFilenameReferences, backupPointer, patches) ||
        !ApplyPatches(patches)) {
        return false;
    }

    g_Installed = true;
    Core::Logger::Info("Save filename redirect installed from manifest name '%s': main='%s' backup='%s'",
                       manifestName.c_str(),
                       g_SaveFileName.c_str(),
                       g_BackupFileName.c_str());
    return true;
}

const std::string& SaveFileName() {
    return g_SaveFileName;
}

const std::string& BackupFileName() {
    return g_BackupFileName;
}

} // namespace HookCrashers::Save::SaveFileRedirect
