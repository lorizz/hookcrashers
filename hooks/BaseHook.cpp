#include "../stdafx.h"
#include "BaseHook.h"
#include "../logger.h"

BaseHook::BaseHook(uint16_t id) : m_id(id), m_moduleBase(0) {}

void BaseHook::Initialize(uintptr_t moduleBase) {
    Logger& l = Logger::Instance();
    m_moduleBase = moduleBase;
    l.Get()->info("Initialized BaseHook for ID 0x{:X} with module base 0x{:X}", m_id, m_moduleBase);
}

uintptr_t BaseHook::GetFunctionAddress(uintptr_t relativeOffset) const {
    if (m_moduleBase == 0) {
        Logger& l = Logger::Instance();
        l.Get()->error("Attempting to get function address but module base is not initialized!");
        return 0;
    }
    return m_moduleBase + relativeOffset;
}