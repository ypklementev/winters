// 1-D height map of snow lying along a horizontal edge (window top, taskbar,
// screen bottom). Cell i covers local x in [i*cellWidth, (i+1)*cellWidth).
// Cells can be masked out (occluded / off-screen); masked cells never hold snow.
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace sim {

class Heightmap {
public:
    struct Spill {
        float localX;  // where the snow went over the edge
        float area;    // px^2
    };

    Heightmap() = default;
    Heightmap(std::size_t cells, float cellWidth);

    std::size_t size() const { return h_.size(); }
    float cellWidth() const { return cellW_; }

    float height(std::size_t i) const { return h_[i]; }
    void setHeight(std::size_t i, float v) { h_[i] = v; }
    bool visible(std::size_t i) const { return vis_[i] != 0; }
    void setVisible(std::size_t i, bool v) { vis_[i] = v ? 1 : 0; }
    const std::vector<float>& heights() const { return h_; }

    // Cell index for a local x, or -1 if outside.
    int cellAt(float localX) const;
    // Snow height at local x; 0 for masked/outside cells.
    float heightAt(float localX) const;

    // Adds `area` px^2 of snow around localX (triangle kernel of half-width
    // `spread`), only into visible cells, never above maxHeight.
    // Returns the area actually deposited.
    float deposit(float localX, float spread, float area, float maxHeight);

    // Lowers cell i to at most newHeight; returns removed area (px^2).
    float lowerTo(std::size_t i, float newHeight);

    float totalArea() const;
    void clear();

    // Angle-of-repose relaxation: neighbouring cells may differ by at most
    // maxSlope * cellWidth. Snow pushed over an edge (array end or masked
    // neighbour) is removed and reported in `spills`. Returns true if
    // anything moved.
    bool relax(float maxSlope, int iterations, std::vector<Spill>& spills);

    // Changes the number of cells (window resized), preserving the snow
    // profile shape and total area. Visibility of new cells defaults to true.
    void resample(std::size_t newCells);

private:
    float cellW_ = 2.0f;
    std::vector<float> h_;
    std::vector<std::uint8_t> vis_;
};

}  // namespace sim
