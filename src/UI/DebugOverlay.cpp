#include "DebugOverlay.h"

#include "../Core/Logger.h"

#include <imgui.h>

namespace HookCrashers::UI::DebugOverlay {
namespace {

bool g_Initialized = false;

} // namespace

void Initialize() {
    g_Initialized = true;
    Core::Logger::Info("ImGui debug overlay scaffold initialized");
}

void Draw() {
    if (!g_Initialized) {
        return;
    }

    ImGui::Begin("HookCrashers v5");
    ImGui::TextUnformatted("HookCrashers v5 debug overlay");
    ImGui::Text("Log: %s", Core::Logger::LogPath().c_str());
    ImGui::End();
}

bool IsInitialized() {
    return g_Initialized;
}

} // namespace HookCrashers::UI::DebugOverlay
