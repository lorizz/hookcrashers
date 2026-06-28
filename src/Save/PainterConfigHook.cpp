#include "PainterConfigHook.h"
#include <windows.h>
#include <detours.h>
#include <string>
#include <sstream>
#include "CharacterConfig.h"
#include "../Util/Logger.h"

namespace HookCrashers::Save {

    static std::string to_hex(void* ptr) {
        std::stringstream ss;
        ss << "0x" << std::hex << std::uppercase << (uintptr_t)ptr;
        return ss.str();
    }

    static const uintptr_t RVA_PAINTER_ENTRY = 0x5C0C0; // updated
    static const uintptr_t RVA_XML_INIT = 0xA4A80; // updated -> parent changed behaviour!
    static const uintptr_t RVA_XML_LOAD = 0xA4FC0; // updated
    static const uintptr_t RVA_XML_PARSE = 0x5C190; // updated
    static const uintptr_t RVA_XML_CLEAN = 0xA4CC0; // updated
    static const uintptr_t RVA_NOTIFY = 0x261C0; // same!!
    static const uintptr_t RVA_GLOBAL_VAR = 0x1CCF50; // updated

    // Convenzioni interne
    typedef void(__thiscall* tXML_Init)(void* pThis, int a2);
    typedef int(__thiscall* tXML_Load)(void* pThis, int pFileData);
    typedef char(__thiscall* tXML_Parse)(void* pBufferAsThis, void* pXmlDoc);
    typedef void(__thiscall* tXML_Clean)(void* pThis);
    typedef void(__thiscall* tNotify)(void* pThis, const char* pStringSrc);

    typedef char(__thiscall* tPainterEntry)(void* pThis, int pFileData);
    static tPainterEntry g_origPainterEntry = nullptr;

    // --- IL DETOUR IN __fastcall ---
    // ECX = pXmlDoc (this), EDX = dummy (per fastcall), Stack = pFileData
    static char __fastcall HookedPainterConfig(void* pXmlDoc, void* edx_dummy, int pFileData)
    {
        static int s_callCount = 0;
        ++s_callCount;
        HookCrashers::Util::Logger::Instance().Get()->info("[HookHit] PainterConfig ENTER call={} pXmlDoc={} pFileData={}", s_callCount, to_hex(pXmlDoc), to_hex((void*)pFileData));
        HookCrashers::Util::Logger::Instance().Get()->flush();
        HookCrashers::Util::Logger::Instance().Get()->debug("[PainterHook] >>> ENTER - pXmlDoc: " + to_hex(pXmlDoc) + " pFileData: " + to_hex((void*)pFileData));
        uintptr_t base = (uintptr_t)GetModuleHandle(NULL);
        uint8_t* doc = (uint8_t*)pXmlDoc;

        auto XML_Init = (tXML_Init)(base + RVA_XML_INIT);
        auto XML_Load = (tXML_Load)(base + RVA_XML_LOAD);
        auto XML_Parse = (tXML_Parse)(base + RVA_XML_PARSE);
        auto XML_Clean = (tXML_Clean)(base + RVA_XML_CLEAN);
        auto Notify = (tNotify)(base + RVA_NOTIFY);
        void* globalStr = (void*)(base + RVA_GLOBAL_VAR);

        doc[84] = 1;
        HookCrashers::Util::Logger::Instance().Get()->debug("[PainterHook] doc[84] = 1");

        if (pFileData)
        {
            int N = CharacterConfig::Instance().GetAddonCount();
            size_t bufSize = 0x168 + (N > 10 ? (N - 10) * 32 : 0) + 64;
            HookCrashers::Util::Logger::Instance().Get()->debug("[PainterHook] Allocating localBuf on stack, size: " + std::to_string(bufSize) + " N=" + std::to_string(N));

            uint8_t* localBuf = (uint8_t*)_alloca(bufSize);
            memset(localBuf, 0, bufSize);
            HookCrashers::Util::Logger::Instance().Get()->debug("[PainterHook] localBuf at: " + to_hex(localBuf));

            HookCrashers::Util::Logger::Instance().Get()->debug("[PainterHook] Calling XML_Init...");
            XML_Init(localBuf, 0);
            HookCrashers::Util::Logger::Instance().Get()->debug("[PainterHook] XML_Init OK");

            HookCrashers::Util::Logger::Instance().Get()->debug("[PainterHook] Calling XML_Load...");
            int loadResult = XML_Load(localBuf, pFileData);
            HookCrashers::Util::Logger::Instance().Get()->debug("[PainterHook] XML_Load Result: " + std::to_string(loadResult));

            HookCrashers::Util::Logger::Instance().Get()->debug("[PainterHook] Calling XML_Parse (pXmlDoc as this, localBuf as arg)...");
            char parseResult = XML_Parse(pXmlDoc, localBuf);
            HookCrashers::Util::Logger::Instance().Get()->debug("[PainterHook] XML_Parse Result: " + std::to_string((int)parseResult));

            if (loadResult == 0 && parseResult)
            {
                HookCrashers::Util::Logger::Instance().Get()->debug("[PainterHook] Success Path. Calling XML_Clean...");
                XML_Clean(localBuf);
                HookCrashers::Util::Logger::Instance().Get()->debug("[PainterHook] XML_Clean OK");

                doc[84] = 0;
                HookCrashers::Util::Logger::Instance().Get()->debug("[PainterHook] doc[84] = 0. Calling Notify...");
                //Notify(pXmlDoc, (const char*)globalStr);
                HookCrashers::Util::Logger::Instance().Get()->debug("[PainterHook] Notify OK");

                HookCrashers::Util::Logger::Instance().Get()->debug("[PainterHook] <<< EXIT Success (Return 1)");
                HookCrashers::Util::Logger::Instance().Get()->info("[HookHit] PainterConfig LEAVE call={} result=1", s_callCount);
                HookCrashers::Util::Logger::Instance().Get()->flush();
                return 1;            }

            HookCrashers::Util::Logger::Instance().Get()->debug("[PainterHook] Parse/Load failed. Calling XML_Clean...");
            XML_Clean(localBuf);
            HookCrashers::Util::Logger::Instance().Get()->debug("[PainterHook] XML_Clean OK");
        }

        HookCrashers::Util::Logger::Instance().Get()->debug("[PainterHook] <<< EXIT Failure (Return 0)");
        HookCrashers::Util::Logger::Instance().Get()->info("[HookHit] PainterConfig LEAVE call={} result=0", s_callCount);
        HookCrashers::Util::Logger::Instance().Get()->flush();
        return 0;    }

    bool SetupPainterConfigHook() {
        uintptr_t base = (uintptr_t)GetModuleHandle(NULL);
        g_origPainterEntry = (tPainterEntry)(base + RVA_PAINTER_ENTRY);

        DetourTransactionBegin();
        DetourUpdateThread(GetCurrentThread());

        // Attacchiamo la nostra __fastcall alla __thiscall originale
        DetourAttach(&(PVOID&)g_origPainterEntry, HookedPainterConfig);

        if (DetourTransactionCommit() != NO_ERROR) {
            HookCrashers::Util::Logger::Instance().Get()->error("[PainterHook] DetourTransactionCommit FAILED!");
            return false;
        }

        HookCrashers::Util::Logger::Instance().Get()->info("[PainterHook] Hooked sub_83C080 via __fastcall (Registers safe).");
        return true;
    }
}
