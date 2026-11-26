// Standalone: clang++ -std=c++17 tests/MenuModelTests.cpp src/UI/MenuModel.cpp && ./a.out
#include "../src/UI/MenuModel.h"

#include <cstdio>
#include <cstdlib>
#include <string>

using namespace HookCrashers::UI::Menus;

namespace {
int g_checks = 0;
void Check(bool good, const char* message) {
    ++g_checks;
    if (!good) {
        std::printf("FAIL %s\n", message);
        std::exit(1);
    }
}
Item Make(const std::string& id, ItemKind kind) {
    Item item;
    item.id = id;
    item.kind = kind;
    item.label = id;
    return item;
}
} // namespace

int main() {
    Registry& registry = Registry::Instance();
    registry.Clear();

    Check(IsValidId("my.mod:menu-1_A"), "ids accept letters, digits and . : - _");
    Check(!IsValidId("") && !IsValidId("has space") && !IsValidId(std::string(65, 'a')), "ids reject empty/space/too long");

    Check(registry.CreateMenu("mod.main", "Main") == Result::Ok, "create menu");
    Check(registry.CreateMenu("mod.main", "Again") == Result::DuplicateId, "duplicate menu rejected");
    Check(registry.CreateMenu("bad id", "x") == Result::InvalidArgument, "invalid menu id rejected");
    Check(registry.AddItem("missing", Make("a", ItemKind::Button)) == Result::UnknownMenu, "item on unknown menu rejected");

    Menu snapshot;
    registry.Snapshot("mod.main", snapshot);
    const auto firstRevision = snapshot.revision;

    Check(registry.AddItem("mod.main", Make("play", ItemKind::Button)) == Result::Ok, "button added");
    Check(registry.AddItem("mod.main", Make("play", ItemKind::Label)) == Result::DuplicateId, "duplicate item id rejected");

    Item toggle = Make("god", ItemKind::Toggle);
    toggle.value = 7;
    Check(registry.AddItem("mod.main", toggle) == Result::Ok, "toggle added");
    Item stored;
    Check(registry.ItemAt("mod.main", 1, stored) && stored.value == 1, "toggle value normalized to 1");
    Check(stored.options.size() == 2 && stored.options[0] == "Off" && stored.options[1] == "On", "toggle gets Off/On labels");

    Item badToggle = Make("bad", ItemKind::Toggle);
    badToggle.options = {"one"};
    Check(registry.AddItem("mod.main", badToggle) == Result::InvalidArgument, "toggle with one label rejected");

    Item choice = Make("difficulty", ItemKind::Choice);
    choice.options = {"Easy", "Normal", "Hard"};
    choice.value = 99;
    Check(registry.AddItem("mod.main", choice) == Result::Ok, "choice added");
    Check(registry.ItemAt("mod.main", 2, stored) && stored.value == 2, "choice value clamped to last option");

    Item emptyChoice = Make("empty", ItemKind::Choice);
    Check(registry.AddItem("mod.main", emptyChoice) == Result::InvalidArgument, "choice without options rejected");

    Item submenu = Make("more", ItemKind::Submenu);
    submenu.target = "mod.main";
    Check(registry.AddItem("mod.main", submenu) == Result::InvalidArgument, "submenu to itself rejected");
    submenu.target = "mod.more";
    Check(registry.AddItem("mod.main", submenu) == Result::Ok, "submenu added (target may be created later)");

    Item graphic = Make("logo", ItemKind::Graphic);
    Check(registry.AddItem("mod.main", graphic) == Result::InvalidArgument, "graphic without graphic id rejected");
    graphic.graphic = "mod.logo";
    Check(registry.AddItem("mod.main", graphic) == Result::Ok, "graphic added");

    registry.Snapshot("mod.main", snapshot);
    Check(snapshot.revision > firstRevision, "structural changes bump the revision");
    Check(CountTextItems(snapshot) == 4 && CountGraphics(snapshot) == 1, "text and graphic counts");

    const auto beforeLive = snapshot.revision;
    Check(registry.SetItemText("mod.main", "play", "Play now") == Result::Ok, "live text update");
    Check(registry.SetItemValue("mod.main", "difficulty", 0) == Result::Ok, "live value update");
    Check(registry.SetItemEnabled("mod.main", "play", false) == Result::Ok, "live enable update");
    Check(registry.SetItemEnabled("mod.main", "logo", false) == Result::WrongKind, "graphics cannot be disabled");
    Check(registry.SetItemValue("mod.main", "play", 1) == Result::WrongKind, "buttons have no value");
    registry.Snapshot("mod.main", snapshot);
    Check(snapshot.revision == beforeLive, "live updates do not force a rebuild");
    Check(snapshot.items[0].label == "Play now" && !snapshot.items[0].enabled, "live updates stored");

    {
        Menu before;
        registry.Snapshot("mod.main", before);
        Check(registry.SetItemOptions("mod.main", "difficulty", {"E", "N", "H"}) == Result::Ok, "same-count options update");
        Menu after;
        registry.Snapshot("mod.main", after);
        Check(after.revision == before.revision && after.items[2].options[1] == "N", "same-count options are live");
        Check(registry.SetItemOptions("mod.main", "difficulty", {"Easy", "Normal", "Hard", "Insane"}) == Result::Ok, "options grow");
        registry.Snapshot("mod.main", after);
        Check(after.revision > before.revision, "option count change is structural");
        Check(registry.SetItemOptions("mod.main", "play", {"x"}) == Result::WrongKind, "buttons have no options");
        Check(registry.SetItemOptions("mod.main", "god", {"a", "b", "c"}) == Result::InvalidArgument, "toggle needs two labels");
        Check(registry.SetItemOptions("mod.main", "difficulty", {"Easy", "Normal", "Hard"}) == Result::Ok, "options restored");
    }

    int value = -1;
    Check(registry.GetItemValue("mod.main", "difficulty", value) == Result::Ok && value == 0, "get value");
    Check(registry.CommitValue("mod.main", 2, 1, stored) == Result::Ok && stored.value == 1, "native change committed");
    Check(registry.CommitValue("mod.main", 0, 1, stored) == Result::WrongKind, "native change on button rejected");
    Check(registry.CommitValue("mod.main", 42, 1, stored) == Result::UnknownItem, "native change out of range rejected");

    int fired = -1;
    Item callbackItem = Make("cb", ItemKind::Button);
    callbackItem.onActivate = [&fired](int v) { fired = v; };
    Check(registry.AddItem("mod.main", callbackItem) == Result::Ok, "button with callback");
    Item copy;
    registry.ItemAt("mod.main", 5, copy);
    copy.onActivate(3);
    Check(fired == 3, "callbacks survive snapshots");

    Check(registry.CreateMenu("mod.full", "Full") == Result::Ok, "second menu");
    for (int i = 0; i < static_cast<int>(kMaxTextItems); ++i) {
        Check(registry.AddItem("mod.full", Make("i" + std::to_string(i), ItemKind::Label)) == Result::Ok, "fill text items");
    }
    Check(registry.AddItem("mod.full", Make("overflow", ItemKind::Button)) == Result::LimitReached, "text item limit enforced");

    for (std::size_t i = registry.MenuCount(); i < kMaxMenus; ++i) {
        Check(registry.CreateMenu("mod.m" + std::to_string(i), "") == Result::Ok, "fill menu slots");
    }
    Check(registry.CreateMenu("mod.overflow", "") == Result::LimitReached, "menu limit (mod slots) enforced");

    Check(registry.SetItemText("mod.main", "play", std::string(300, 'x')) == Result::InvalidArgument, "long text rejected");

    registry.Clear();
    std::printf("PASS menu model (%d checks)\n", g_checks);
    return 0;
}
