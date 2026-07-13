#pragma once

#include "SWFStructures.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace HookCrashers::SWF::ShapeInjection {

struct ImportedShape {
    std::string sourcePath;
    std::uint16_t tagCode = 0;
    std::uint16_t shapeId = 0;
    std::vector<std::uint8_t> payload;
};

class ShapeIdAllocator {
public:
    explicit ShapeIdAllocator(const SWFBufferView& view);

    std::uint16_t Allocate();

private:
    std::array<bool, 65536> m_UsedIds {};
    std::uint32_t m_NextId = 1;
};

bool LoadShapeFromSWF(const std::string& path, std::uint16_t shapeId, ImportedShape& out);
void AppendShapeDefinition(std::vector<std::uint8_t>& out, const ImportedShape& shape);
bool ReplacePlaceObject2CharacterAtDepth(std::vector<std::uint8_t>& frame,
                                         std::uint16_t depth,
                                         std::uint16_t shapeId,
                                         std::uint16_t* oldCharacterId = nullptr);

} // namespace HookCrashers::SWF::ShapeInjection
