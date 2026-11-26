#include "SaveSlotMenu.h"

#include "MenuApi.h"
#include "UpdatePromptMenu.h"
#include "../Core/Logger.h"
#include "../Core/Memory.h"
#include "../Save/SaveSlots.h"

#include <windows.h>

#include <array>
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
// The native layout spreads the items over the page; more lines would push the
// first one under the title.
constexpr int kModLines = 8;
constexpr std::uint32_t kGreen = 0x5AD25AFFu;
constexpr std::uint32_t kOrange = 0xFFA030FFu;
constexpr std::uint32_t kRed = 0xFF4A4AFFu;
constexpr std::uint32_t kBlue = 0x7FC8FFFFu;
constexpr std::uintptr_t kProfileManagerPointerRva = 0x391294; // first field: game language index

enum Text : int {
    kTitle, kSlot, kLastPlayed, kEmptySlot, kMods, kOk, kChanged, kMissing, kNew, kPlay, kContinue, kNewGame,
    kModInfo, kImportVanilla, kDelete, kConfirmDelete, kError, kNoMods, kLocked, kNeeded, kTextCount
};

// Same language order as the game and the update prompt: English, German,
// French, Spanish, Italian, Japanese, Korean, Traditional Chinese, Portuguese,
// Simplified Chinese, Russian.
const char* const kTexts[11][kTextCount] = {
    // English
    {u8"Save slots", u8"Slot", u8"Last played", u8"Empty slot", u8"Mods", u8"ok", u8"changed", u8"missing", u8"new", u8"Play", u8"Continue", u8"New game", u8"Mod Info", u8"Import vanilla save", u8"Delete slot", u8"Move this slot to the trash?", u8"Save slot error", u8"No mods recorded yet", u8"Locked: save mods missing or changed", u8"needed"},
    // German
    {u8"Spielst\u00E4nde", u8"Platz", u8"Zuletzt gespielt", u8"Leerer Platz", u8"Mods", u8"ok", u8"ge\u00E4ndert", u8"fehlen", u8"neu", u8"Spielen", u8"Fortsetzen", u8"Neues Spiel", u8"Mod-Info", u8"Vanilla-Spielstand importieren", u8"Platz l\u00F6schen", u8"Diesen Platz in den Papierkorb verschieben?", u8"Spielstand-Fehler", u8"Noch keine Mods gespeichert", u8"Gesperrt: Spielstand-Mods fehlen oder ge\u00E4ndert", u8"ben\u00F6tigt"},
    // French
    {u8"Sauvegardes", u8"Emplacement", u8"Derni\u00E8re partie", u8"Emplacement vide", u8"Mods", u8"ok", u8"modifi\u00E9s", u8"manquants", u8"nouveaux", u8"Jouer", u8"Continuer", u8"Nouvelle partie", u8"Infos mods", u8"Importer la sauvegarde vanilla", u8"Supprimer l'emplacement", u8"Mettre cet emplacement \u00E0 la corbeille ?", u8"Erreur de sauvegarde", u8"Aucun mod enregistr\u00E9", u8"Bloqu\u00E9 : mods de sauvegarde manquants ou modifi\u00E9s", u8"requis"},
    // Spanish
    {u8"Ranuras de guardado", u8"Ranura", u8"\u00DAltima partida", u8"Ranura vac\u00EDa", u8"Mods", u8"ok", u8"cambiados", u8"faltan", u8"nuevos", u8"Jugar", u8"Continuar", u8"Nueva partida", u8"Info de mods", u8"Importar partida vanilla", u8"Borrar ranura", u8"\u00BFMover esta ranura a la papelera?", u8"Error de guardado", u8"Ning\u00FAn mod registrado", u8"Bloqueada: faltan o cambiaron mods del guardado", u8"necesario"},
    // Italian
    {u8"Salvataggi", u8"Slot", u8"Ultima partita", u8"Slot vuoto", u8"Mod", u8"ok", u8"cambiate", u8"mancanti", u8"nuove", u8"Gioca", u8"Continua", u8"Nuova partita", u8"Info mod", u8"Importa salvataggio vanilla", u8"Elimina slot", u8"Spostare questo slot nel cestino?", u8"Errore salvataggio", u8"Nessuna mod registrata", u8"Bloccato: mod del salvataggio mancanti o cambiate", u8"necessaria"},
    // Japanese
    {u8"\u30BB\u30FC\u30D6\u30B9\u30ED\u30C3\u30C8", u8"\u30B9\u30ED\u30C3\u30C8", u8"\u6700\u7D42\u30D7\u30EC\u30A4", u8"\u7A7A\u304D\u30B9\u30ED\u30C3\u30C8", u8"MOD", u8"OK", u8"\u5909\u66F4", u8"\u4E0D\u8DB3", u8"\u65B0\u898F", u8"\u30D7\u30EC\u30A4", u8"\u3064\u3065\u3051\u308B", u8"\u30CB\u30E5\u30FC\u30B2\u30FC\u30E0", u8"MOD\u60C5\u5831", u8"\u901A\u5E38\u306E\u30BB\u30FC\u30D6\u3092\u53D6\u308A\u8FBC\u3080", u8"\u30B9\u30ED\u30C3\u30C8\u3092\u524A\u9664", u8"\u3053\u306E\u30B9\u30ED\u30C3\u30C8\u3092\u30B4\u30DF\u7BB1\u306B\u79FB\u52D5\u3057\u307E\u3059\u304B\uFF1F", u8"\u30BB\u30FC\u30D6\u30A8\u30E9\u30FC", u8"MOD\u306E\u8A18\u9332\u306A\u3057", u8"\u30ED\u30C3\u30AF\u4E2D\uFF1A\u30BB\u30FC\u30D6\u7528MOD\u304C\u4E0D\u8DB3\u30FB\u5909\u66F4", u8"\u5FC5\u8981"},
    // Korean
    {u8"\uC800\uC7A5 \uC2AC\uB86F", u8"\uC2AC\uB86F", u8"\uB9C8\uC9C0\uB9C9 \uD50C\uB808\uC774", u8"\uBE48 \uC2AC\uB86F", u8"\uBAA8\uB4DC", u8"\uC815\uC0C1", u8"\uBCC0\uACBD\uB428", u8"\uC5C6\uC74C", u8"\uC0C8\uB85C\uC6C0", u8"\uD50C\uB808\uC774", u8"\uACC4\uC18D\uD558\uAE30", u8"\uC0C8 \uAC8C\uC784", u8"\uBAA8\uB4DC \uC815\uBCF4", u8"\uAE30\uBCF8 \uAC8C\uC784 \uC800\uC7A5 \uAC00\uC838\uC624\uAE30", u8"\uC2AC\uB86F \uC0AD\uC81C", u8"\uC774 \uC2AC\uB86F\uC744 \uD734\uC9C0\uD1B5\uC73C\uB85C \uC62E\uAE38\uAE4C\uC694?", u8"\uC800\uC7A5 \uC624\uB958", u8"\uAE30\uB85D\uB41C \uBAA8\uB4DC \uC5C6\uC74C", u8"\uC7A0\uAE40: \uC800\uC7A5 \uAD00\uB828 \uBAA8\uB4DC\uAC00 \uC5C6\uAC70\uB098 \uBCC0\uACBD\uB428", u8"\uD544\uC694"},
    // Traditional Chinese
    {u8"\u5B58\u6A94\u4F4D", u8"\u5B58\u6A94\u4F4D", u8"\u4E0A\u6B21\u904A\u73A9", u8"\u7A7A\u5B58\u6A94\u4F4D", u8"\u6A21\u7D44", u8"\u6B63\u5E38", u8"\u5DF2\u8B8A\u66F4", u8"\u7F3A\u5C11", u8"\u65B0\u589E", u8"\u958B\u59CB", u8"\u7E7C\u7E8C", u8"\u65B0\u904A\u6232", u8"\u6A21\u7D44\u8CC7\u8A0A", u8"\u532F\u5165\u539F\u7248\u5B58\u6A94", u8"\u522A\u9664\u5B58\u6A94\u4F4D", u8"\u5C07\u6B64\u5B58\u6A94\u4F4D\u79FB\u5230\u8CC7\u6E90\u56DE\u6536\u7B52\uFF1F", u8"\u5B58\u6A94\u932F\u8AA4", u8"\u5C1A\u7121\u6A21\u7D44\u7D00\u9304", u8"\u5DF2\u9396\u5B9A\uFF1A\u5B58\u6A94\u76F8\u95DC\u6A21\u7D44\u7F3A\u5C11\u6216\u5DF2\u8B8A\u66F4", u8"\u5FC5\u8981"},
    // Portuguese
    {u8"Espa\u00E7os de save", u8"Espa\u00E7o", u8"\u00DAltimo jogo", u8"Espa\u00E7o vazio", u8"Mods", u8"ok", u8"alterados", u8"faltando", u8"novos", u8"Jogar", u8"Continuar", u8"Novo jogo", u8"Info dos mods", u8"Importar save original", u8"Apagar espa\u00E7o", u8"Mover este espa\u00E7o para a lixeira?", u8"Erro de save", u8"Nenhum mod registrado", u8"Bloqueado: mods do save faltando ou alterados", u8"necess\u00E1rio"},
    // Simplified Chinese
    {u8"\u5B58\u6863\u4F4D", u8"\u5B58\u6863\u4F4D", u8"\u4E0A\u6B21\u6E38\u73A9", u8"\u7A7A\u5B58\u6863\u4F4D", u8"\u6A21\u7EC4", u8"\u6B63\u5E38", u8"\u5DF2\u66F4\u6539", u8"\u7F3A\u5931", u8"\u65B0\u589E", u8"\u5F00\u59CB", u8"\u7EE7\u7EED", u8"\u65B0\u6E38\u620F", u8"\u6A21\u7EC4\u4FE1\u606F", u8"\u5BFC\u5165\u539F\u7248\u5B58\u6863", u8"\u5220\u9664\u5B58\u6863\u4F4D", u8"\u5C06\u6B64\u5B58\u6863\u4F4D\u79FB\u5230\u56DE\u6536\u7AD9\uFF1F", u8"\u5B58\u6863\u9519\u8BEF", u8"\u5C1A\u65E0\u6A21\u7EC4\u8BB0\u5F55", u8"\u5DF2\u9501\u5B9A\uFF1A\u5B58\u6863\u76F8\u5173\u6A21\u7EC4\u7F3A\u5931\u6216\u5DF2\u66F4\u6539", u8"\u5FC5\u9700"},
    // Russian
    {u8"\u0421\u043B\u043E\u0442\u044B \u0441\u043E\u0445\u0440\u0430\u043D\u0435\u043D\u0438\u0439", u8"\u0421\u043B\u043E\u0442", u8"\u041F\u043E\u0441\u043B\u0435\u0434\u043D\u044F\u044F \u0438\u0433\u0440\u0430", u8"\u041F\u0443\u0441\u0442\u043E\u0439 \u0441\u043B\u043E\u0442", u8"\u041C\u043E\u0434\u044B", u8"\u043E\u043A", u8"\u0438\u0437\u043C\u0435\u043D\u0435\u043D\u044B", u8"\u043D\u0435\u0442", u8"\u043D\u043E\u0432\u044B\u0435", u8"\u0418\u0433\u0440\u0430\u0442\u044C", u8"\u041F\u0440\u043E\u0434\u043E\u043B\u0436\u0438\u0442\u044C", u8"\u041D\u043E\u0432\u0430\u044F \u0438\u0433\u0440\u0430", u8"\u041C\u043E\u0434\u044B", u8"\u0418\u043C\u043F\u043E\u0440\u0442 \u043E\u0431\u044B\u0447\u043D\u043E\u0433\u043E \u0441\u043E\u0445\u0440\u0430\u043D\u0435\u043D\u0438\u044F", u8"\u0423\u0434\u0430\u043B\u0438\u0442\u044C \u0441\u043B\u043E\u0442", u8"\u041F\u0435\u0440\u0435\u043C\u0435\u0441\u0442\u0438\u0442\u044C \u0441\u043B\u043E\u0442 \u0432 \u043A\u043E\u0440\u0437\u0438\u043D\u0443?", u8"\u041E\u0448\u0438\u0431\u043A\u0430 \u0441\u043E\u0445\u0440\u0430\u043D\u0435\u043D\u0438\u044F", u8"\u041C\u043E\u0434\u044B \u0435\u0449\u0451 \u043D\u0435 \u0437\u0430\u043F\u0438\u0441\u0430\u043D\u044B", u8"\u0417\u0430\u0431\u043B\u043E\u043A\u0438\u0440\u043E\u0432\u0430\u043D\u043E: \u043C\u043E\u0434\u044B \u0441\u043E\u0445\u0440\u0430\u043D\u0435\u043D\u0438\u044F \u043E\u0442\u0441\u0443\u0442\u0441\u0442\u0432\u0443\u044E\u0442 \u0438\u043B\u0438 \u0438\u0437\u043C\u0435\u043D\u0435\u043D\u044B", u8"\u043D\u0443\u0436\u0435\u043D"},
};

int g_language = 0;
int g_selected = 1;

const char* T(Text id) {
    return kTexts[g_language][id];
}

void DetectLanguage() {
    static const char* const kSteamNames[11] = {"english", "german", "french", "spanish", "italian", "japanese",
                                                "koreana", "tchinese", "portuguese", "schinese", "russian"};
    if (HMODULE steam = GetModuleHandleA("steam_api.dll")) {
        auto apps = reinterpret_cast<void*(__cdecl*)()>(GetProcAddress(steam, "SteamAPI_SteamApps_v008"));
        auto language = reinterpret_cast<const char*(__cdecl*)(void*)>(
            GetProcAddress(steam, "SteamAPI_ISteamApps_GetCurrentGameLanguage"));
        void* object = apps ? apps() : nullptr;
        if (const char* name = object && language ? language(object) : nullptr) {
            for (int i = 0; i < 11; ++i) {
                if (_stricmp(name, kSteamNames[i]) == 0) {
                    g_language = i;
                    return;
                }
            }
        }
    }
    const auto* manager = *reinterpret_cast<const std::uint32_t* const*>(Core::Memory::Va(kProfileManagerPointerRva));
    if (manager && *manager < 11) g_language = static_cast<int>(*manager);
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

std::vector<std::string> SlotOptions(const std::vector<Slots::SlotInfo>& list) {
    std::vector<std::string> options;
    for (const auto& slot : list) {
        options.push_back("<  " + std::to_string(slot.index) + (slot.active ? " *" : "") + "  >");
    }
    return options;
}

void Refresh() {
    if (!Slots::IsReady()) return;
    const auto list = Slots::List();
    Menus::Api::SetItemOptions(kMenu, "slot", SlotOptions(list));
    Menus::Api::SetItemValue(kMenu, "slot", g_selected - 1);

    const Slots::SlotInfo* slot = Selected(list);
    if (!slot) return;
    if (slot->used) {
        Menus::Api::SetItemText(kMenu, "name", slot->meta.name);
        Menus::Api::SetItemText(kMenu, "info1", std::string(T(kLastPlayed)) + ": " + Date(slot->meta.played) +
                                                    (slot->meta.hcVersion.empty() ? "" : "  HC " + slot->meta.hcVersion));
        const auto& sum = slot->summary;
        char line[256];
        if (slot->blocked) {
            std::snprintf(line, sizeof(line), "%s", T(kLocked));
        } else {
            std::snprintf(line, sizeof(line), "%s: %d %s  %d %s  %d %s  %d %s", T(kMods), sum.match, T(kOk),
                          sum.outdated, T(kChanged), sum.missing, T(kMissing), sum.added, T(kNew));
        }
        Menus::Api::SetItemText(kMenu, "info2", line);
        Menus::Api::SetItemColor(kMenu, "info2", slot->blocked ? kRed
                                                 : (sum.missing || sum.outdated) ? kOrange
                                                 : sum.added ? kBlue : kGreen);
    } else {
        Menus::Api::SetItemText(kMenu, "name", T(kEmptySlot));
        Menus::Api::SetItemText(kMenu, "info1", " ");
        Menus::Api::SetItemText(kMenu, "info2", " ");
        Menus::Api::SetItemColor(kMenu, "info2", 0);
    }
    Menus::Api::SetItemText(kMenu, "play", slot->active ? T(kContinue) : slot->used ? T(kPlay) : T(kNewGame));
    Menus::Api::SetItemEnabled(kMenu, "play", !slot->blocked);
    Menus::Api::SetItemEnabled(kMenu, "modinfo", slot->used);
    Menus::Api::SetItemEnabled(kMenu, "import", !slot->used && Slots::VanillaSaveAvailable());
    Menus::Api::SetItemEnabled(kMenu, "delete", slot->used && !slot->active);
}

void ShowError(const std::string& error) {
    Core::Logger::Error("SaveSlotMenu: %s", error.c_str());
    UpdatePromptMenu::CreatePrompt(std::string(T(kError)) + ": " + error, {}, {});
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
    UpdatePromptMenu::CreatePrompt(T(kConfirmDelete), [index]() {
        std::string error;
        if (!Slots::Delete(index, error)) ShowError(error);
    }, {});
}

void OnModInfo(int) {
    const auto list = Slots::List();
    const Slots::SlotInfo* slot = Selected(list);
    if (!slot) return;
    Menus::Api::SetItemText(kModsMenu, "title", std::string(T(kModInfo)) + " - " + T(kSlot) + " " + std::to_string(slot->index));
    const int count = static_cast<int>(slot->mods.size());
    for (int line = 0; line < kModLines; ++line) {
        std::string text = " ";
        std::uint32_t color = 0;
        if (count == 0 && line == 0) {
            text = T(kNoMods);
        } else if (line == kModLines - 1 && count > kModLines) {
            text = "+" + std::to_string(count - (kModLines - 1)) + " ...";
        } else if (line < count) {
            const auto& mod = slot->mods[static_cast<std::size_t>(line)];
            switch (mod.state) {
            case Slots::ModState::Match: text = mod.name + " " + mod.savedVersion; color = kGreen; break;
            case Slots::ModState::Outdated:
                text = mod.name + " " + mod.savedVersion + " -> " + mod.currentVersion;
                color = Slots::IsBlocking(mod) ? kRed : kOrange;
                break;
            case Slots::ModState::Missing:
                text = mod.name + " " + mod.savedVersion + " (" + (mod.touchesSave ? T(kNeeded) : T(kMissing)) + ")";
                color = Slots::IsBlocking(mod) ? kRed : kOrange;
                break;
            case Slots::ModState::Added: text = mod.name + " " + mod.currentVersion + " (" + T(kNew) + ")"; color = kBlue; break;
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
    bool ok = Menus::Api::CreateMenu(kMenu, T(kTitle));
    std::vector<std::string> options;
    for (int i = 1; i <= Slots::kMaxSlots; ++i) options.push_back("<  " + std::to_string(i) + "  >");
    // The selector wraps: left on slot 1 goes to slot 10 and right on 10 to 1.
    ok = ok && Menus::Api::AddChoice(kMenu, "slot", T(kSlot), options, 0, [](int value) {
        g_selected = value + 1;
        Refresh();
    });
    ok = ok && Menus::Api::AddLabel(kMenu, "name", " ") && Menus::Api::AddLabel(kMenu, "info1", " ") &&
         Menus::Api::AddLabel(kMenu, "info2", " ");
    ok = ok && Menus::Api::AddButton(kMenu, "play", T(kPlay), OnPlay);
    ok = ok && Menus::Api::AddButton(kMenu, "modinfo", T(kModInfo), OnModInfo);
    ok = ok && Menus::Api::AddButton(kMenu, "import", T(kImportVanilla), OnImport);
    ok = ok && Menus::Api::AddButton(kMenu, "delete", T(kDelete), OnDelete);
    // The slot screen is a choice: no Back button, the Back key does nothing.
    ok = ok && Menus::Api::SetBackButton(kMenu, false);
    // Mod Info: the menu title is a label so it can name the slot.
    ok = ok && Menus::Api::CreateMenu(kModsMenu, "") && Menus::Api::AddLabel(kModsMenu, "title", T(kModInfo));
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
    Core::Logger::Info("SaveSlotMenu: installed (language %d), open=%s", g_language, Menus::Api::OpenResultName(result));
    return true;
}

} // namespace HookCrashers::UI::SaveSlotMenu
