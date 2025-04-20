#pragma once
#include <cstdint>

class BaseHook {
public:
    BaseHook(uint16_t id);
    virtual ~BaseHook() = default;

    uint16_t GetId() const { return m_id; }

    virtual bool Execute(void* thisPtr, int param_1, uint32_t param_2,
        int param_3, int* param_4, uint32_t* param_5,
        uint32_t param_6) = 0;

    bool CanHandle(uint16_t functionId) const { return functionId == m_id; }

    virtual void Initialize(uintptr_t moduleBase);

protected:
    uint16_t m_id;

    uintptr_t m_moduleBase;

    uintptr_t GetFunctionAddress(uintptr_t relativeOffset) const;
};