// Metal counterpart of src/render/renderer.*: draws the simulation into any
// BGRA8 texture (an overlay window's drawable, or an offscreen one for the
// preview tool). Instance data is built once per frame and shared by all
// screens; each screen draws its own part of the simulation space.
#pragma once

#import <Metal/Metal.h>

#include <string>

#include "render/instances.h"

namespace sim {
class World;
}

// Part of the simulation space a render target shows.
struct MetalViewport {
    float originX = 0.0f, originY = 0.0f;  // simulation units, top-left
    float width = 0.0f, height = 0.0f;     // simulation units
    float unitsPerPixel = 1.0f;
};

class MetalRenderer {
public:
    static constexpr MTLPixelFormat kPixelFormat = MTLPixelFormatBGRA8Unorm;

    MetalRenderer() = default;
    MetalRenderer(const MetalRenderer&) = delete;
    MetalRenderer& operator=(const MetalRenderer&) = delete;

    // Compiles the shaders and uploads the flake atlas. On failure `error`
    // gets a human-readable reason.
    bool init(id<MTLDevice> device, std::string* error);
    bool ready() const { return flakePipe_ != nil; }
    id<MTLDevice> device() const { return device_; }

    // Builds and uploads this frame's instances, then returns a command
    // buffer to encode the targets into. May wait for a frame in flight to
    // finish (at most kFramesInFlight are queued). Returns nil on failure.
    // The caller must commit the returned buffer, even with nothing encoded.
    id<MTLCommandBuffer> beginFrame(const sim::World& world, float timeSeconds);
    // Clears `target` to transparent and draws the frame into it.
    void encode(id<MTLCommandBuffer> cb, id<MTLTexture> target, const MetalViewport& view);

private:
    static constexpr int kFramesInFlight = 3;

    id<MTLRenderPipelineState> makePipeline(id<MTLLibrary> lib, NSString* vs, NSString* fs, std::string* error);
    bool createAtlas();

    id<MTLDevice> device_ = nil;
    id<MTLCommandQueue> queue_ = nil;
    id<MTLRenderPipelineState> flakePipe_ = nil, ballPipe_ = nil, pilePipe_ = nil, shapePipe_ = nil;
    id<MTLTexture> atlas_ = nil;
    dispatch_semaphore_t inFlight_ = nil;

    // One buffer per frame in flight; holds all four instance arrays.
    id<MTLBuffer> buffers_[kFramesInFlight] = {};
    int slot_ = 0;
    NSUInteger offPiles_ = 0, offBalls_ = 0, offShapes_ = 0, offFlakes_ = 0;

    render::FrameInstances frame_;
    float time_ = 0.0f;
};
