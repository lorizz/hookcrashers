#include "StringCache.h"
#include "Logger.h"
#include <mutex>

namespace HookCrashers {
    namespace Util {

        std::unordered_map<uint16_t, std::string> StringCache::s_stringMap;
        std::mutex StringCache::s_mutex;

        void StringCache::CacheString(uint16_t id, const char* str) {
            if (id == 0 || !str) return;
            std::string strValue = str;
            if (strValue.empty()) return;

            std::lock_guard<std::mutex> lock(s_mutex);

            auto it = s_stringMap.find(id);
            if (it == s_stringMap.end()) {
                s_stringMap.emplace(id, std::move(strValue));
            }
            else if (it->second != strValue) {
                it->second = std::move(strValue);
            }
        }

        std::string StringCache::GetStringCopyById(uint16_t id) {
            std::lock_guard<std::mutex> lock(s_mutex);

            auto it = s_stringMap.find(id);
            if (it != s_stringMap.end()) {
                return it->second;
            }
            return "";
        }

        std::string StringCache::LookupString(uint16_t id) {
            std::string foundString = GetStringCopyById(id);
            if (!foundString.empty()) {
                return foundString;
            }
            else {
                return "[ID:" + std::to_string(id) + " Not Found]";
            }
        }

    }
}