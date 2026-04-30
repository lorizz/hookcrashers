#pragma once

#include <string>
#include <vector>
#include <cstdint>

namespace HookCrashers::Localization {

    // La nostra struttura interna per tenere in memoria le stringhe caricate dal JSON
    struct CustomLocalizationEntry {
        int numericId;
        std::string logicalId;
        std::vector<std::wstring> languages;
    };

}