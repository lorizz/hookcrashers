// Production reader/cache plus an isolated model of the verified x86 VM layout.
// No game startup or game process is involved.
#define HOOKCRASHERS_EXPORTS
#include "../src/Characters/CurrentCharacterData.cpp"
#include "../src/NativeDB/Natives.h"
#include <deque>
#include <memory>
#include <string>
#include <thread>
#include <atomic>
#include <algorithm>
#include <cstdlib>

using namespace HookCrashers;
using namespace HookCrashers::Characters::CurrentData;
namespace {
void Check(bool ok, const char* message) {
    if (!ok) { std::printf("FAIL %s\n", message); std::exit(1); }
}
unsigned updateCalls = 0, levelUps = 0;
double previousLevel = 0;
std::uint64_t updateSequence = 0;
DWORD updateThread = 0;
void __stdcall TestUpdate() {
    const auto data = GetCurrentCharacterData(1);
    Check(data.result == CharacterDataResult::Ok && data.sequence > updateSequence, "callback sees newly published data");
    updateSequence = data.sequence; updateThread = GetCurrentThreadId(); ++updateCalls;
    if (previousLevel && data.level > previousLevel) ++levelUps;
    previousLevel = data.level;
    Check(!RegisterUpdateCallback(TestUpdate) && !UnregisterUpdateCallback(TestUpdate), "callback cannot mutate dispatch registry");
}
unsigned faultCalls = 0;
void __stdcall FaultUpdate() { ++faultCalls; RaiseException(0xE0001234, 0, 0, nullptr); }
HANDLE updateEntered, updateRelease;
void __stdcall BlockingUpdate() { SetEvent(updateEntered); WaitForSingleObject(updateRelease, 10000); }
std::uint32_t Address(const void* pointer) { return reinterpret_cast<std::uint32_t>(pointer); }
struct FakeObject {
    std::uint32_t vtable;
    Map map;
    std::uint8_t padding[12] {};
    std::uint32_t type = 0x40;
    std::uint32_t referenceCount = 1;
    std::uint32_t elements = 0;
    std::uint32_t length = 0;
};
static_assert(offsetof(FakeObject, type) == 0x18 && offsetof(FakeObject, elements) == 0x20);
std::uint8_t __fastcall Getter(FakeObject* object, void*, unsigned name, Argument* output) {
    if (object->type == 0x80 && name == 1) {
        *output = {0, 4, 0, object->length}; return 1;
    }
    const auto* entries = reinterpret_cast<Entry*>(object->map.entries);
    for (unsigned i = 0; i < object->map.count; ++i) if (entries[i].name == name) {
        const auto& stored = *reinterpret_cast<StoredValue*>(entries[i].value);
        *output = {stored.object, stored.type, 0, stored.scalar};
        if (stored.type == 1 && stored.object) output->type = reinterpret_cast<FakeObject*>(stored.object)->type;
        return 1;
    }
    *output = {0, 0x80000000, 0, 0}; return 0;
}
std::uint32_t vtable[4] {0,0,0,Address(reinterpret_cast<void*>(&Getter))};
std::uint32_t arrayVtable[4] {0,0,0,Address(reinterpret_cast<void*>(&Getter))};
struct NativeFixture {
    std::array<unsigned char*,5> code {};
    explicit NativeFixture(const char* path) {
        FILE* file = nullptr; fopen_s(&file,path,"rb"); Check(file != nullptr,"native fixture open");
        char magic[8] {}; unsigned sizes[5] {}, count = 0;
        Check(std::fread(magic,1,8,file) == 8 && std::memcmp(magic,"HCCDAT1\0",8) == 0,"native fixture magic");
        Check(std::fread(sizes,4,5,file) == 5 && std::fread(&count,4,1,file) == 1,"native fixture header");
        for (unsigned i = 0; i < 5; ++i) {
            Check(sizes[i] <= 4096,"native helper bounds");
            code[i] = static_cast<unsigned char*>(VirtualAlloc(nullptr,4096,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE));
            Check(code[i] && std::fread(code[i],1,sizes[i],file) == sizes[i],"native helper bytes");
        }
        Check(count <= 8,"native call fixup bounds");
        for (unsigned i = 0; i < count; ++i) {
            unsigned fixup[3]; Check(std::fread(fixup,4,3,file) == 3,"native call fixup");
            Check(fixup[0] < 5 && fixup[2] < 5 && fixup[1]+4 <= sizes[fixup[0]],"native call target bounds");
            const auto displacement = Address(code[fixup[2]]) - Address(code[fixup[0]] + fixup[1] + 4);
            std::memcpy(code[fixup[0]] + fixup[1], &displacement,4);
        }
        std::fclose(file);
        for (auto block : code) { DWORD protection; Check(VirtualProtect(block,4096,PAGE_EXECUTE_READ,&protection) != 0,"helper RX");
            FlushInstructionCache(GetCurrentProcess(),block,4096); }
    }
    ~NativeFixture() { for (auto block : code) VirtualFree(block,0,MEM_RELEASE); }
};
struct Model {
    std::array<std::uint32_t, 12> strings {};
    std::array<std::uint32_t, 4096> pointers {};
    std::array<std::uint16_t, 8192> hashes {};
    std::deque<std::string> text;
    std::deque<StoredValue> values;
    struct Owned { FakeObject object {}; std::vector<Entry> entries; std::vector<Argument> elements; };
    std::deque<Owned> objects;
    std::vector<std::uint8_t> context = std::vector<std::uint8_t>(0x4350);
    Model() {
        strings[6] = static_cast<unsigned>(pointers.size());
        strings[7] = Address(pointers.data()); strings[8] = static_cast<unsigned>(hashes.size());
        strings[9] = Address(hashes.data()); strings[10] = 0x12345678; strings[11] = 0x87654321;
        *reinterpret_cast<std::uint32_t*>(context.data() + 0x2F2C) = Address(strings.data());
    }
    std::uint16_t Name(const char* name) {
        if (const auto existing = NameId(Address(strings.data()), name)) return existing;
        Check(text.size() + 1 < pointers.size(), "test string capacity");
        text.emplace_back(name);
        const auto id = static_cast<std::uint16_t>(text.size()); pointers[id] = Address(text.back().c_str());
        unsigned hash = 0, weight = 0x77;
        for (const auto* p = reinterpret_cast<const unsigned char*>(name); *p; ++p) hash += *p * weight++;
        hash &= static_cast<unsigned>(hashes.size() - 1);
        while (hashes[hash]) hash = (hash + 1) & static_cast<unsigned>(hashes.size() - 1);
        hashes[hash] = id;
        return id;
    }
    FakeObject* New(std::uint32_t type = 0x40) {
        objects.emplace_back(); auto& object = objects.back().object;
        object.vtable = Address(type == 0x80 ? arrayVtable : vtable); object.type = type; return &object;
    }
    void Set(FakeObject* object, const char* name, StoredValue value) {
        auto& owned = *std::find_if(objects.begin(), objects.end(), [&](auto& o){ return &o.object == object; });
        const auto id = Name(name);
        for (auto& entry : owned.entries) if (entry.name == id) {
            *reinterpret_cast<StoredValue*>(entry.value) = value; return;
        }
        values.push_back(value); owned.entries.push_back({id, 0, Address(&values.back())});
        std::sort(owned.entries.begin(), owned.entries.end(), [](auto& a, auto& b){ return a.name < b.name; });
        object->map = {static_cast<unsigned>(owned.entries.size()), Address(owned.entries.data())};
    }
    void Int(FakeObject* object, const char* name, int value) { Set(object,name,{0,4,static_cast<unsigned>(value)}); }
    void Bool(FakeObject* object, const char* name, bool value) { Set(object,name,{0,2,value ? 1u : 0u}); }
    void Float(FakeObject* object, const char* name, float value) {
        unsigned bits; std::memcpy(&bits,&value,4); Set(object,name,{0,16,bits});
    }
    void Ref(FakeObject* object, const char* name, FakeObject* ref) { Set(object,name,{Address(ref),1,0}); }
    void Array(FakeObject* object, const std::vector<Argument>& elements) {
        auto& owned = *std::find_if(objects.begin(), objects.end(), [&](auto& o){ return &o.object == object; });
        owned.elements = elements; object->type = 0x80; object->elements = Address(owned.elements.data());
        object->length = static_cast<unsigned>(owned.elements.size());
    }
};
std::uint32_t observedSelf = 0, observedTime = 0;
void* injectedFrame = nullptr;
void* injectedProfile = nullptr;
__declspec(naked) void InvokeInjectedFrame() {
    __asm {
        mov ecx, injectedProfile
        mov eax, 3f000000h
        movd xmm1, eax
        call dword ptr [injectedFrame]
        ret
    }
}
__declspec(naked) void OriginalFrame() {
    __asm {
        mov observedSelf, ecx
        movss observedTime, xmm1
        ret
    }
}
__declspec(naked) void InvokeFrameHook() {
    __asm {
        mov ecx, 12345678h
        mov eax, 3f000000h
        movd xmm1, eax
        call FrameHook
        ret
    }
}
}

int main(int argc, char** argv) {
    Check(GetCurrentCharacterData().result == CharacterDataResult::Unavailable, "initial state");
    auto model = std::make_unique<Model>();
    auto* root = model->New();
    *reinterpret_cast<unsigned*>(model->context.data() + 0x5C) = Address(root);
    auto* loader = model->New(); auto* outerGame = model->New(); auto* game = model->New();
    model->Ref(root,"loader",loader); model->Ref(loader,"game",outerGame); model->Ref(outerGame,"game",game);
    model->Bool(root,"g_bLoading",false);
    FakeObject* hud[4] {}; FakeObject* player[3] {};
    for (unsigned i = 0; i < 4; ++i) {
        hud[i] = model->New();
        std::string name = "hud" + std::to_string(i + 1); model->Ref(root,name.c_str(),hud[i]);
        model->Int(hud[i],"port",i == 2 ? 1 : i + 1);
        model->Bool(hud[i],"alive",i < 3);
        model->Int(hud[i],"health",999); // Must never replace live health silently.
        if (i == 3) continue;
        player[i] = model->New(); model->Ref(player[i],"hud_pt",hud[i]);
        model->Ref(hud[i],"player_pt",player[i]);
        name = "p" + std::to_string(i + 1); model->Ref(game,name.c_str(),player[i]);
        model->Int(player[i],"p_type",33 + i); model->Float(player[i],"health",i == 1 ? 0.0f : 12.5f + i);
        model->Bool(player[i],"alive",i != 1); model->Int(player[i],"level",19 + i);
        model->Int(player[i],"gold",1000 + i);
    }
    // Intentionally wrong reciprocal link: use the explicit p1 path instead.
    model->Ref(hud[0],"player_pt",player[1]);
    auto* unlocks = model->New(0x80);
    model->Array(unlocks,{{0,4,0,1},{0,4,0,2},{0,4,0,255}});
    model->Ref(player[0],"normal_level_unlocks",unlocks);
    auto* custom = model->New(1); model->Int(custom,"counter",321);
    model->Ref(custom,"self",custom); model->Ref(player[0],"custom_data",custom);
    auto* pet = model->New(); model->Int(pet,"animal_type",6); model->Ref(player[0],"pet",pet);
    const std::string longText(180,'x'); const auto textId = model->Name(longText.c_str());
    model->Set(player[0],"test_string",{0,8,textId});
    CaptureFromContext(model->context.data());
    const auto first = GetCurrentCharacterData(1);
    Check(first.result == CharacterDataResult::Ok && first.source == CharacterDataSource::LivePlayer &&
          first.health == 12.5 && first.level == 19 && first.characterId == 33 && first.savePort == 0 && first.animalType == 6,
          "live HUD1 mapping, float preservation, identity and save port");
    Check(!HasCharacterField(first,CurrentCharacterField::Defense), "missing value distinguished from zero");
    Check(GetCurrentCharacterData(2).health == 0 && GetCurrentCharacterData(2).state == CharacterDataState::InLevel,
          "dead player still has valid live data");
    Check(GetCurrentCharacterData(3).playerSlot == 3 && GetCurrentCharacterData(3).savePort == 0,
          "HUD slot independent from save port");
    Check(GetCurrentCharacterData(4).state == CharacterDataState::Inactive &&
          GetCurrentCharacterData(4).source == CharacterDataSource::Hud, "inactive HUD fallback marked");
    bool array = false, element = false, cycle = false, truncatedString = false;
    for (unsigned i = 0; i < first.propertyCount; ++i) {
        CurrentCharacterProperty property {};
        Check(HookCrashers_GetCurrentCharacterProperty(1,first.sequence,i,&property,sizeof(property)) == CharacterDataResult::Ok,
              "property enumeration same sequence");
        if (property.kind == CharacterPropertyKind::Array && property.number == 3) array = true;
        if (property.parentIndex != UINT32_MAX && std::strcmp(property.name,"2") == 0 && property.number == 255) element = true;
        if (std::strcmp(property.name,"self") == 0 && (property.flags & CharacterProperty_Reference)) cycle = true;
        if (std::strcmp(property.name,"test_string") == 0 && (property.flags & CharacterProperty_Truncated) &&
            property.text[127] == 0) truncatedString = true;
    }
    Check(array && element && cycle && truncatedString && (first.flags & CharacterData_Truncated),
          "arrays, nested addon properties, cycles and explicit truncation");
    Check(model->strings[10] == 0x12345678 && model->strings[11] == 0x87654321,
          "string table cursors not modified");
    std::puts("PASS 4-player identity, HUD/live mapping, floats/zero/missing fields, arrays, custom properties, cycles and truncation");
    Check(argc == 3,"native and skipped-frame fixture paths required");
    NativeFixture native(argv[1]);
    vtable[3] = Address(native.code[0]); // Real native own-property getter + real property map lookup.
    arrayVtable[3] = Address(native.code[4]); // Real native array length getter.
    CaptureFromContext(model->context.data());
    const auto real = GetCurrentCharacterData(1);
    Check(real.result == CharacterDataResult::Ok && real.health == first.health &&
          real.level == first.level && real.propertyCount == first.propertyCount,
          "production reader with real native getter, property map and object-reference helper");
    using HashFn = unsigned(__thiscall*)(void*,const char*);
    auto nativeHash = reinterpret_cast<HashFn>(native.code[3]);
    for (const char* name : {"health","hud1","p_type","normal_level_unlocks"}) {
        unsigned hash = 0, weight = 0x77;
        for (const auto* p = reinterpret_cast<const unsigned char*>(name); *p; ++p) hash += *p * weight++;
        Check(nativeHash(model->strings.data(),name) == (hash & (model->hashes.size()-1)),"native string hash agreement");
    }
    std::puts("PASS actual native property getter/map lookup/object references, array length getter and native string hash");
    model->Bool(root,"g_bLoading",true); CaptureFromContext(model->context.data());
    Check(GetCurrentCharacterData(1).state == CharacterDataState::Loading &&
          GetCurrentCharacterData(1).source == CharacterDataSource::Hud && GetCurrentCharacterData(1).health == 999,
          "loading state explicitly switches to HUD data");
    CurrentCharacterProperty property {};
    Check(HookCrashers_GetCurrentCharacterProperty(1,first.sequence,0,&property,sizeof(property)) == CharacterDataResult::SequenceChanged,
          "enumeration cannot silently mix snapshots");
    CurrentCharacterData data {};
    Check(HookCrashers_GetCurrentCharacterData(0,&data,sizeof(data)) == CharacterDataResult::InvalidPlayer &&
          HookCrashers_GetCurrentCharacterData(1,&data,1) == CharacterDataResult::InvalidBuffer &&
          HookCrashers_GetCurrentCharacterData(1,reinterpret_cast<CurrentCharacterData*>(1),sizeof(data)) == CharacterDataResult::InvalidBuffer,
          "invalid caller arguments");
    AcquireSRWLockExclusive(&g_Lock); g_Cache[0].data.capturedAtMilliseconds = GetTickCount64() - 501;
    ReleaseSRWLockExclusive(&g_Lock);
    Check(GetCurrentCharacterData(1).result == CharacterDataResult::Stale, "stale snapshots explicitly reported");
    model->Bool(root,"g_bLoading",false);
    std::atomic<bool> stop {false}; std::atomic<bool> coherent {true};
    std::thread reader([&]{ while (!stop) { const auto d = GetCurrentCharacterData(1);
        if (d.result == CharacterDataResult::Ok && (d.health != 12.5 || d.characterId != 33)) coherent = false; } });
    for (unsigned i = 0; i < 1000; ++i) CaptureFromContext(model->context.data());
    stop = true; reader.join(); Check(coherent, "concurrent API reads preserve complete copies");
    std::puts("PASS loading/source state, sequence consistency, invalid buffers, staleness and concurrent cache readers");
    auto* image = static_cast<unsigned char*>(VirtualAlloc(nullptr,0x400000,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE));
    Check(image != nullptr,"isolated profile allocation");
    NativeDB::SetGameBase(Address(image));
    std::array<unsigned char,0x898> profile {}; std::array<unsigned char,0x78> movie {};
    *reinterpret_cast<unsigned*>(image + kProfileGlobalRva) = Address(profile.data());
    *reinterpret_cast<unsigned*>(profile.data() + 0x190) = Address(movie.data());
    *reinterpret_cast<unsigned*>(movie.data() + 0x70) = Address(model->context.data());
    g_OriginalFrameUpdate = reinterpret_cast<void*>(&OriginalFrame);
    InvokeFrameHook();
    Check(observedSelf == 0x12345678 && observedTime == 0x3F000000 &&
          GetCurrentCharacterData(1).health == 12.5,"assembly bridge preserves private ECX/XMM1 ABI and captures after update");
    // Execute only the native frame's disabled-update branch. No game startup,
    // virtual update calls or asset/network calls are reached. A local RET stub
    // replaces its security-cookie helper; the native frame prologue/epilogue,
    // XMM1 input and production Detours transaction remain real.
    Check(!Install(),"signature mismatch refuses hook installation");
    FILE* frameFile = nullptr; fopen_s(&frameFile,argv[2],"rb");
    Check(frameFile && std::fread(image + kFrameUpdateRva,1,503,frameFile) == 503,"skipped-frame fixture");
    std::fclose(frameFile);
    const unsigned cookieAddress = Address(image + 0x2592CC);
    std::memcpy(image + kFrameUpdateRva + 10,&cookieAddress,4);
    image[0x16594A] = 0xC3;
    profile[0x30] = 1;
    DWORD protection = 0;
    Check(VirtualProtect(image + 0x100000,0x70000,PAGE_EXECUTE_READ,&protection) != 0,"isolated frame RX");
    FlushInstructionCache(GetCurrentProcess(),image + 0x100000,0x70000);
    injectedProfile = profile.data(); injectedFrame = image + kFrameUpdateRva;
    Check(Install(),"production capture hook attaches to verified native frame");
    const auto beforeFrame = GetCurrentCharacterData(1).sequence;
    InvokeInjectedFrame();
    Check(GetCurrentCharacterData(1).sequence > beforeFrame && GetCurrentCharacterData(1).health == 12.5,
          "actual Detours hook captures after native skipped-frame branch");
    Shutdown();
    const auto afterDetach = GetCurrentCharacterData(1).sequence;
    InvokeInjectedFrame();
    Check(GetCurrentCharacterData(1).sequence == afterDetach,"detach restores original update without capture");
    Check(GetProcAddress(GetModuleHandleA(nullptr),"GetCurrentCharacterData") != nullptr,
          "undecorated API alias available to GetProcAddress consumers");
    Check(!RegisterUpdateCallback(nullptr) && !UnregisterUpdateCallback(nullptr), "null callbacks rejected");
    Check(RegisterUpdateCallback(TestUpdate) && !RegisterUpdateCallback(TestUpdate), "register once without duplicate callbacks");
    Check(RegisterUpdateCallback(FaultUpdate), "register isolated fault callback");
    CaptureFromContext(model->context.data());
    model->Int(player[0],"level",20);
    CaptureFromContext(model->context.data());
    CaptureFromContext(model->context.data());
    Check(updateCalls == 3 && levelUps == 1 && updateThread == GetCurrentThreadId(), "same-thread update detects one level increase");
    Check(faultCalls == 1, "faulting callback disabled without stopping healthy callbacks");
    Check(UnregisterUpdateCallback(TestUpdate) && !UnregisterUpdateCallback(TestUpdate) &&
          UnregisterUpdateCallback(FaultUpdate), "unregister removes callbacks");
    CaptureFromContext(model->context.data());
    Check(updateCalls == 3, "no calls after successful unregister");
    updateEntered = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    updateRelease = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    Check(RegisterUpdateCallback(BlockingUpdate), "register blocking test callback");
    std::thread dispatch([&]{ CaptureFromContext(model->context.data()); });
    Check(WaitForSingleObject(updateEntered,5000) == WAIT_OBJECT_0, "callback entered");
    std::atomic<bool> removalStarted {false}, removalDone {false};
    std::thread removal([&]{ removalStarted = true; Check(UnregisterUpdateCallback(BlockingUpdate), "remove active callback"); removalDone = true; });
    while (!removalStarted) SwitchToThread();
    Sleep(20); Check(!removalDone, "unregister waits for active invocation before module unload");
    SetEvent(updateRelease); dispatch.join(); removal.join();
    CloseHandle(updateEntered); CloseHandle(updateRelease);
    std::puts("PASS update callback publication order/thread, level-up detection, duplicate/null/reentrant registration, fault isolation and synchronized unregister");
    CaptureFromContext(nullptr);
    Check(GetCurrentCharacterData(1).result == CharacterDataResult::Unavailable,"removed scene invalidates old data");
    VirtualFree(image,0,MEM_RELEASE); NativeDB::SetGameBase(0);
    std::puts("PASS production assembly bridge, actual native skipped-frame Detours attach/detach, export alias and scene invalidation");
    std::puts("ALL OFFLINE CURRENT CHARACTER API TESTS PASSED (no game launched)");
}
