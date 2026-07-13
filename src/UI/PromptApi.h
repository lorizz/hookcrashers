#pragma once

namespace HookCrashers::UI {

using PromptCallback = void(__stdcall*)(void* userData);

bool CreatePrompt(const char* message,
                  PromptCallback onConfirm,
                  PromptCallback onDecline,
                  void* userData = nullptr);

} // namespace HookCrashers::UI

extern "C" __declspec(dllexport)
bool __stdcall HookCrashers_CreatePrompt(const char* message,
                                         HookCrashers::UI::PromptCallback onConfirm,
                                         HookCrashers::UI::PromptCallback onDecline,
                                         void* userData);
