#include "DisplayListDepthHook.h"

#include "../SWF/Runtime/SWFRuntimeInjector.h"
#include "../Util/Logger.h"

#include <windows.h>
#include <detours.h>
#include <cstdint>

namespace HookCrashers::Core {
namespace {
    constexpr uintptr_t kInsertDisplayObjectRva = 0x117A80;
    constexpr uintptr_t kSortDepthOffset = 0x20;
    constexpr uintptr_t kTagDepthOffset = 0xB2;

    using InsertDisplayObjectFn = int(__thiscall*)(int* displayList, int displayObject, char replaceSameDepth);
    InsertDisplayObjectFn g_originalInsertDisplayObject = nullptr;

    int __fastcall DetouredInsertDisplayObject(int* displayList, void*, int displayObject, char replaceSameDepth) {
        if (displayObject) {
            const uint16_t tagDepth = *reinterpret_cast<uint16_t*>(displayObject + kTagDepthOffset);
            const int sortDepth = *reinterpret_cast<int*>(displayObject + kSortDepthOffset);

            if (sortDepth == 0 && tagDepth > 0) {
                HookCrashers::SWF::Runtime::TryPatchDisplayObjectSortDepth(
                    reinterpret_cast<void*>(displayObject),
                    static_cast<int>(tagDepth));
            }
        }

        return g_originalInsertDisplayObject(displayList, displayObject, replaceSameDepth);
    }
}

bool SetupDisplayListDepthHook(uintptr_t moduleBase) {
    if (!moduleBase) {
        return false;
    }
    if (g_originalInsertDisplayObject) {
        return true;
    }

    uintptr_t targetAddress = moduleBase + kInsertDisplayObjectRva;
    g_originalInsertDisplayObject = reinterpret_cast<InsertDisplayObjectFn>(targetAddress);
    Util::Logger::Instance().Get()->debug(
        "[SWFInject] Installing hook | name=DisplayListDepth | RVA=0x{:X} | VA=0x{:X}.",
        kInsertDisplayObjectRva,
        targetAddress);

    DetourTransactionBegin();
    DetourUpdateThread(GetCurrentThread());
    LONG error = DetourAttach(&(PVOID&)g_originalInsertDisplayObject, DetouredInsertDisplayObject);
    if (error != NO_ERROR) {
        Util::Logger::Instance().Get()->error("[SWFInject] Display-list depth DetourAttach failed: {}.", error);
        DetourTransactionAbort();
        g_originalInsertDisplayObject = nullptr;
        return false;
    }

    error = DetourTransactionCommit();
    if (error != NO_ERROR) {
        Util::Logger::Instance().Get()->error("[SWFInject] Display-list depth DetourTransactionCommit failed: {}.", error);
        g_originalInsertDisplayObject = nullptr;
        return false;
    }

    Util::Logger::Instance().Get()->debug("[SWFInject] Hook installed | name=DisplayListDepth.");
    return true;
}
}
