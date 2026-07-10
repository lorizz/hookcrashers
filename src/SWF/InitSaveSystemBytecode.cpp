#include "InitSaveSystemBytecode.h"

#include "../Characters/CharacterRegistry.h"
#include "../NativeDB/Classes.h"
#include <cstring>

namespace HookCrashers::SWF {
namespace {

enum class Action : std::uint8_t {
    Push = 0x96,
    SetVariable = 0x1D,
    GetVariable = 0x1C,
    SetMember = 0x4F,
    NewObject = 0x40,
    End = 0x00
};

void U16(std::vector<std::uint8_t>& out, std::uint16_t value) {
    out.push_back(static_cast<std::uint8_t>(value & 0xFF));
    out.push_back(static_cast<std::uint8_t>((value >> 8) & 0xFF));
}

void U32(std::vector<std::uint8_t>& out, std::uint32_t value) {
    out.push_back(static_cast<std::uint8_t>(value & 0xFF));
    out.push_back(static_cast<std::uint8_t>((value >> 8) & 0xFF));
    out.push_back(static_cast<std::uint8_t>((value >> 16) & 0xFF));
    out.push_back(static_cast<std::uint8_t>((value >> 24) & 0xFF));
}

void F64(std::vector<std::uint8_t>& out, double value) {
    std::uint8_t bytes[8] = {};
    std::memcpy(bytes, &value, sizeof(bytes));
    out.insert(out.end(), bytes, bytes + sizeof(bytes));
}

void ActionHeader(std::vector<std::uint8_t>& out, Action action, std::uint16_t length) {
    out.push_back(static_cast<std::uint8_t>(action));
    U16(out, length);
}

void PushString(std::vector<std::uint8_t>& payload, const char* value) {
    payload.push_back(0x00);
    while (*value) {
        payload.push_back(static_cast<std::uint8_t>(*value++));
    }
    payload.push_back(0x00);
}

void PushDouble(std::vector<std::uint8_t>& payload, double value) {
    payload.push_back(0x06);
    F64(payload, value);
}

void PushInt(std::vector<std::uint8_t>& payload, std::int32_t value) {
    payload.push_back(0x07);
    U32(payload, static_cast<std::uint32_t>(value));
}

void EmitPush(std::vector<std::uint8_t>& out, const std::vector<std::uint8_t>& payload) {
    ActionHeader(out, Action::Push, static_cast<std::uint16_t>(payload.size()));
    out.insert(out.end(), payload.begin(), payload.end());
}

void EmitPushString(std::vector<std::uint8_t>& out, const char* value) {
    std::vector<std::uint8_t> payload;
    PushString(payload, value);
    EmitPush(out, payload);
}

void EmitSetMemberInt(std::vector<std::uint8_t>& out, const char* objectName, const char* memberName, std::int32_t value) {
    EmitPushString(out, objectName);
    out.push_back(static_cast<std::uint8_t>(Action::GetVariable));

    std::vector<std::uint8_t> payload;
    PushString(payload, memberName);
    PushInt(payload, value);
    EmitPush(out, payload);

    out.push_back(static_cast<std::uint8_t>(Action::SetMember));
}

} // namespace

std::vector<std::uint8_t> BuildInitSaveSystemBytecode() {
    const Characters::Registry& registry = Characters::Registry::Instance();
    std::vector<std::uint8_t> out;

    {
        std::vector<std::uint8_t> payload;
        PushString(payload, "save_data_info");
        PushDouble(payload, 0.0);
        PushString(payload, "Object");
        EmitPush(out, payload);
        out.push_back(static_cast<std::uint8_t>(Action::NewObject));
        out.push_back(static_cast<std::uint8_t>(Action::SetVariable));
    }

    constexpr const char* objectName = "save_data_info";
    EmitSetMemberInt(out, objectName, "char_offset", static_cast<int>(NativeDB::kGlobalDataSize));
    EmitSetMemberInt(out, objectName, "char_size", static_cast<int>(NativeDB::kCharacterDataSize));
    EmitSetMemberInt(out, objectName, "num_items", 128);
    EmitSetMemberInt(out, objectName, "num_animals", 32);
    EmitSetMemberInt(out, objectName, "num_levels", 64);
    EmitSetMemberInt(out, objectName, "num_relics", 8);
    EmitSetMemberInt(out, objectName, "num_items_expansion", 64);
    EmitSetMemberInt(out, objectName, "num_characters_legacy", registry.ClassicVisibleCount());
    EmitSetMemberInt(out, objectName, "num_characters_noaddons", registry.NetworkBaseCount());
    EmitSetMemberInt(out, objectName, "num_characters_safe", registry.SaveCharacterSlotCount());
    EmitSetMemberInt(out, objectName, "num_character_addons", registry.AddonCount());
    EmitSetMemberInt(out, objectName, "num_characters", registry.NetworkTotalCount());

    {
        std::vector<std::uint8_t> payload;
        PushString(payload, "relic_offset");
        PushInt(payload, 40);
        EmitPush(out, payload);
        out.push_back(static_cast<std::uint8_t>(Action::SetVariable));
    }

    {
        std::vector<std::uint8_t> payload;
        PushString(payload, "weapon_offset");
        PushInt(payload, 50);
        EmitPush(out, payload);
        out.push_back(static_cast<std::uint8_t>(Action::SetVariable));
    }

    out.push_back(static_cast<std::uint8_t>(Action::End));
    return out;
}

} // namespace HookCrashers::SWF
