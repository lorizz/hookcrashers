#include "SaveSlotMenu.h"

#include "MenuApi.h"
#include "UpdatePromptMenu.h"
#include "../Core/Logger.h"
#include "../Save/SaveSlots.h"

#include <windows.h>

#include <cstdint>
#include <cstdio>
#include <ctime>
#include <string>
#include <vector>

namespace HookCrashers::UI::SaveSlotMenu {
namespace {

namespace Slots = Save::Slots;

constexpr const char* kMenu = "hc.saveslots";
constexpr const char* kModsMenu = "hc.saveslots.mods";
constexpr int kModLines = 12;
constexpr std::uint32_t kGreen = 0x5AD25AFFu;
constexpr std::uint32_t kOrange = 0xFFA030FFu;
constexpr std::uint32_t kRed = 0xFF4A4AFFu;
constexpr std::uint32_t kBlue = 0x7FC8FFFFu;

struct Text {
    const char* title; const char* slot; const char* empty; const char* lastPlayed; const char* emptySlot;
    const char* mods; const char* ok; const char* outdated; const char* missing; const char* added;
    const char* play; const char* resume; const char* newGame; const char* modInfo; const char* importVanilla;
    const char* remove; const char* confirmDelete; const char* failed; const char* noMods;
};

constexpr Text kEnglish {
    "Save slots", "Slot", "empty", "Last played", "Empty slot",
    "Mods", "ok", "changed", "missing", "new",
    "Play", "Continue", "New game", "Mod Info", "Import vanilla save",
    "Delete slot", "Move this slot to the trash?", "Save slot error", "No mods recorded yet",
};
constexpr Text kItalian {
    "Salvataggi", "Slot", "vuoto", "Ultimo uso", "Slot vuoto",
    "Mod", "ok", "cambiate", "mancanti", "nuove",
    "Gioca", "Continua", "Nuova partita", "Mod Info", "Importa save vanilla",
    "Elimina slot", "Spostare questo slot nel cestino?", "Errore slot", "Nessuna mod registrata",
};

const Text* g_text = &kEnglish;
int g_selected = 1;

void DetectLanguage() {
    HMODULE steam = GetModuleHandleA("steam_api.dll");
    if (!steam) return;
    auto apps = reinterpret_cast<void*(__cdecl*)()>(GetProcAddress(steam, "SteamAPI_SteamApps_v008"));
    auto language = reinterpret_cast<const char*(__cdecl*)(void*)>(
        GetProcAddress(steam, "SteamAPI_ISteamApps_GetCurrentGameLanguage"));
    void* object = apps ? apps() : nullptr;
    const char* name = object && language ? language(object) : nullptr;
    g_text = name && _stricmp(name, "italian") == 0 ? &kItalian : &kEnglish;
}

std::string Date(std::int64_t seconds) {
    if (seconds <= 0) return "-";
    const std::time_t t = static_cast<std::time_t>(seconds);
    std::tm local {};
    localtime_s(&local, &t);
    char buffer[32];
    std::strftime(buffer, sizeof(buffer), "%d/%m/%Y %H:%M", &local);
    return buffer;
}

const Slots::SlotInfo* Selected(const std::vector<Slots::SlotInfo>& list) {
    for (const auto& slot : list) if (slot.index == g_selected) return &slot;
    return nullptr;
}

void Refresh() {
    if (!Slots::IsReady()) return;
    const auto list = Slots::List();
    std::vector<std::string> options;
    for (const auto& slot : list) options.push_back(std::to_string(slot.index) + (slot.active ? " *" : ""));
    Menus::Api::SetItemOptions(kMenu, "slot", options);
    Menus::Api::SetItemValue(kMenu, "slot", g_selected - 1);

    const Slots::SlotInfo* slot = Selected(list);
    if (!slot) return;
    if (slot->used) {
        Menus::Api::SetItemText(kMenu, "name", slot->meta.name);
        Menus::Api::SetItemText(kMenu, "info1", std::string(g_text->lastPlayed) + ": " + Date(slot->meta.played) +
                                                    (slot->meta.hcVersion.empty() ? "" : "  HC " + slot->meta.hcVersion));
        const auto& sum = slot->summary;
        char line[160];
        std::snprintf(line, sizeof(line), "%s: %d %s  %d %s  %d %s  %d %s", g_text->mods,
                      sum.match, g_text->ok, sum.outdated, g_text->outdated, sum.missing, g_text->missing,
                      sum.added, g_text->added);
        Menus::Api::SetItemText(kMenu, "info2", line);
        Menus::Api::SetItemColor(kMenu, "info2", sum.missing ? kRed : sum.outdated ? kOrange : sum.added ? kBlue : kGreen);
    } else {
        Menus::Api::SetItemText(kMenu, "name", g_text->emptySlot);
        Menus::Api::SetItemText(kMenu, "info1", " ");
        Menus::Api::SetItemText(kMenu, "info2", " ");
        Menus::Api::SetItemColor(kMenu, "info2", 0);
    }
    Menus::Api::SetItemText(kMenu, "play", slot->active ? g_text->resume : slot->used ? g_text->play : g_text->newGame);
    Menus::Api::SetItemEnabled(kMenu, "modinfo", slot->used);
    Menus::Api::SetItemEnabled(kMenu, "import", !slot->used && Slots::VanillaSaveAvailable());
    Menus::Api::SetItemEnabled(kMenu, "delete", slot->used && !slot->active);
}

void ShowError(const std::string& error) {
    Core::Logger::Error("SaveSlotMenu: %s", error.c_str());
    UpdatePromptMenu::CreatePrompt(std::string(g_text->failed) + ": " + error, {}, {});
}

void OnPlay(int) {
    std::string error;
    if (!Slots::Activate(g_selected, error)) {
        ShowError(error);
        return;
    }
    Menus::Api::Close();
}

void OnImport(int) {
    std::string error;
    if (!Slots::ImportVanilla(g_selected, error)) {
        ShowError(error);
        return;
    }
    Menus::Api::Close();
}

void OnDelete(int) {
    const int index = g_selected;
    UpdatePromptMenu::CreatePrompt(g_text->confirmDelete, [index]() {
        std::string error;
        if (!Slots::Delete(index, error)) ShowError(error);
    }, {});
}

void OnModInfo(int) {
    const auto list = Slots::List();
    const Slots::SlotInfo* slot = Selected(list);
    if (!slot) return;
    const int count = static_cast<int>(slot->mods.size());
    for (int line = 0; line < kModLines; ++line) {
        std::string text = " ";
        std::uint32_t color = 0;
        if (count == 0 && line == 0) {
            text = g_text->noMods;
        } else if (line == kModLines - 1 && count > kModLines) {
            text = "+" + std::to_string(count - (kModLines - 1)) + " ...";
        } else if (line < count) {
            const auto& mod = slot->mods[static_cast<std::size_t>(line)];
            switch (mod.state) {
            case Slots::ModState::Match: text = mod.name + " " + mod.savedVersion; color = kGreen; break;
            case Slots::ModState::Outdated: text = mod.name + " " + mod.savedVersion + " -> " + mod.currentVersion; color = kOrange; break;
            case Slots::ModState::Missing: text = mod.name + " " + mod.savedVersion + " (" + g_text->missing + ")"; color = kRed; break;
            case Slots::ModState::Added: text = mod.name + " " + mod.currentVersion + " (" + g_text->added + ")"; color = kBlue; break;
            }
        }
        Menus::Api::SetItemText(kModsMenu, "m" + std::to_string(line), text);
        Menus::Api::SetItemColor(kModsMenu, "m" + std::to_string(line), color);
    }
    Menus::Api::Open(kModsMenu);
}

} // namespace

bool Install() {
    if (!Slots::IsInstalled()) return false;
    DetectLanguage();
    bool ok = Menus::Api::CreateMenu(kMenu, g_text->title);
    std::vector<std::string> options;
    for (int i = 1; i <= Slots::kMaxSlots; ++i) options.push_back(std::to_string(i));
    ok = ok && Menus::Api::AddChoice(kMenu, "slot", g_text->slot, options, 0, [](int value) {
        g_selected = value + 1;
        Refresh();
    });
    ok = ok && Menus::Api::AddLabel(kMenu, "name", " ") && Menus::Api::AddLabel(kMenu, "info1", " ") &&
         Menus::Api::AddLabel(kMenu, "info2", " ");
    // The slot screen is a choice: no Back button, the Back key does nothing.
    ok = ok && Menus::Api::SetBackButton(kMenu, false);
    ok = ok && Menus::Api::AddButton(kMenu, "play", g_text->play, OnPlay);
    ok = ok && Menus::Api::AddButton(kMenu, "modinfo", g_text->modInfo, OnModInfo);
    ok = ok && Menus::Api::AddButton(kMenu, "import", g_text->importVanilla, OnImport);
    ok = ok && Menus::Api::AddButton(kMenu, "delete", g_text->remove, OnDelete);
    ok = ok && Menus::Api::CreateMenu(kModsMenu, g_text->modInfo);
    for (int line = 0; line < kModLines && ok; ++line) {
        ok = Menus::Api::AddLabel(kModsMenu, "m" + std::to_string(line), " ");
    }
    if (!ok) {
        Core::Logger::Error("SaveSlotMenu: menus could not be registered");
        return false;
    }
    Slots::SetChangedCallback([]() {
        g_selected = Slots::ActiveSlot();
        Refresh();
    });
    const auto result = Menus::Api::Open(kMenu);
    Core::Logger::Info("SaveSlotMenu: installed, open=%s", Menus::Api::OpenResultName(result));
    return true;
}

} // namespace HookCrashers::UI::SaveSlotMenu
