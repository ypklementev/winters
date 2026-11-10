#include "app/icon.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

namespace {

struct Seg {
    float ax, ay, bx, by;
};

float distToSeg(float px, float py, const Seg& s) {
    const float dx = s.bx - s.ax, dy = s.by - s.ay;
    const float len2 = dx * dx + dy * dy;
    float t = len2 > 0 ? ((px - s.ax) * dx + (py - s.ay) * dy) / len2 : 0.0f;
    t = std::clamp(t, 0.0f, 1.0f);
    const float ex = s.ax + t * dx - px, ey = s.ay + t * dy - py;
    return std::sqrt(ex * ex + ey * ey);
}

}  // namespace

HICON CreateSnowflakeIcon(int size) {
    size = std::max(size, 16);
    const float c = size * 0.5f;
    const float arm = size * 0.44f;
    const float half = std::max(0.9f, size / 18.0f);    // half stroke width
    const float outline = std::max(1.0f, size / 22.0f);  // outline thickness

    std::vector<Seg> segs;
    for (int k = 0; k < 6; ++k) {
        const float a = k * 3.14159265f / 3.0f + 3.14159265f / 2.0f;
        const float dx = std::cos(a), dy = std::sin(a);
        segs.push_back({c, c, c + dx * arm, c + dy * arm});
        const float bx = c + dx * arm * 0.55f, by = c + dy * arm * 0.55f;
        for (float side : {-1.0f, 1.0f}) {
            const float ba = a + side * 0.75f;
            segs.push_back({bx, by, bx + std::cos(ba) * arm * 0.32f, by + std::sin(ba) * arm * 0.32f});
        }
    }

    std::vector<std::uint32_t> pixels(static_cast<size_t>(size) * size);
    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            float d = 1e9f;
            for (const auto& s : segs) d = std::min(d, distToSeg(x + 0.5f, y + 0.5f, s));
            const float fill = std::clamp(half + 0.5f - d, 0.0f, 1.0f);
            const float edge = std::clamp(half + outline + 0.5f - d, 0.0f, 1.0f);
            // Outline colour #2F6FC4 blended towards white by fill coverage.
            const float r = 0x2F + (255 - 0x2F) * fill;
            const float g = 0x6F + (255 - 0x6F) * fill;
            const float b = 0xC4 + (255 - 0xC4) * fill;
            const auto A = static_cast<std::uint32_t>(edge * 255.0f + 0.5f);
            const auto R = static_cast<std::uint32_t>(r + 0.5f);
            const auto G = static_cast<std::uint32_t>(g + 0.5f);
            const auto B = static_cast<std::uint32_t>(b + 0.5f);
            pixels[static_cast<size_t>(y) * size + x] = (A << 24) | (R << 16) | (G << 8) | B;
        }
    }

    BITMAPV5HEADER bi{};
    bi.bV5Size = sizeof(bi);
    bi.bV5Width = size;
    bi.bV5Height = -size;  // top-down
    bi.bV5Planes = 1;
    bi.bV5BitCount = 32;
    bi.bV5Compression = BI_BITFIELDS;
    bi.bV5RedMask = 0x00FF0000;
    bi.bV5GreenMask = 0x0000FF00;
    bi.bV5BlueMask = 0x000000FF;
    bi.bV5AlphaMask = 0xFF000000;

    void* bits = nullptr;
    HDC screen = GetDC(nullptr);
    HBITMAP color = CreateDIBSection(screen, reinterpret_cast<BITMAPINFO*>(&bi), DIB_RGB_COLORS,
                                     &bits, nullptr, 0);
    ReleaseDC(nullptr, screen);
    if (!color) return nullptr;
    std::memcpy(bits, pixels.data(), pixels.size() * sizeof(std::uint32_t));

    std::vector<std::uint8_t> maskBits(static_cast<size_t>((size + 15) / 16 * 2) * size, 0);
    HBITMAP mask = CreateBitmap(size, size, 1, 1, maskBits.data());

    ICONINFO ii{};
    ii.fIcon = TRUE;
    ii.hbmColor = color;
    ii.hbmMask = mask;
    HICON icon = CreateIconIndirect(&ii);
    DeleteObject(color);
    DeleteObject(mask);
    return icon;
}
