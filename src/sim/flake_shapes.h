// Flake sprite categories shared by the simulation (which picks a shape per
// particle) and the renderer's procedural sprite atlas.
#pragma once

#include <cstdint>

namespace sim {

enum class FlakeShape : std::uint8_t {
    Speck = 0,    // tiny irregular grain: the bulk of real falling snow
    Crystal = 1,  // stellar dendrite
    Plate = 2,    // hexagonal / sectored plate
    Clump = 3,    // several crystals stuck together
};

inline constexpr int kAtlasGrid = 8;  // 8 x 8 sprites
inline constexpr int kSpriteCount = kAtlasGrid * kAtlasGrid;

struct SpriteRange {
    int first;
    int count;
};

inline constexpr SpriteRange kSpriteRanges[4] = {
    {0, 16},   // Speck
    {16, 24},  // Crystal
    {40, 12},  // Plate
    {52, 12},  // Clump
};

inline int spriteIndex(FlakeShape shape, std::uint32_t variant) {
    const SpriteRange r = kSpriteRanges[static_cast<int>(shape)];
    return r.first + static_cast<int>(variant % static_cast<std::uint32_t>(r.count));
}

inline FlakeShape shapeOfSprite(int sprite) {
    for (int s = 3; s >= 0; --s) {
        if (sprite >= kSpriteRanges[s].first) return static_cast<FlakeShape>(s);
    }
    return FlakeShape::Speck;
}

}  // namespace sim
