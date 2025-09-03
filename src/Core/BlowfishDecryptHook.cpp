#define NOMINMAX
#include "BlowfishDecryptHook.h"
#include "../Util/Logger.h"
#include <detours.h>
#include <algorithm>
#include <iomanip>
#include <sstream>
#include <vector>

namespace HookCrashers {
    namespace Core {

        static Util::Logger& L = Util::Logger::Instance();

        using OriginalBlowfishDecrypt_t = void(__thiscall*)(void* thisPtr, uint32_t* block_part1, uint32_t* block_part2);
        static OriginalBlowfishDecrypt_t g_originalFunction = nullptr;

        constexpr uintptr_t BLOWFISH_DECRYPT_OFFSET = 0xdacd0;
        static void* g_capturedBlowfishContext = nullptr;

        static std::vector<uint8_t> g_capturedSaveData;
        static bool g_isCapturing = false;

        void LogBufferSection(const std::string& sectionName, const std::vector<uint8_t>& data, size_t offset, size_t length) {
            if (offset >= data.size()) {
                L.Get()->trace("Sezione '{}': Offset di partenza fuori dai limiti.", sectionName);
                return;
            }

            std::stringstream ss;
            ss << std::hex << std::setfill('0');

            size_t effectiveLength = std::min(length, data.size() - offset);

            for (size_t i = 0; i < effectiveLength; ++i) {
                ss << std::setw(2) << static_cast<int>(data[offset + i]) << " ";
                if ((i + 1) % 16 == 0 && (i + 1) < effectiveLength) {
                    ss << "\n                      ";
                }
            }

            L.Get()->trace("Sezione '{}' ({} bytes a offset {}):\n                      {}",
                sectionName, effectiveLength, offset, ss.str());
        }

        void StartSaveDataCapture() {
            L.Get()->info("Inizio cattura dati di salvataggio decriptati...");
            g_capturedSaveData.clear();
            g_isCapturing = true;
        }

        void StopSaveDataCapture() {
            L.Get()->info("Cattura dati di salvataggio terminata. Totale byte catturati: {}", g_capturedSaveData.size());
            g_isCapturing = false;
        }

        void __fastcall DetouredBlowfishDecrypt(void* thisPtr, void* /* edx_dummy */, uint32_t* block_part1, uint32_t* block_part2) {
            if (g_capturedBlowfishContext == nullptr) {
                g_capturedBlowfishContext = thisPtr;
                L.Get()->info("Contesto Blowfish catturato all'indirizzo: 0x{:X}", (uintptr_t)thisPtr);
            }

            g_originalFunction(thisPtr, block_part1, block_part2);

            if (g_isCapturing) {
                const uint8_t* p1_bytes = reinterpret_cast<const uint8_t*>(block_part1);
                const uint8_t* p2_bytes = reinterpret_cast<const uint8_t*>(block_part2);

                g_capturedSaveData.insert(g_capturedSaveData.end(), p1_bytes, p1_bytes + 4);
                g_capturedSaveData.insert(g_capturedSaveData.end(), p2_bytes, p2_bytes + 4);
            }
        }

        bool SetupBlowfishDecryptHook(uintptr_t moduleBase) {
            if (g_originalFunction != nullptr) {
                L.Get()->warn("Hook di BlowfishDecrypt già installato.");
                return true;
            }

            uintptr_t targetAddress = moduleBase + BLOWFISH_DECRYPT_OFFSET;
            g_originalFunction = reinterpret_cast<OriginalBlowfishDecrypt_t>(targetAddress);

            L.Get()->info("Tentativo di hook per BlowfishDecrypt a 0x{:X}", targetAddress);

            DetourTransactionBegin();
            DetourUpdateThread(GetCurrentThread());

            LONG error = DetourAttach(&(PVOID&)g_originalFunction, DetouredBlowfishDecrypt);
            if (error != NO_ERROR) {
                L.Get()->error("DetourAttach per BlowfishDecrypt fallito con errore: {}", error);
                DetourTransactionAbort();
                g_originalFunction = nullptr;
                return false;
            }

            error = DetourTransactionCommit();
            if (error != NO_ERROR) {
                L.Get()->error("DetourTransactionCommit per BlowfishDecrypt fallito con errore: {}", error);
                g_originalFunction = nullptr;
                return false;
            }

            L.Get()->info("Hook per BlowfishDecrypt installato con successo.");
            StartSaveDataCapture(); // Avvia la cattura automaticamente all'installazione dell'hook
            return true;
        }

        void TeardownBlowfishDecryptHook() {
            if (g_originalFunction == nullptr) return;

            DetourTransactionBegin();
            DetourUpdateThread(GetCurrentThread());
            DetourDetach(&(PVOID&)g_originalFunction, DetouredBlowfishDecrypt);
            DetourTransactionCommit();

            g_originalFunction = nullptr;
            g_capturedBlowfishContext = nullptr;
            g_isCapturing = false;
            L.Get()->info("Hook per BlowfishDecrypt rimosso.");
        }

        void AnalyzeCapturedData() {
            if (g_capturedSaveData.empty()) {
                L.Get()->warn("Nessun dato di salvataggio catturato da analizzare.");
                return;
            }

            L.Get()->info("Analisi dei {} byte di salvataggio catturati:", g_capturedSaveData.size());

            constexpr size_t GLOBAL_UNLOCKS_SIZE = 64;
            constexpr size_t CHARACTER_DATA_SIZE = 48;
            constexpr size_t NUM_CHARACTERS_TO_LOG = 32;

            LogBufferSection("Global Unlocks", g_capturedSaveData, 0, GLOBAL_UNLOCKS_SIZE);

            size_t characterDataStartOffset = GLOBAL_UNLOCKS_SIZE;
            for (size_t i = 0; i < NUM_CHARACTERS_TO_LOG; ++i) {
                size_t currentCharOffset = characterDataStartOffset + (i * CHARACTER_DATA_SIZE);
                if (currentCharOffset >= g_capturedSaveData.size()) {
                    L.Get()->trace("Fine dei dati dei personaggi dopo {} slot.", i);
                    break;
                }
                std::string sectionName = "Character Slot " + std::to_string(i);
                LogBufferSection(sectionName, g_capturedSaveData, currentCharOffset, CHARACTER_DATA_SIZE);
            }

            size_t restOfDataOffset = characterDataStartOffset + (NUM_CHARACTERS_TO_LOG * CHARACTER_DATA_SIZE);
            if (restOfDataOffset < g_capturedSaveData.size()) {
                size_t restOfDataSize = g_capturedSaveData.size() - restOfDataOffset;
                LogBufferSection("Dati Rimanenti", g_capturedSaveData, restOfDataOffset, restOfDataSize);
            }

            L.Get()->info("Analisi dei dati catturati completata.");
        }

        const std::vector<uint8_t>& GetCapturedSaveData() {
            return g_capturedSaveData;
        }
    }
}