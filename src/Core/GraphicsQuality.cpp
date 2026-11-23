#include "GraphicsQuality.h"
#include "GraphicsQualityInternal.h"
#include "Memory.h"
#include <windows.h>
#include <detours.h>
#include <cstring>
#include <vector>

namespace HookCrashers::Core::GraphicsQuality {
namespace {
constexpr std::uintptr_t kReadInt=0xB7730, kWriteInt=0xB7810;
constexpr std::uintptr_t kRaster=0xF9F60, kAddOption=0x154E50;
constexpr std::uintptr_t kPlatformGlobal=0x3610F0;
constexpr char kSchemaKey[]="hookcrashers_quality_schema";
// These natives RET 16 / RET 24, including reserved stack argument slots.
// Do not replace these signatures with a three-argument cdecl declaration.
using ReadIntFn=int(__thiscall*)(void*,const char*,const char*,int,std::uint32_t);
using WriteIntFn=void(__thiscall*)(void*,const char*,const char*,int,std::uint32_t,std::uint32_t,std::uint32_t);
using RasterFn=void(__thiscall*)(void*,std::uint32_t,std::uint32_t,std::uint32_t*,std::uint32_t*,std::uint8_t,std::uint32_t);
using AddOptionFn=void(__thiscall*)(void*,int);
ReadIntFn g_ReadInt=nullptr;
WriteIntFn g_WriteInt=nullptr;
RasterFn g_Raster=nullptr;
AddOptionFn g_AddOption=nullptr;
bool g_Installed=false;
int g_Maximum=3;
const char* g_Failure="not installed";

bool IsQualityKey(const char* section,const char* key) {
    return section && key && !std::strcmp(section,"video") && !std::strcmp(key,"tex_quality");
}
int __fastcall ReadInt(void* ini,void*,const char* section,const char* key,int fallback,std::uint32_t pad) {
    const int value=g_ReadInt(ini,section,key,fallback,pad);
    if (!IsQualityKey(section,key)) return value;
    const int schema=g_ReadInt(ini,"video",kSchemaKey,0,0);
    const int decoded=Detail::DecodeSetting(value,schema,g_Maximum);
    if (schema<1 || decoded!=value) {
        // Change value and schema in the same in-memory INI before native save.
        // A later reload must not migrate a deliberately selected High again.
        g_WriteInt(ini,"video","tex_quality",decoded,0,0,0);
        g_WriteInt(ini,"video",kSchemaKey,1,0,0,0);
    }
    return decoded;
}
void __fastcall WriteInt(void* ini,void*,const char* section,const char* key,int value,
                         std::uint32_t pad0,std::uint32_t pad1,std::uint32_t pad2) {
    if (IsQualityKey(section,key)) {
        value=(std::clamp)(value,0,g_Maximum);
        g_WriteInt(ini,"video",kSchemaKey,1,0,0,0);
    }
    g_WriteInt(ini,section,key,value,pad0,pad1,pad2);
}
int CurrentQuality() {
    const auto platform=*reinterpret_cast<const std::uintptr_t*>(Memory::Va(kPlatformGlobal));
    return platform ? *reinterpret_cast<const int*>(platform+0x258) : -1;
}
void __fastcall Raster(void* renderer,void*,std::uint32_t width,std::uint32_t height,
                       std::uint32_t* outWidth,std::uint32_t* outHeight,
                       std::uint8_t powerOfTwo,std::uint32_t threshold) {
    if (CurrentQuality()!=static_cast<int>(Detail::Quality::High)) {
        g_Raster(renderer,width,height,outWidth,outHeight,powerOfTwo,threshold);
        return;
    }
    const auto minimum=*reinterpret_cast<const std::uint32_t*>(static_cast<const std::uint8_t*>(renderer)+0x670);
    *outWidth=Detail::HistoricalHighDimension(width,powerOfTwo!=0,minimum);
    *outHeight=Detail::HistoricalHighDimension(height,powerOfTwo!=0,minimum);
}
void __fastcall AddMediumAndHigh(void* selector,void*,int mediumLabel) {
    g_AddOption(selector,mediumLabel);
    g_AddOption(selector,0x31E); // Original localized High/Alta, all 11 languages.
}

struct Patch {
    std::uintptr_t rva;
    std::vector<std::uint8_t> original,replacement;
};
std::vector<Patch> g_Patches;
std::vector<std::uint8_t> Dword(std::uint32_t value) {
    return {static_cast<std::uint8_t>(value),static_cast<std::uint8_t>(value>>8),
            static_cast<std::uint8_t>(value>>16),static_cast<std::uint8_t>(value>>24)};
}
std::vector<Patch> Patches(int maximum) {
    const auto m=static_cast<std::uint8_t>(maximum);
    auto relative=Dword(static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(&AddMediumAndHigh)-Memory::Va(0x135450)));
    relative.insert(relative.begin(),0xE8);
    return {
        {0x13544B,{0xE8,0x00,0xFA,0x01,0x00},relative}, // Append High after Medium.
        {0x31375,{0x83,0xB8,0x58,0x02,0,0,2},{0x83,0xB8,0x58,0x02,0,0,3}}, // Ultra atlas.
        {0xF9F84,{0x83,0xB8,0x58,0x02,0,0,2},{0x83,0xB8,0x58,0x02,0,0,3}}, // Ultra raster scale.
        {0xF7995,{0x83,0xE8,1,0x74,0x11},{0x83,0xF8,1,0x73,0x11}}, // Minimum 100 for 1..3.
        {0x1361D5,{0x83,0xE8,1,0x74,0x11},{0x83,0xF8,1,0x73,0x11}},
        {0x1363D5,{0x66,0x83,0x78,0x62,2},{0x66,0x83,0x78,0x62,3}}, // Ultra warning.
        {0x135E79,{0xB9,2,0,0,0},{0xB9,m,0,0,0}}, // Reset video UI.
        {0xE051E,{0xC7,0x87,0x58,0x02,0,0,2,0,0,0},{0xC7,0x87,0x58,0x02,0,0,m,0,0,0}},
        {0xE0F56,{0x6A,2},{0x6A,m}}, // Config fallback.
        {0xE0F72,{0xB9,2,0,0,0},{0xB9,3,0,0,0}}, // WOW64 maximum.
        {0xE0F77,{0xB8,1,0,0,0},{0xB8,2,0,0,0}}, // 32-bit OS maximum: High.
        {0xE2438,{0x6A,2},{0x6A,m}}, // Reset default INI.
    };
}
bool Match(std::uintptr_t rva,const std::vector<std::uint8_t>& bytes) {
    return !std::memcmp(reinterpret_cast<const void*>(Memory::Va(rva)),bytes.data(),bytes.size());
}
bool VerifiedNatives() {
    return Match(kReadInt,{0x55,0x8B,0xEC,0x83,0xEC,0x48}) &&
        Match(kReadInt+0xC3,{0xC2,0x10,0}) && Match(kReadInt+0xD7,{0xC2,0x10,0}) &&
        Match(kWriteInt,{0x55,0x8B,0xEC,0x81,0xEC,0x8C,0,0,0}) &&
        Match(kWriteInt+0x88,{0xC2,0x18,0}) && Match(kWriteInt+0x9E,{0xC2,0x18,0}) &&
        Match(kRaster,{0x55,0x8B,0xEC,0x51,0xA1}) &&
        *reinterpret_cast<const std::uint32_t*>(Memory::Va(kRaster+5))==Memory::Va(kPlatformGlobal) &&
        Match(kRaster+0x150,{0xC2,0x18,0}) &&
        Match(kAddOption,{0x55,0x8B,0xEC,0x6A,0xFF,0x68}) && Match(kAddOption+0x1B5,{0xC2,4,0});
}
bool Restore(std::size_t count) {
    bool ok=true;
    while (count) { const auto& p=g_Patches[--count]; ok=Memory::WriteBytes(p.rva,p.original) && ok; }
    return ok;
}
}

bool Install() {
    if (g_Installed) return true;
    BOOL wow=FALSE;
    if (!IsWow64Process(GetCurrentProcess(),&wow)) { g_Failure="cannot determine native quality limit"; return false; }
    g_Maximum=wow ? 3 : 2;
    g_Patches=Patches(g_Maximum);
    if (!VerifiedNatives()) { g_Failure="unsupported native graphics ABI"; return false; }
    for (const auto& p:g_Patches) {
        if (p.original.size()!=p.replacement.size() || !Match(p.rva,p.original)) {
            g_Failure="graphics instruction signature mismatch"; return false;
        }
    }
    g_ReadInt=reinterpret_cast<ReadIntFn>(Memory::Va(kReadInt));
    g_WriteInt=reinterpret_cast<WriteIntFn>(Memory::Va(kWriteInt));
    g_Raster=reinterpret_cast<RasterFn>(Memory::Va(kRaster));
    g_AddOption=reinterpret_cast<AddOptionFn>(Memory::Va(kAddOption));
    // Runs during initial ASI attach, before the game enters its startup code.
    if (DetourTransactionBegin()!=NO_ERROR) { g_Failure="cannot begin graphics hooks"; return false; }
    bool ok=DetourUpdateThread(GetCurrentThread())==NO_ERROR;
    if (ok) ok=DetourAttach(reinterpret_cast<PVOID*>(&g_ReadInt),ReadInt)==NO_ERROR;
    if (ok) ok=DetourAttach(reinterpret_cast<PVOID*>(&g_WriteInt),WriteInt)==NO_ERROR;
    if (ok) ok=DetourAttach(reinterpret_cast<PVOID*>(&g_Raster),Raster)==NO_ERROR;
    std::size_t applied=0;
    if (ok) for (const auto& p:g_Patches) {
        if (!Memory::WriteBytes(p.rva,p.replacement)) { ok=false; break; }
        ++applied;
    }
    if (!ok) {
        DetourTransactionAbort(); Restore(applied); g_Failure="graphics hook preparation failed"; return false;
    }
    if (DetourTransactionCommit()!=NO_ERROR) {
        Restore(applied); g_Failure="graphics hook commit failed"; return false;
    }
    g_Installed=true; g_Failure=""; return true;
}
bool IsInstalled() { return g_Installed; }
const char* FailureReason() { return g_Failure; }
void Shutdown() {
    if (!g_Installed || DetourTransactionBegin()!=NO_ERROR) return;
    bool ok=DetourUpdateThread(GetCurrentThread())==NO_ERROR;
    if (ok) ok=DetourDetach(reinterpret_cast<PVOID*>(&g_Raster),Raster)==NO_ERROR;
    if (ok) ok=DetourDetach(reinterpret_cast<PVOID*>(&g_WriteInt),WriteInt)==NO_ERROR;
    if (ok) ok=DetourDetach(reinterpret_cast<PVOID*>(&g_ReadInt),ReadInt)==NO_ERROR;
    if (!ok) { DetourTransactionAbort(); return; }
    if (DetourTransactionCommit()==NO_ERROR) { Restore(g_Patches.size()); g_Installed=false; }
}
}
