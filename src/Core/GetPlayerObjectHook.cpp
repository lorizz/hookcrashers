#include "GetPlayerObjectHook.h"
#include "../Util/Logger.h"

namespace HookCrashers {
    namespace Core {
        static Util::Logger& L = Util::Logger::Instance();

        using OriginalGetPlayerObjectFunc_t = void* (__thiscall*)(void* thisPtr, uint16_t playerId);
        static OriginalGetPlayerObjectFunc_t g_originalFunction = nullptr;

        uintptr_t* g_pGameManagerPtr = nullptr;

        constexpr uintptr_t GET_PLAYER_OBJECT_OFFSET = 0xEE1B0;
        constexpr uintptr_t GAME_MANAGER_POINTER_OFFSET = 0x38E19C;

        void* __fastcall  DetouredGetPlayerObject(void* thisPtr, void* /*edx*/, uint16_t playerId) {
            if (thisPtr != nullptr) {
                try {
                    return g_originalFunction(thisPtr, playerId);
                }
                catch (const std::exception& e) {
                }
                catch (...) {
                }
            }
        }

        bool SetupGetPlayerObjectHook(uintptr_t moduleBase) {
            uintptr_t targetAddress = moduleBase + GET_PLAYER_OBJECT_OFFSET;
            g_originalFunction = reinterpret_cast<OriginalGetPlayerObjectFunc_t>(targetAddress);

            if (!g_originalFunction) {
                return false;
            }

            g_pGameManagerPtr = reinterpret_cast<uintptr_t*>(moduleBase + GAME_MANAGER_POINTER_OFFSET);

            DetourTransactionBegin();
            DetourUpdateThread(GetCurrentThread());
            LONG error = DetourAttach(&(PVOID&)g_originalFunction, DetouredGetPlayerObject);
            if (error != NO_ERROR) {
                DetourTransactionAbort();
                g_originalFunction = nullptr;
                return false;
            }
            error = DetourTransactionCommit();
            if (error != NO_ERROR) {
                g_originalFunction = nullptr;
                return false;
            }
            return true;
        }

        void* GetPlayerObject(uint16_t playerId) {
            if (!g_originalFunction) {
                return nullptr;
            }

            // Controlla se il puntatore è stato inizializzato correttamente durante il setup.
            if (!g_pGameManagerPtr) {
                return nullptr;
            }

            // Dereferenzia il puntatore memorizzato per ottenere il 'this' pointer corretto.
            void* correctThisPtr = reinterpret_cast<void*>(*g_pGameManagerPtr);

            if (!correctThisPtr) {
                return nullptr;
            }

            // Chiama la funzione originale con il contesto giusto.
            try {
                return g_originalFunction(correctThisPtr, playerId);
            }
            catch (...) {
                return nullptr;
            }
        }

        // ---- Implementazione metodi dal vtable ----

        char GetPlayerState(void* playerObj) {
            if (!playerObj) return 0;
            using Fn = char(__thiscall*)(void*);
            Fn func = *reinterpret_cast<Fn*>(
                *reinterpret_cast<uintptr_t*>(playerObj) + 0x5C
                );
            return func(playerObj);
        }

        char GetPlayerActiveState(void* playerObj) {
            if (!playerObj) return 0;
            using Fn = char(__thiscall*)(void*);
            Fn func = *reinterpret_cast<Fn*>(
                *reinterpret_cast<uintptr_t*>(playerObj) + 0x20
                );
            return func(playerObj);
        }

        uint64_t GetPlayerPosition(void* playerObj) {
            if (!playerObj) return 0;
            using Fn = uint64_t(__thiscall*)(void*);
            Fn func = *reinterpret_cast<Fn*>(
                *reinterpret_cast<uintptr_t*>(playerObj) + 0x54
                );
            return func(playerObj);
        }

        int GetPlayerSelectedCharacterType(void* playerObj) {
            if (!playerObj) return -1;
            using GetCharInfoFn = void* (__thiscall*)(void*);
            GetCharInfoFn getCharInfo = *reinterpret_cast<GetCharInfoFn*>(*reinterpret_cast<uintptr_t*>(playerObj) + 0x68);
            void* charInfo = getCharInfo(playerObj);
            if (!charInfo || *reinterpret_cast<char*>(reinterpret_cast<uintptr_t>(charInfo) + 0x860) == '\0') {
                return -1;
            }
            return *reinterpret_cast<int*>(reinterpret_cast<uintptr_t>(charInfo) + 0x85C) + 1;
        }

        bool IsOnlineMode() {
            // Usa il puntatore al GameManager che hai già inizializzato
            if (!g_pGameManagerPtr || *g_pGameManagerPtr == 0) {
                return false;
            }

            // Traduciamo letteralmente la logica di FUN_00ae20d0
            void* gameManager = reinterpret_cast<void*>(*g_pGameManagerPtr);

            // Controllo 1: if (*(int *)(DAT_00dee19c + 0x18) != 0)
            if (*reinterpret_cast<uintptr_t*>(reinterpret_cast<uintptr_t>(gameManager) + 0x18) == 0) {
                return false;
            }

            // Controllo 2: pObject = *(int **)(DAT_00dee19c + 0xc)
            void* pObject = *reinterpret_cast<void**>(reinterpret_cast<uintptr_t>(gameManager) + 0xc);
            if (!pObject) {
                return false;
            }

            // Chiamata alla vtable+0x14 usando la convenzione __thiscall
            using Fn = int(__thiscall*)(void*);
            Fn func = *reinterpret_cast<Fn*>(
                *reinterpret_cast<uintptr_t*>(pObject) + 0x14
                );

            // Il parametro `param_1` di Ghidra è irrilevante qui,
            // perché non viene mai usato prima della chiamata.
            // La chiamata alla funzione non sembra passare argomenti significativi
            // oltre al `this` pointer in ECX.
            int result = func(pObject);

            // Controllo finale: if (in_EAX == 4) return 1;
            return (result == 4);
        }
    }
}
