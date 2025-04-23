#pragma once

#include "../stdafx.h"

namespace HookCrashers {
	namespace Core {
		class BaseHook {
		public:
			BaseHook(uint16_t id);
			virtual ~BaseHook() = default;

			uint16_t GetId() const { return m_id; }

			virtual bool Execute() { return false; }
			virtual void Initialize(uintptr_t moduleBase);

			bool IsInitialized() const { return m_moduleBase != 0; }

		protected:
			uint16_t m_id;
			uintptr_t m_moduleBase;

			uintptr_t GetFunctionAddress(uintptr_t relativeOffset) const;
		};
	}
}