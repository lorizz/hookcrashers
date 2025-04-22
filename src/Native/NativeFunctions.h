#pragma once

/*#include "NativeCaller.h"
#include <string>

namespace HookCrashers {
	namespace Native {
		struct GameFunctions {
			static NativeInfo<void*> GetFirstArgument;
			static NativeInfo<void*> GetNextArgument;
			static NativeInfo<void*> SetStorageOffset;
			static NativeInfo<void*> ReadStorageData;
			static NativeInfo<void*> WriteStorageData;
		};

		bool LoadNatives(uintptr_t moduleBase);

		const char* GetGameStringById(uint16_t stringId);

		/*inline int* Call_GetFirstArgument(void* swfArgsContainer) {
			return CallNative<int*>(GameFunctions::GetFirstArgument);
		}

		inline int* Call_GetNextArgument() {
			// Similar assumptions to GetFirstArgument based on Ghidra usage.
			// `uVar2 = GetNextArgument();`
			return CallNative<int*>(GameFunctions::GetNextArgument); // FastCall, 0 args? Check Convention!
		}

		inline void Call_SetStorageOffset(void* thisPtr, int storageIndex, int offset) {
			// Ghidra: SetStorageOffset(local_78, iVar8, uVar9); -> thisPtr, int, int
			CallNative<void>(GameFunctions::SetStorageOffset, thisPtr, storageIndex, offset); // ThisCall
		}

		inline int* Call_ReadStorageData(void* thisPtr, int storageIndex, void* outValuePtr) {
			// Ghidra: piVar10 = (int *)Native_ReadStorage(local_78,iVar8,&local_6d); -> thisPtr, int, out*
			// Returns a pointer to an int status code? (0=ok, 1=fail?)
			return CallNative<int*>(GameFunctions::ReadStorageData, thisPtr, storageIndex, outValuePtr); // ThisCall
		}

		inline int* Call_WriteStorageData(void* thisPtr, int storageIndex, void* valuePtr) {
			// Ghidra: piVar10 = (int *)Native_WriteStorage(local_78,iVar8,local_6c); -> thisPtr, int, value*
			// Returns a pointer to an int status code?
			return CallNative<int*>(GameFunctions::WriteStorageData, thisPtr, storageIndex, valuePtr); // ThisCall
		}+/
	}
}*/