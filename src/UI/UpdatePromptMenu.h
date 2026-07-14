#pragma once

#include <functional>
#include <string>

namespace HookCrashers::UI::UpdatePromptMenu {

bool Install();
void Remove();
void SetStartupUpdateAvailable(bool available);
bool CreatePrompt(const std::string& message,
                  std::function<void()> onConfirm,
                  std::function<void()> onDecline);
void SetVersionTextTemplate(const std::string& text, const std::string& modVersion);

} // namespace HookCrashers::UI::UpdatePromptMenu
