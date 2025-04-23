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

        using OriginalAddString_t = uint16_t(__thiscall*)(
            StringManagerContext* pStringManager,
            uint32_t hash_or_index,
            const char* stringToAdd
            );
        static OriginalAddString_t g_originalFunction = nullptr;

        constexpr uintptr_t ADD_STRING_OFFSET = 0x5D4B0;

        uint16_t __fastcall DetouredAddStringReturnID(
            StringManagerContext* pStringManager,
            void* /* edxUnused */,
            uint32_t hash_or_index,
            const char* stringToAdd
        ) {
            uint16_t resultId = 0;

            if (g_originalFunction) {
                try {
                    resultId = g_originalFunction(pStringManager, hash_or_index, stringToAdd);
                }
                catch (const std::exception& e) {
                    L.Get()->error("!!! std::exception in original AddStringReturnID: {} !!!", e.what());
                    resultId = 0;
                }
                catch (...) {
                    L.Get()->critical("!!! Unknown exception in original AddStringReturnID !!!");
                    resultId = 0;
                }
            }
            else {
                L.Get()->error("Original AddStringReturnID function pointer is null!");
                resultId = 0;
            }

            if (resultId != 0 && stringToAdd) {
                Util::StringCache::CacheString(resultId, stringToAdd);
            }

            return resultId;
        }

        bool SetupAddStringHook(uintptr_t moduleBase) {
            L.Get()->info("Setting up AddStringReturnID hook (Target Offset: {:#x})...", ADD_STRING_OFFSET);
            uintptr_t targetAddress = moduleBase + ADD_STRING_OFFSET;
            g_originalFunction = reinterpret_cast<OriginalAddString_t>(targetAddress);

            if (!g_originalFunction) {
                L.Get()->error("Target address for AddStringReturnID is invalid (0x{:X})", targetAddress);
                return false;
            }

            DetourTransactionBegin();
            DetourUpdateThread(GetCurrentThread());
            LONG error = DetourAttach(&(PVOID&)g_originalFunction, DetouredAddStringReturnID);
            if (error != NO_ERROR) {
                L.Get()->error("DetourAttach failed for AddStringReturnID: {}", error);
                DetourTransactionAbort();
                g_originalFunction = nullptr;
                return false;
            }
            error = DetourTransactionCommit();
            if (error != NO_ERROR) {
                L.Get()->error("DetourTransactionCommit failed for AddStringReturnID: {}", error);
                g_originalFunction = nullptr;
                return false;
            }

            L.Get()->info("AddStringReturnID hook attached successfully at 0x{:X}", targetAddress);
            L.Get()->flush();
            return true;
        }

    }
}