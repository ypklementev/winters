#include "render/renderer.h"

#include <d3dcompiler.h>

#include <algorithm>
#include <cstring>

#include "render/shaders.h"
#include "sim/world.h"

namespace {

// d3dcompiler_47.dll is part of Windows 10+; load it lazily so neither
// toolchain needs an import library for it.
pD3DCompile loadD3DCompile() {
    static pD3DCompile fn = [] {
        HMODULE mod = LoadLibraryExW(L"d3dcompiler_47.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
        return mod ? reinterpret_cast<pD3DCompile>(
                         reinterpret_cast<void*>(GetProcAddress(mod, "D3DCompile")))
                   : nullptr;
    }();
    return fn;
}

HRESULT compile(const char* entry, const char* target, ComPtr<ID3DBlob>& out) {
    pD3DCompile d3dCompile = loadD3DCompile();
    if (!d3dCompile) return HRESULT_FROM_WIN32(ERROR_MOD_NOT_FOUND);
    ComPtr<ID3DBlob> errors;
    HRESULT hr = d3dCompile(shaders::kSource, sizeof(shaders::kSource) - 1, "winflake", nullptr, nullptr,
                            entry, target, D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, out.put(), errors.put());
    if (FAILED(hr) && errors) {
        OutputDebugStringA(static_cast<const char*>(errors->GetBufferPointer()));
    }
    return hr;
}

struct FrameConstants {
    float viewSize[2];
    float padding[2];
};

}  // namespace

HRESULT Renderer::init(HWND hwnd, UINT width, UINT height) {
    shutdown();
    HRESULT hr = gpu_.init(hwnd, width, height);
    if (FAILED(hr)) return hr;
    hr = createPipeline();
    if (FAILED(hr)) shutdown();
    return hr;
}

void Renderer::shutdown() {
    flakeBuf_ = {};
    ballBuf_ = {};
    pileBuf_ = {};
    constants_.reset();
    discLayout_.reset();
    pileLayout_.reset();
    discVs_.reset();
    pileVs_.reset();
    flakePs_.reset();
    ballPs_.reset();
    pilePs_.reset();
    blend_.reset();
    raster_.reset();
    gpu_.shutdown();
}

HRESULT Renderer::createPipeline() {
    ID3D11Device* dev = gpu_.device();
    // Shader model 4.0 also runs on feature level 10_x hardware.
    ComPtr<ID3DBlob> discVs, pileVs, flakePs, ballPs, pilePs;
    HRESULT hr;
    if (FAILED(hr = compile("vs_disc", "vs_4_0", discVs))) return hr;
    if (FAILED(hr = compile("vs_pile", "vs_4_0", pileVs))) return hr;
    if (FAILED(hr = compile("ps_flake", "ps_4_0", flakePs))) return hr;
    if (FAILED(hr = compile("ps_ball", "ps_4_0", ballPs))) return hr;
    if (FAILED(hr = compile("ps_pile", "ps_4_0", pilePs))) return hr;

    auto vs = [&](ComPtr<ID3DBlob>& b, ComPtr<ID3D11VertexShader>& out) {
        return dev->CreateVertexShader(b->GetBufferPointer(), b->GetBufferSize(), nullptr, out.put());
    };
    auto ps = [&](ComPtr<ID3DBlob>& b, ComPtr<ID3D11PixelShader>& out) {
        return dev->CreatePixelShader(b->GetBufferPointer(), b->GetBufferSize(), nullptr, out.put());
    };
    if (FAILED(hr = vs(discVs, discVs_))) return hr;
    if (FAILED(hr = vs(pileVs, pileVs_))) return hr;
    if (FAILED(hr = ps(flakePs, flakePs_))) return hr;
    if (FAILED(hr = ps(ballPs, ballPs_))) return hr;
    if (FAILED(hr = ps(pilePs, pilePs_))) return hr;

    const D3D11_INPUT_ELEMENT_DESC discElems[] = {
        {"INSTANCE", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 0, D3D11_INPUT_PER_INSTANCE_DATA, 1},
    };
    hr = dev->CreateInputLayout(discElems, ARRAYSIZE(discElems), discVs->GetBufferPointer(),
                                discVs->GetBufferSize(), discLayout_.put());
    if (FAILED(hr)) return hr;
    const D3D11_INPUT_ELEMENT_DESC pileElems[] = {
        {"PILE", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 0, D3D11_INPUT_PER_INSTANCE_DATA, 1},
        {"PILE", 1, DXGI_FORMAT_R32G32_FLOAT, 0, 16, D3D11_INPUT_PER_INSTANCE_DATA, 1},
    };
    hr = dev->CreateInputLayout(pileElems, ARRAYSIZE(pileElems), pileVs->GetBufferPointer(),
                                pileVs->GetBufferSize(), pileLayout_.put());
    if (FAILED(hr)) return hr;

    D3D11_BUFFER_DESC cb{};
    cb.ByteWidth = sizeof(FrameConstants);
    cb.Usage = D3D11_USAGE_DEFAULT;
    cb.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    if (FAILED(hr = dev->CreateBuffer(&cb, nullptr, constants_.put()))) return hr;

    // Premultiplied-alpha "over".
    D3D11_BLEND_DESC bd{};
    auto& rt = bd.RenderTarget[0];
    rt.BlendEnable = TRUE;
    rt.SrcBlend = D3D11_BLEND_ONE;
    rt.DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
    rt.BlendOp = D3D11_BLEND_OP_ADD;
    rt.SrcBlendAlpha = D3D11_BLEND_ONE;
    rt.DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
    rt.BlendOpAlpha = D3D11_BLEND_OP_ADD;
    rt.RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    if (FAILED(hr = dev->CreateBlendState(&bd, blend_.put()))) return hr;

    D3D11_RASTERIZER_DESC rd{};
    rd.FillMode = D3D11_FILL_SOLID;
    rd.CullMode = D3D11_CULL_NONE;
    rd.DepthClipEnable = TRUE;
    return dev->CreateRasterizerState(&rd, raster_.put());
}

HRESULT Renderer::upload(DynamicBuffer& b, const void* data, size_t bytes) {
    if (bytes == 0) return S_OK;
    if (bytes > b.capacityBytes || !b.buffer) {
        size_t cap = std::max<size_t>(b.capacityBytes, 64 * 1024);
        while (cap < bytes) cap *= 2;
        D3D11_BUFFER_DESC bd{};
        bd.ByteWidth = static_cast<UINT>(cap);
        bd.Usage = D3D11_USAGE_DYNAMIC;
        bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
        bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        b.buffer.reset();
        HRESULT hr = gpu_.device()->CreateBuffer(&bd, nullptr, b.buffer.put());
        if (FAILED(hr)) {
            b.capacityBytes = 0;
            return hr;
        }
        b.capacityBytes = cap;
    }
    D3D11_MAPPED_SUBRESOURCE mapped{};
    HRESULT hr = gpu_.context()->Map(b.buffer.get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
    if (FAILED(hr)) return hr;
    std::memcpy(mapped.pData, data, bytes);
    gpu_.context()->Unmap(b.buffer.get(), 0);
    return S_OK;
}

void Renderer::buildInstances(const sim::World& world) {
    const sim::FlakePool& f = world.flakes();
    flakes_.resize(f.size());
    for (size_t i = 0; i < f.size(); ++i) flakes_[i] = {f.x[i], f.y[i], f.radius[i], f.alpha[i]};

    balls_.clear();
    for (const sim::Snowball& b : world.snowballs()) balls_.push_back({b.pos.x, b.pos.y, b.radius, 1.0f});

    // Smooth outline: corner heights are averages of neighbouring cells;
    // ends next to a hidden/absent cell are rounded down.
    piles_.clear();
    constexpr float kMin = 0.05f;
    for (const sim::Surface& s : world.surfaces()) {
        const sim::Heightmap& hm = s.pile;
        const size_t n = hm.size();
        const float cw = hm.cellWidth();
        const auto& h = hm.heights();
        for (size_t i = 0; i < n; ++i) {
            if (!hm.visible(i)) continue;
            const bool leftOpen = i == 0 || !hm.visible(i - 1);
            const bool rightOpen = i + 1 >= n || !hm.visible(i + 1);
            const float hl = leftOpen ? h[i] * 0.5f : (h[i - 1] + h[i]) * 0.5f;
            const float hr = rightOpen ? h[i] * 0.5f : (h[i] + h[i + 1]) * 0.5f;
            if (h[i] < kMin && hl < kMin && hr < kMin) continue;
            const float x0 = s.rect.left + static_cast<float>(i) * cw;
            piles_.push_back({x0, x0 + cw, s.edgeY(), 0.0f, hl, hr});
        }
    }
}

Renderer::Result Renderer::render(const sim::World& world) {
    if (!ready()) return Result::Failed;
    ID3D11DeviceContext* ctx = gpu_.context();
    buildInstances(world);

    HRESULT hr = S_OK;
    auto fail = [&](HRESULT e) { return gpu_.isDeviceLost(e) ? Result::DeviceLost : Result::Failed; };
    if (FAILED(hr = upload(pileBuf_, piles_.data(), piles_.size() * sizeof(PileInstance)))) return fail(hr);
    if (FAILED(hr = upload(ballBuf_, balls_.data(), balls_.size() * sizeof(DiscInstance)))) return fail(hr);
    if (FAILED(hr = upload(flakeBuf_, flakes_.data(), flakes_.size() * sizeof(DiscInstance)))) return fail(hr);

    const FrameConstants fc{{static_cast<float>(gpu_.width()), static_cast<float>(gpu_.height())}, {0, 0}};
    ctx->UpdateSubresource(constants_.get(), 0, nullptr, &fc, 0, 0);

    ID3D11RenderTargetView* rtv = gpu_.backBufferRtv();
    const float clear[4] = {0, 0, 0, 0};
    ctx->OMSetRenderTargets(1, &rtv, nullptr);  // flip model unbinds it after Present
    ctx->ClearRenderTargetView(rtv, clear);

    D3D11_VIEWPORT vp{};
    vp.Width = static_cast<float>(gpu_.width());
    vp.Height = static_cast<float>(gpu_.height());
    vp.MaxDepth = 1.0f;
    ctx->RSSetViewports(1, &vp);
    ctx->RSSetState(raster_.get());
    ctx->OMSetBlendState(blend_.get(), nullptr, 0xFFFFFFFF);
    ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
    ID3D11Buffer* cbs = constants_.get();
    ctx->VSSetConstantBuffers(0, 1, &cbs);

    auto draw = [&](DynamicBuffer& b, UINT stride, size_t count, ID3D11InputLayout* layout,
                    ID3D11VertexShader* v, ID3D11PixelShader* p) {
        if (count == 0) return;
        ID3D11Buffer* vb = b.buffer.get();
        const UINT offset = 0;
        ctx->IASetInputLayout(layout);
        ctx->IASetVertexBuffers(0, 1, &vb, &stride, &offset);
        ctx->VSSetShader(v, nullptr, 0);
        ctx->PSSetShader(p, nullptr, 0);
        ctx->DrawInstanced(4, static_cast<UINT>(count), 0, 0);
    };
    draw(pileBuf_, sizeof(PileInstance), piles_.size(), pileLayout_.get(), pileVs_.get(), pilePs_.get());
    draw(ballBuf_, sizeof(DiscInstance), balls_.size(), discLayout_.get(), discVs_.get(), ballPs_.get());
    draw(flakeBuf_, sizeof(DiscInstance), flakes_.size(), discLayout_.get(), discVs_.get(), flakePs_.get());

    hr = gpu_.present();
    if (SUCCEEDED(hr)) return Result::Ok;
    return fail(hr);
}
