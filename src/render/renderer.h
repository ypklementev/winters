// Draws simulation state into the overlay's composition swap chain:
// one instanced draw call each for piles, snowballs and flakes.
#pragma once

#include <windows.h>

#include <d3d11.h>

#include <vector>

#include "render/com_ptr.h"
#include "render/device.h"

namespace sim {
class World;
}

struct DiscInstance {
    float x, y, radius, alpha;
};

struct PileInstance {
    float x0, x1, base, unused;
    float hLeft, hRight;
};

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
    // Grows (if needed) and fills a dynamic vertex buffer.
    HRESULT upload(DynamicBuffer& b, const void* data, size_t bytes);
    void buildInstances(const sim::World& world);

    GpuContext gpu_;
    ComPtr<ID3D11VertexShader> discVs_, pileVs_;
    ComPtr<ID3D11PixelShader> flakePs_, ballPs_, pilePs_;
    ComPtr<ID3D11InputLayout> discLayout_, pileLayout_;
    ComPtr<ID3D11Buffer> constants_;
    ComPtr<ID3D11BlendState> blend_;
    ComPtr<ID3D11RasterizerState> raster_;
    DynamicBuffer flakeBuf_, ballBuf_, pileBuf_;

    std::vector<DiscInstance> flakes_;
    std::vector<DiscInstance> balls_;
    std::vector<PileInstance> piles_;
};
