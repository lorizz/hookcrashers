#include "PromptApi.h"

#include "UpdatePromptMenu.h"

#if defined(_M_IX86)
#pragma comment(linker, "/EXPORT:HookCrashers_CreatePrompt=_HookCrashers_CreatePrompt@16")
#endif

namespace HookCrashers::UI {

bool CreatePrompt(const char* message,
                  PromptCallback onConfirm,
                  PromptCallback onDecline,
                  void* userData) {
    if (!message || !*message) {
        return false;
    }

    return UpdatePromptMenu::CreatePrompt(
        message,
        [onConfirm, userData]() {
            if (onConfirm) {
                onConfirm(userData);
            }
        },
        [onDecline, userData]() {
            if (onDecline) {
                onDecline(userData);
            }
        });
}

} // namespace HookCrashers::UI

extern "C" __declspec(dllexport)
bool __stdcall HookCrashers_CreatePrompt(const char* message,
                                         HookCrashers::UI::PromptCallback onConfirm,
                                         HookCrashers::UI::PromptCallback onDecline,
                                         void* userData) {
    return HookCrashers::UI::CreatePrompt(message, onConfirm, onDecline, userData);
}
