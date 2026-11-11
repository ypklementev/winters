// Procedural snowflake sprite atlas: 8x8 grid of single-channel (coverage)
// sprites — irregular grains, stellar dendrites, plates and clumps — plus a
// mip chain. Pure C++ (no platform headers) so it can be tested and previewed
// anywhere.
#pragma once

#include <cstdint>
#include <vector>

namespace atlas {

struct Image {
    int width = 0;
    int height = 0;
    std::vector<std::uint8_t> pixels;  // R8, row-major
};

// cellSize: pixels per sprite (power of two). Layout matches sim::kSpriteRanges.
Image GenerateFlakeAtlas(int cellSize = 64, std::uint32_t seed = 0x5A0F1A4E);

// Box-filtered mip chain, level 0 first. Stops when a sprite cell is
// `minCellSize` px wide (lower levels would bleed between sprites).
std::vector<Image> BuildMips(const Image& base, int cellSize, int minCellSize = 4);

}  // namespace atlas
