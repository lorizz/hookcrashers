#pragma once

// Mod-provided menu graphics. A mod registers an uncompressed (FWS) SWF and the
// name of one exported sprite; when MENU.cok6 loads, HookCrashers copies that
// sprite (and everything it uses) into the menu movie with fresh character ids
// and re-exports it as a private alias ("hcg_<n>") that GraphicMenuItem can
// instantiate. The SWF files themselves are never modified.

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace HookCrashers::SWF::MenuGraphicAssets {

enum class RegisterResult : int {
    Ok = 0,
    InvalidArgument,
    DuplicateId,
    Unsupported,   // see `error` for the reason (compressed, bitmaps, buttons...)
    LimitReached,
};

constexpr std::size_t kMaxGraphics = 64;
constexpr std::size_t kMaxSwfBytes = 8u * 1024 * 1024;

// Pure validation: true when `exportName` is an exported sprite that Import can copy.
bool Validate(const std::uint8_t* swf, std::size_t bytes, const std::string& exportName, std::string& error);

struct Source {
    const std::uint8_t* data = nullptr;
    std::size_t bytes = 0;
    std::string exportName;   // export inside the mod SWF
    std::string alias;        // export name created inside MENU
};

// Pure import seam. Imports every source it can (each one atomically) and
// returns false only when the menu itself is unusable; `imported` receives the
// aliases that were added. `output` is untouched when nothing was imported.
bool Build(const std::uint8_t* menu, std::size_t bytes, const std::vector<Source>& sources,
           std::vector<std::uint8_t>& output, std::vector<std::string>& imported,
           std::vector<std::string>& failures);

// Registry used by the Menu API.
RegisterResult Register(const std::string& id, std::vector<std::uint8_t> swf,
                        const std::string& exportName, std::string& error);
bool IsRegistered(const std::string& id);
std::string AliasFor(const std::string& id);       // "" when unknown
bool IsAvailableInScene(const std::string& id);    // imported into the current MENU
void ClearForTests();

// Windows only: patches the MENU scene buffer before the game parses it.
bool PatchScene(void* scene);

} // namespace HookCrashers::SWF::MenuGraphicAssets
