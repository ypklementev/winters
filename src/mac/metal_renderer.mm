#include "mac/metal_renderer.h"

#include <algorithm>
#include <cstring>
#include <vector>

#include "mac/metal_shaders.h"
#include "render/flake_atlas.h"

namespace {

// Must match `struct Frame` in metal_shaders.h.
struct FrameConstants {
    float viewOrigin[2];
    float viewSize[2];
    float time;
    float aa;
    float padding[2];
};

constexpr int kAtlasCell = 64;

const std::vector<atlas::Image>& AtlasMips() {
    static const std::vector<atlas::Image> mips = atlas::BuildMips(atlas::GenerateFlakeAtlas(kAtlasCell), kAtlasCell);
    return mips;
}

NSUInteger AlignUp(NSUInteger v) { return (v + 255) & ~static_cast<NSUInteger>(255); }

}  // namespace

bool MetalRenderer::init(id<MTLDevice> device, std::string* error) {
    device_ = device;
    if (!device_) {
        if (error) *error = "Metal is not available on this Mac";
        return false;
    }
    queue_ = [device_ newCommandQueue];
    inFlight_ = dispatch_semaphore_create(kFramesInFlight);

    NSError* err = nil;
    MTLCompileOptions* opts = [MTLCompileOptions new];
    opts.fastMathEnabled = YES;
    id<MTLLibrary> lib = [device_ newLibraryWithSource:@(metal_shaders::kSource) options:opts error:&err];
    if (!lib) {
        if (error) *error = std::string("shader compilation failed: ") + err.localizedDescription.UTF8String;
        return false;
    }
    pilePipe_ = makePipeline(lib, @"vs_pile", @"ps_pile", error);
    ballPipe_ = makePipeline(lib, @"vs_ball", @"ps_ball", error);
    shapePipe_ = makePipeline(lib, @"vs_shape", @"ps_shape", error);
    flakePipe_ = makePipeline(lib, @"vs_flake", @"ps_flake", error);
    if (!pilePipe_ || !ballPipe_ || !shapePipe_ || !flakePipe_ || !createAtlas()) {
        flakePipe_ = nil;
        if (error && error->empty()) *error = "could not create the flake atlas";
        return false;
    }
    return true;
}

id<MTLRenderPipelineState> MetalRenderer::makePipeline(id<MTLLibrary> lib, NSString* vs, NSString* fs,
                                                       std::string* error) {
    MTLRenderPipelineDescriptor* d = [MTLRenderPipelineDescriptor new];
    d.vertexFunction = [lib newFunctionWithName:vs];
    d.fragmentFunction = [lib newFunctionWithName:fs];
    MTLRenderPipelineColorAttachmentDescriptor* ca = d.colorAttachments[0];
    ca.pixelFormat = kPixelFormat;
    // Premultiplied-alpha "over", as on Windows.
    ca.blendingEnabled = YES;
    ca.rgbBlendOperation = MTLBlendOperationAdd;
    ca.alphaBlendOperation = MTLBlendOperationAdd;
    ca.sourceRGBBlendFactor = MTLBlendFactorOne;
    ca.sourceAlphaBlendFactor = MTLBlendFactorOne;
    ca.destinationRGBBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
    ca.destinationAlphaBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
    NSError* err = nil;
    id<MTLRenderPipelineState> p = [device_ newRenderPipelineStateWithDescriptor:d error:&err];
    if (!p && error) *error = std::string("pipeline ") + vs.UTF8String + ": " + err.localizedDescription.UTF8String;
    return p;
}

bool MetalRenderer::createAtlas() {
    const std::vector<atlas::Image>& mips = AtlasMips();
    MTLTextureDescriptor* td = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatR8Unorm
                                                                                  width:mips[0].width
                                                                                 height:mips[0].height
                                                                              mipmapped:YES];
    td.mipmapLevelCount = mips.size();
    td.usage = MTLTextureUsageShaderRead;
    atlas_ = [device_ newTextureWithDescriptor:td];
    if (!atlas_) return false;
    for (size_t i = 0; i < mips.size(); ++i) {
        const atlas::Image& m = mips[i];
        [atlas_ replaceRegion:MTLRegionMake2D(0, 0, m.width, m.height)
                  mipmapLevel:i
                    withBytes:m.pixels.data()
                  bytesPerRow:m.width];
    }
    return true;
}

id<MTLCommandBuffer> MetalRenderer::beginFrame(const sim::World& world, float timeSeconds) {
    if (!ready()) return nil;
    frame_.build(world);
    time_ = timeSeconds;

    offPiles_ = 0;
    offBalls_ = AlignUp(offPiles_ + frame_.piles.size() * sizeof(render::PileInstance));
    offShapes_ = AlignUp(offBalls_ + frame_.balls.size() * sizeof(render::BallInstance));
    offFlakes_ = AlignUp(offShapes_ + frame_.shapes.size() * sizeof(render::ShapeInstance));
    const NSUInteger total = std::max<NSUInteger>(offFlakes_ + frame_.flakes.size() * sizeof(render::FlakeInstance), 256);

    // Wait until the GPU is done with the buffer we are about to overwrite.
    dispatch_semaphore_wait(inFlight_, DISPATCH_TIME_FOREVER);
    slot_ = (slot_ + 1) % kFramesInFlight;
    __strong id<MTLBuffer>& buf = buffers_[slot_];
    if (!buf || buf.length < total) {
        NSUInteger cap = buf ? buf.length : 256 * 1024;
        while (cap < total) cap *= 2;
        buf = [device_ newBufferWithLength:cap options:MTLResourceStorageModeShared];
        if (!buf) {
            dispatch_semaphore_signal(inFlight_);
            return nil;
        }
    }
    auto* base = static_cast<char*>(buf.contents);
    auto copy = [&](NSUInteger off, const auto& v) {
        if (!v.empty()) std::memcpy(base + off, v.data(), v.size() * sizeof(v[0]));
    };
    copy(offPiles_, frame_.piles);
    copy(offBalls_, frame_.balls);
    copy(offShapes_, frame_.shapes);
    copy(offFlakes_, frame_.flakes);

    id<MTLCommandBuffer> cb = [queue_ commandBuffer];
    dispatch_semaphore_t sem = inFlight_;
    [cb addCompletedHandler:^(id<MTLCommandBuffer>) {
      dispatch_semaphore_signal(sem);
    }];
    return cb;
}

void MetalRenderer::encode(id<MTLCommandBuffer> cb, id<MTLTexture> target, const MetalViewport& view) {
    MTLRenderPassDescriptor* rp = [MTLRenderPassDescriptor renderPassDescriptor];
    rp.colorAttachments[0].texture = target;
    rp.colorAttachments[0].loadAction = MTLLoadActionClear;
    rp.colorAttachments[0].storeAction = MTLStoreActionStore;
    rp.colorAttachments[0].clearColor = MTLClearColorMake(0, 0, 0, 0);
    id<MTLRenderCommandEncoder> enc = [cb renderCommandEncoderWithDescriptor:rp];

    const FrameConstants fc{{view.originX, view.originY}, {view.width, view.height}, time_, view.unitsPerPixel, {0, 0}};
    [enc setVertexBytes:&fc length:sizeof(fc) atIndex:1];
    [enc setFragmentBytes:&fc length:sizeof(fc) atIndex:1];
    [enc setFragmentTexture:atlas_ atIndex:0];

    id<MTLBuffer> buf = buffers_[slot_];
    auto draw = [&](id<MTLRenderPipelineState> pipe, NSUInteger offset, size_t count) {
        if (count == 0) return;
        [enc setRenderPipelineState:pipe];
        [enc setVertexBuffer:buf offset:offset atIndex:0];
        [enc drawPrimitives:MTLPrimitiveTypeTriangleStrip vertexStart:0 vertexCount:4 instanceCount:count];
    };
    draw(pilePipe_, offPiles_, frame_.piles.size());
    draw(ballPipe_, offBalls_, frame_.balls.size());
    draw(shapePipe_, offShapes_, frame_.shapes.size());
    draw(flakePipe_, offFlakes_, frame_.flakes.size());
    [enc endEncoding];
}
