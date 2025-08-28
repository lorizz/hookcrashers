#pragma once

#include <string>
#include <vector>
#include <cstdint>

namespace HookCrashers {
    namespace Core {
        namespace Localization {

            // Represents a single localization entry with all its translations.
            // This is what a mod will provide.
            struct ModLocalizationEntry {
                std::string logicalId;
                std::wstring languages[11];
            };

            // This represents the structure the game uses in its static data table.
#pragma pack(push, 1)
            struct GameLocalizationEntry {
                int id;
                const wchar_t* languages[11];
            };
#pragma pack(pop)
        }
    }
}