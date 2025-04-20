#include "NativeHooks.h"
#include "../logger.h"

#define LOG_NATIVE(fn, base) \
    Logger::Instance().Get()->info("Loaded native function: {} at {}", #fn, \
    reinterpret_cast<void*>(base + reinterpret_cast<uintptr_t>(fn.Address)))

NativeHooks::NativeInfo<void*> Natives::GetFirstArgument = { nullptr, NativeHooks::CallingConvention::FastCall };
NativeHooks::NativeInfo<void*> Natives::GetNextArgument = { nullptr, NativeHooks::CallingConvention::FastCall };
NativeHooks::NativeInfo<void*> Natives::SetStorageOffset = { nullptr, NativeHooks::CallingConvention::ThisCall };
NativeHooks::NativeInfo<void*> Natives::ReadStorageData = { nullptr, NativeHooks::CallingConvention::ThisCall };

void LoadNatives(uintptr_t moduleBase)
{
    Natives::GetFirstArgument.Address = reinterpret_cast<void*>(moduleBase + 0x74520);
    Natives::GetNextArgument.Address = reinterpret_cast<void*>(moduleBase + 0x74140);
    Natives::SetStorageOffset.Address = reinterpret_cast<void*>(moduleBase + 0x4AF10);
    Natives::ReadStorageData.Address = reinterpret_cast<void*>(moduleBase + 0x4AE40);

    LOG_NATIVE(Natives::GetFirstArgument, moduleBase);
    LOG_NATIVE(Natives::GetNextArgument, moduleBase);
    LOG_NATIVE(Natives::SetStorageOffset, moduleBase);
    LOG_NATIVE(Natives::ReadStorageData, moduleBase);
    Logger::Instance().Get()->flush();
}