#include "BaseHook.h"
#include "../Util/Logger.h"

namespace HookCrashers {
    namespace Core {

        BaseHook::BaseHook(uint16_t id) : m_id(id), m_moduleBase(0) {}

        void BaseHook::Initialize(uintptr_t moduleBase) {
            Util::Logger& l = Util::Logger::Instance();
            if (moduleBase == 0) {
                //l.Get()->error("BaseHook::Initialize called with null module base!");
                return;
            }
            m_moduleBase = moduleBase;
            //l.Get()->info("Initialized BaseHook for ID {} with module base 0x{:X}", m_id, m_moduleBase);
        }

        uintptr_t BaseHook::GetFunctionAddress(uintptr_t relativeOffset) const {
            if (!IsInitialized()) {
                //Util::Logger::Instance().Get()->error("BaseHook::GetFunctionAddress: Hook not initialized with module base!");
                return 0;
            }
            return m_moduleBase + relativeOffset;
        }

    }
}