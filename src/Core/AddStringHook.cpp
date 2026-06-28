#include "AddStringHook.h"
#include "../Util/Logger.h"
#include "../Util/StringCache.h"
#include <detours.h>
#include <string>

namespace HookCrashers {
    namespace Core {
        static Util::Logger& L = Util::Logger::Instance();

        struct StringManagerContext {
            int current_offset;
            int buffer_end_offset;
            char* string_buffer_start;
            uint32_t unknown_0C;
            uint32_t unknown_10;
            int id_counter;
            uint32_t unknown_18;
            uint32_t* id_hash_table_ptr;
            uint32_t hash_mask;
            uint16_t* reverse_lookup_ptr;
        };

        static StringManagerContext* g_pStringManager;
        static uint32_t g_hashOrIndex;

        using OriginalAddString_t = uint16_t(__thiscall*)(
            StringManagerContext* pStringManager,
            uint32_t hash_or_index,
            const char* stringToAdd
            );
        static OriginalAddString_t g_originalFunction = nullptr;

        constexpr uintptr_t ADD_STRING_OFFSET = 0xFD5F0; // updated

        uint16_t __fastcall DetouredAddStringReturnID(
            StringManagerContext* pStringManager,
            void* /* edxUnused */,
            uint32_t hash_or_index,
            const char* stringToAdd
        ) {
            uint16_t resultId = 0;
            g_pStringManager = pStringManager;
            g_hashOrIndex = hash_or_index;

            if (g_originalFunction) {
                try {
                    resultId = g_originalFunction(pStringManager, hash_or_index, stringToAdd);
                }
                catch (const std::exception&) {
                    resultId = 0;
                }
                catch (...) {
                    resultId = 0;
                }
            }

            if (resultId != 0 && stringToAdd) {
                Util::StringCache::CacheString(resultId, stringToAdd);
            }
            return resultId;
        }

        uint16_t AddCustomString(const char* stringToAdd) {
            if (g_pStringManager != nullptr) {
                uint16_t stringId = DetouredAddStringReturnID(g_pStringManager, nullptr, g_hashOrIndex, stringToAdd);
                //L.Get()->info("Registered custom string to StringCache ID: {}", stringId);
                //L.Get()->flush();
                return stringId; // Return the string ID
            }
            return 0; // Return 0 if string manager is null
        }

        bool SetupAddStringHook(uintptr_t moduleBase) {
            uintptr_t targetAddress = moduleBase + ADD_STRING_OFFSET;
            g_originalFunction = reinterpret_cast<OriginalAddString_t>(targetAddress);
            L.Get()->info("[Hook] Attaching AddString hook at offset 0x{:X} (address=0x{:X}).", ADD_STRING_OFFSET, targetAddress);

            if (!g_originalFunction) {
                L.Get()->error("[Hook] AddString hook failed because the target address is invalid.");
                return false;
            }

            DetourTransactionBegin();
            DetourUpdateThread(GetCurrentThread());
            LONG error = DetourAttach(&(PVOID&)g_originalFunction, DetouredAddStringReturnID);
            if (error != NO_ERROR) {
                L.Get()->error("[Hook] AddString DetourAttach failed at offset 0x{:X}: {}", ADD_STRING_OFFSET, error);
                DetourTransactionAbort();
                g_originalFunction = nullptr;
                return false;
            }
            error = DetourTransactionCommit();
            if (error != NO_ERROR) {
                L.Get()->error("[Hook] AddString DetourTransactionCommit failed at offset 0x{:X}: {}", ADD_STRING_OFFSET, error);
                g_originalFunction = nullptr;
                return false;
            }

            L.Get()->info("[Hook] AddString hook attached successfully at offset 0x{:X} (address=0x{:X}).", ADD_STRING_OFFSET, targetAddress);
            return true;
        }

    }
}
