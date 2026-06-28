#include "StringLookupHook.h"
#include "LocalizationManager.h"
#include <detours.h>
#include <windows.h>
#include "../Util/Logger.h"

namespace HookCrashers::Localization {

    constexpr uintptr_t STRING_LOOKUP_OFFSET = 0x125EA0; // updated // FUN_00ad3430

    using OriginalStringLookup_t = uint16_t(__thiscall*)(void* thisPtr, int16_t stringId);
    static OriginalStringLookup_t g_originalLookup = nullptr;

    uint16_t __fastcall DetouredStringLookup(void* thisPtr, void* edx, int16_t stringId)
    {
        static int s_callCount = 0;
        ++s_callCount;
        LocalizationManager& locManager = LocalizationManager::getInstance();
        const bool isCustomString = locManager.isInitialized() && stringId >= locManager.getBaseCustomId();
        const bool logThisCall = s_callCount <= 80 || (s_callCount % 250) == 0 || isCustomString;
        if (logThisCall) {
            HookCrashers::Util::Logger::Instance().Get()->info(
                "[HookHit] StringLookup ENTER call={} this=0x{:X} string_id={} custom={}",
                s_callCount,
                reinterpret_cast<uintptr_t>(thisPtr),
                stringId,
                isCustomString);
            HookCrashers::Util::Logger::Instance().Get()->flush();
        }

        if (isCustomString)
        {
            int custom_index = stringId - locManager.getBaseCustomId();
            const wchar_t* custom_string = locManager.getStringByIndex(custom_index);

            *(uintptr_t*)((char*)thisPtr + 0xF4) = 0;
            *(uintptr_t*)((char*)thisPtr + 0xF8) = 0;

            if (custom_string) {
                size_t len = wcslen(custom_string);

                *(const wchar_t**)((char*)thisPtr + 0xF8) = custom_string;
                *(uint16_t*)((char*)thisPtr + 0xC8) = static_cast<uint16_t>(len);

                if (logThisCall) {
                    HookCrashers::Util::Logger::Instance().Get()->info("[HookHit] StringLookup LEAVE custom call={} len={}", s_callCount, len);
                    HookCrashers::Util::Logger::Instance().Get()->flush();
                }
                return static_cast<uint16_t>(len);
            }

            *(uint16_t*)((char*)thisPtr + 0xC8) = 0;
            if (logThisCall) {
                HookCrashers::Util::Logger::Instance().Get()->info("[HookHit] StringLookup LEAVE custom call={} len=0", s_callCount);
                HookCrashers::Util::Logger::Instance().Get()->flush();
            }
            return 0;
        }

        if (g_originalLookup) {
            uint16_t result = g_originalLookup(thisPtr, stringId);
            if (logThisCall) {
                HookCrashers::Util::Logger::Instance().Get()->info("[HookHit] StringLookup LEAVE original call={} result={}", s_callCount, result);
                HookCrashers::Util::Logger::Instance().Get()->flush();
            }
            return result;
        }
        return 0;
    }


    bool SetupStringLookupHook(uintptr_t moduleBase) {
        HookCrashers::Util::Logger::Instance().Get()->info("[Hook] Attaching StringLookup hook at offset 0x{:X} (address=0x{:X}).", STRING_LOOKUP_OFFSET, moduleBase + STRING_LOOKUP_OFFSET);

        uintptr_t targetAddress = moduleBase + STRING_LOOKUP_OFFSET;
        g_originalLookup = reinterpret_cast<OriginalStringLookup_t>(targetAddress);

        if (!g_originalLookup) {
            HookCrashers::Util::Logger::Instance().Get()->error("[Hook] StringLookup hook failed because the target address is invalid.");
            return false;
        }

        DetourTransactionBegin();
        DetourUpdateThread(GetCurrentThread());
        if (DetourAttach(&(PVOID&)g_originalLookup, DetouredStringLookup) != NO_ERROR) {
            HookCrashers::Util::Logger::Instance().Get()->error("[Hook] StringLookup DetourAttach failed at offset 0x{:X}.", STRING_LOOKUP_OFFSET);
            DetourTransactionAbort();
            return false;
        }
        if (DetourTransactionCommit() != NO_ERROR) {
            HookCrashers::Util::Logger::Instance().Get()->error("[Hook] StringLookup DetourTransactionCommit failed at offset 0x{:X}.", STRING_LOOKUP_OFFSET);
            return false;
        }

        HookCrashers::Util::Logger::Instance().Get()->info("[Hook] StringLookup hook attached successfully at offset 0x{:X} (address=0x{:X}).", STRING_LOOKUP_OFFSET, moduleBase + STRING_LOOKUP_OFFSET);
        return true;
    }
}
