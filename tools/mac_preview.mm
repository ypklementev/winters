// Offscreen preview of the macOS renderer: simulates a desktop with a few
// windows for a while, renders one frame through MetalRenderer and writes it
// over a painted "desktop" to a PNG. Checks the Metal shader port visually
// without running the overlay.
//
//   winters_preview [out.png] [seconds] [unitsPerPixel]
//   unitsPerPixel: 1 = Retina (default), 2 = a 1x display next to a Retina one.
#import <CoreGraphics/CoreGraphics.h>
#import <Foundation/Foundation.h>
#import <ImageIO/ImageIO.h>
#import <Metal/Metal.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "mac/metal_renderer.h"
#include "sim/settings_model.h"
#include "sim/world.h"

namespace {

constexpr float kScale = 2.0f;  // simulation units per point (Retina)
constexpr float kWidthPt = 1280.0f, kHeightPt = 800.0f;

struct Win {
    sim::RectF rect;
    unsigned color;
};

void FillRect(std::vector<std::uint32_t>& px, int w, int h, sim::RectF r, float upp, unsigned rgb) {
    const int x0 = std::max(0, int(r.left / upp)), x1 = std::min(w, int(r.right / upp));
    const int y0 = std::max(0, int(r.top / upp)), y1 = std::min(h, int(r.bottom / upp));
    for (int y = y0; y < y1; ++y)
        for (int x = x0; x < x1; ++x) px[size_t(y) * w + x] = rgb;
}

}  // namespace

int main(int argc, const char** argv) {
    @autoreleasepool {
        const std::string out = argc > 1 ? argv[1] : "winters_preview.png";
        const float seconds = argc > 2 ? std::atof(argv[2]) : 60.0f;
        const float upp = argc > 3 ? std::atof(argv[3]) : 1.0f;

        sim::World world(7);
        sim::UserSettings us;
        us.intensity = sim::Intensity::Blizzard;
        us.wiper = true;
        world.setConfig(sim::toSnowConfig(us));
        sim::Monitor m;
        m.bounds = {0, 0, kWidthPt * kScale, kHeightPt * kScale};
        m.work = {0, 25 * kScale, kWidthPt * kScale, (kHeightPt - 70) * kScale};
        m.scale = kScale;
        world.setMonitors({m});

        const std::vector<Win> wins = {
            {{180 * kScale, 160 * kScale, 760 * kScale, 560 * kScale}, 0xFF2B2F38},
            {{620 * kScale, 300 * kScale, 1150 * kScale, 650 * kScale}, 0xFFF2F2F2},
            {{0, 0, kWidthPt * kScale, 27 * kScale}, 0xFF3A4150},                                    // menu bar
            {{0, (kHeightPt - 70) * kScale, kWidthPt * kScale, kHeightPt * kScale}, 0xFF505868},  // Dock
        };
        std::vector<sim::WindowInfo> infos;
        infos.push_back({1, wins[2].rect, false});
        infos.push_back({2, wins[3].rect, true});
        infos.push_back({3, wins[0].rect, false});  // the dark window is in front
        infos.push_back({4, wins[1].rect, false});
        world.setWindows(infos);
        world.placeWiper({1000 * kScale, (kHeightPt - 70 - 16) * kScale});
        world.setWiperEnabled(true);
        world.prewarm();

        const int steps = int(seconds / sim::kFixedDt);
        for (int i = 0; i < steps; ++i) {
            // Last two seconds: roll a snowball along the dark window's pile.
            const float t = float(steps - i) * sim::kFixedDt;
            if (t < 2.0f) {
                sim::PointerInput p;
                p.valid = true;
                p.sculpt = true;
                p.pos = {(200 + (2.0f - t) * 150) * kScale, 152 * kScale};
                p.vel = {150 * kScale, 0};
                world.setPointer(p);
            }
            world.step(sim::kFixedDt);
        }
        std::printf("flakes %zu, piles %.0f px^2, balls %zu\n", world.flakes().size(), world.lyingArea(),
                    world.snowballs().size());

        id<MTLDevice> device = MTLCreateSystemDefaultDevice();
        MetalRenderer r;
        std::string error;
        if (!r.init(device, &error)) {
            std::fprintf(stderr, "renderer: %s\n", error.c_str());
            return 1;
        }
        const int w = int(kWidthPt * kScale / upp), h = int(kHeightPt * kScale / upp);
        MTLTextureDescriptor* td = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MetalRenderer::kPixelFormat
                                                                                      width:w
                                                                                     height:h
                                                                                  mipmapped:NO];
        td.usage = MTLTextureUsageRenderTarget;
        td.storageMode = MTLStorageModeManaged;
        id<MTLTexture> target = [device newTextureWithDescriptor:td];

        id<MTLCommandBuffer> cb = r.beginFrame(world, 1.0f);
        MetalViewport vp{0, 0, kWidthPt * kScale, kHeightPt * kScale, upp};
        r.encode(cb, target, vp);
        id<MTLBlitCommandEncoder> blit = [cb blitCommandEncoder];
        [blit synchronizeResource:target];
        [blit endEncoding];
        [cb commit];
        [cb waitUntilCompleted];

        std::vector<std::uint32_t> snow(size_t(w) * h);
        [target getBytes:snow.data() bytesPerRow:w * 4 fromRegion:MTLRegionMake2D(0, 0, w, h) mipmapLevel:0];

        // Desktop: blue gradient wallpaper plus the windows, back to front.
        std::vector<std::uint32_t> px(size_t(w) * h);
        for (int y = 0; y < h; ++y) {
            const float k = float(y) / h;
            const unsigned rr = unsigned(40 + 30 * k), gg = unsigned(70 + 40 * k), bb = unsigned(120 + 50 * k);
            for (int x = 0; x < w; ++x) px[size_t(y) * w + x] = 0xFF000000u | rr << 16 | gg << 8 | bb;
        }
        for (int i : {1, 0, 3, 2}) FillRect(px, w, h, wins[i].rect, upp, wins[i].color);

        // Premultiplied BGRA "over" the desktop -> RGBA for the PNG.
        std::vector<std::uint8_t> rgba(size_t(w) * h * 4);
        for (size_t i = 0; i < px.size(); ++i) {
            const std::uint32_t s = snow[i], d = px[i];
            const float a = float(s >> 24) / 255.0f;
            const float sb = float(s & 0xFF), sg = float((s >> 8) & 0xFF), sr = float((s >> 16) & 0xFF);
            const float db = float(d & 0xFF), dg = float((d >> 8) & 0xFF), dr = float((d >> 16) & 0xFF);
            rgba[i * 4 + 0] = std::uint8_t(std::lround(std::min(255.0f, sr + dr * (1 - a))));
            rgba[i * 4 + 1] = std::uint8_t(std::lround(std::min(255.0f, sg + dg * (1 - a))));
            rgba[i * 4 + 2] = std::uint8_t(std::lround(std::min(255.0f, sb + db * (1 - a))));
            rgba[i * 4 + 3] = 255;
        }

        CGColorSpaceRef cs = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
        CGContextRef ctx = CGBitmapContextCreate(rgba.data(), w, h, 8, w * 4, cs,
                                                 static_cast<CGBitmapInfo>(kCGImageAlphaNoneSkipLast));
        CGImageRef img = CGBitmapContextCreateImage(ctx);
        NSURL* url = [NSURL fileURLWithPath:@(out.c_str())];
        CGImageDestinationRef dest = CGImageDestinationCreateWithURL((__bridge CFURLRef)url, CFSTR("public.png"), 1, nullptr);
        CGImageDestinationAddImage(dest, img, nullptr);
        const bool ok = CGImageDestinationFinalize(dest);
        CFRelease(dest);
        CGImageRelease(img);
        CGContextRelease(ctx);
        CGColorSpaceRelease(cs);
        std::printf("%s %s (%dx%d)\n", ok ? "wrote" : "FAILED to write", out.c_str(), w, h);
        return ok ? 0 : 1;
    }
}
