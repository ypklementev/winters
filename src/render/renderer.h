// Draws simulation state into the overlay's composition swap chain:
// one instanced draw call each for piles, snowballs and flakes.
#pragma once

#include <windows.h>

#include <d3d11.h>

#include <vector>

#include "render/com_ptr.h"
#include "render/device.h"
#include "render/instances.h"

namespace sim {
class World;
}

class Renderer {
public:
    enum class Result { Ok, DeviceLost, Failed };

    HRESULT init(HWND hwnd, UINT width, UINT height);
    void shutdown();

    HANDLE frameWaitable() const { return gpu_.frameWaitable(); }
    bool ready() const { return gpu_.device() != nullptr && static_cast<bool>(flakePs_); }

    Result render(const sim::World& world);

private:
    struct DynamicBuffer {
        ComPtr<ID3D11Buffer> buffer;
        size_t capacityBytes = 0;
    };

    HRESULT createPipeline();
    HRESULT createAtlas();
    // Grows (if needed) and fills a dynamic vertex buffer.
    HRESULT upload(DynamicBuffer& b, const void* data, size_t bytes);

    GpuContext gpu_;
    ComPtr<ID3D11VertexShader> flakeVs_, ballVs_, pileVs_, shapeVs_;
    ComPtr<ID3D11PixelShader> flakePs_, ballPs_, pilePs_, shapePs_;
    ComPtr<ID3D11InputLayout> flakeLayout_, ballLayout_, pileLayout_, shapeLayout_;
    ComPtr<ID3D11ShaderResourceView> atlas_;
    ComPtr<ID3D11SamplerState> sampler_;
    ComPtr<ID3D11Buffer> constants_;
    ComPtr<ID3D11BlendState> blend_;
    ComPtr<ID3D11RasterizerState> raster_;
    DynamicBuffer flakeBuf_, ballBuf_, pileBuf_, shapeBuf_;

    render::FrameInstances frame_;
    LARGE_INTEGER startTime_{};
    LARGE_INTEGER qpcFreq_{};
};
