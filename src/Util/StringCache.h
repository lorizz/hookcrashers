#pragma once

#include "../stdafx.h"
#include <string>
#include <unordered_map>
#include <mutex>
#include <cstdint>

namespace HookCrashers {
    namespace Util {

        class StringCache {
        public:
            static void CacheString(uint16_t id, const char* str);
            static std::string GetStringCopyById(uint16_t id);
            static std::string LookupString(uint16_t id);

        private:
            static std::unordered_map<uint16_t, std::string> s_stringMap;
            static std::mutex s_mutex;
        };

    }
}