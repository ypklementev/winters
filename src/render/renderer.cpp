#include "render/renderer.h"

#include <d3dcompiler.h>

#include <algorithm>
#include <cmath>
#include <cstring>

#include "render/flake_atlas.h"
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
    HRESULT hr = d3dCompile(shaders::kSource, sizeof(shaders::kSource) - 1, "winters", nullptr, nullptr,
                            entry, target, D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, out.put(), errors.put());
    if (FAILED(hr) && errors) {
        OutputDebugStringA(static_cast<const char*>(errors->GetBufferPointer()));
    }
    return hr;
}

struct FrameConstants {
    float viewSize[2];
    float time;
    float padding;
};

constexpr int kAtlasCell = 64;

// Generated once per process; device recreation reuses it.
const std::vector<atlas::Image>& AtlasMips() {
    static const std::vector<atlas::Image> mips = atlas::BuildMips(atlas::GenerateFlakeAtlas(kAtlasCell), kAtlasCell);
    return mips;
}

}  // namespace

HRESULT Renderer::init(HWND hwnd, UINT width, UINT height) {
    shutdown();
    QueryPerformanceFrequency(&qpcFreq_);
    QueryPerformanceCounter(&startTime_);
    HRESULT hr = gpu_.init(hwnd, width, height);
    if (FAILED(hr)) return hr;
    hr = createPipeline();
    if (SUCCEEDED(hr)) hr = createAtlas();
    if (FAILED(hr)) shutdown();
    return hr;
}

void Renderer::shutdown() {
    flakeBuf_ = {};
    ballBuf_ = {};
    pileBuf_ = {};
    shapeBuf_ = {};
    shapeLayout_.reset();
    shapeVs_.reset();
    shapePs_.reset();
    constants_.reset();
    flakeLayout_.reset();
    ballLayout_.reset();
    pileLayout_.reset();
    flakeVs_.reset();
    ballVs_.reset();
    pileVs_.reset();
    atlas_.reset();
    sampler_.reset();
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
    ComPtr<ID3DBlob> flakeVs, ballVs, pileVs, shapeVs, flakePs, ballPs, pilePs, shapePs;
    HRESULT hr;
    if (FAILED(hr = compile("vs_flake", "vs_4_0", flakeVs))) return hr;
    if (FAILED(hr = compile("vs_ball", "vs_4_0", ballVs))) return hr;
    if (FAILED(hr = compile("vs_pile", "vs_4_0", pileVs))) return hr;
    if (FAILED(hr = compile("ps_flake", "ps_4_0", flakePs))) return hr;
    if (FAILED(hr = compile("ps_ball", "ps_4_0", ballPs))) return hr;
    if (FAILED(hr = compile("ps_pile", "ps_4_0", pilePs))) return hr;
    if (FAILED(hr = compile("vs_shape", "vs_4_0", shapeVs))) return hr;
    if (FAILED(hr = compile("ps_shape", "ps_4_0", shapePs))) return hr;

    auto vs = [&](ComPtr<ID3DBlob>& b, ComPtr<ID3D11VertexShader>& out) {
        return dev->CreateVertexShader(b->GetBufferPointer(), b->GetBufferSize(), nullptr, out.put());
    };
    auto ps = [&](ComPtr<ID3DBlob>& b, ComPtr<ID3D11PixelShader>& out) {
        return dev->CreatePixelShader(b->GetBufferPointer(), b->GetBufferSize(), nullptr, out.put());
    };
    if (FAILED(hr = vs(flakeVs, flakeVs_))) return hr;
    if (FAILED(hr = vs(ballVs, ballVs_))) return hr;
    if (FAILED(hr = vs(pileVs, pileVs_))) return hr;
    if (FAILED(hr = ps(flakePs, flakePs_))) return hr;
    if (FAILED(hr = ps(ballPs, ballPs_))) return hr;
    if (FAILED(hr = ps(pilePs, pilePs_))) return hr;
    if (FAILED(hr = vs(shapeVs, shapeVs_))) return hr;
    if (FAILED(hr = ps(shapePs, shapePs_))) return hr;
    const D3D11_INPUT_ELEMENT_DESC shapeElems[] = {
        {"SHAPE", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 0, D3D11_INPUT_PER_INSTANCE_DATA, 1},
        {"SHAPE", 1, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 16, D3D11_INPUT_PER_INSTANCE_DATA, 1},
        {"SHAPE", 2, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 32, D3D11_INPUT_PER_INSTANCE_DATA, 1},
    };
    hr = dev->CreateInputLayout(shapeElems, ARRAYSIZE(shapeElems), shapeVs->GetBufferPointer(),
                                shapeVs->GetBufferSize(), shapeLayout_.put());
    if (FAILED(hr)) return hr;

    const D3D11_INPUT_ELEMENT_DESC flakeElems[] = {
        {"FLAKE", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 0, D3D11_INPUT_PER_INSTANCE_DATA, 1},
        {"FLAKE", 1, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 16, D3D11_INPUT_PER_INSTANCE_DATA, 1},
    };
    hr = dev->CreateInputLayout(flakeElems, ARRAYSIZE(flakeElems), flakeVs->GetBufferPointer(),
                                flakeVs->GetBufferSize(), flakeLayout_.put());
    if (FAILED(hr)) return hr;
    const D3D11_INPUT_ELEMENT_DESC ballElems[] = {
        {"INSTANCE", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 0, D3D11_INPUT_PER_INSTANCE_DATA, 1},
    };
    hr = dev->CreateInputLayout(ballElems, ARRAYSIZE(ballElems), ballVs->GetBufferPointer(),
                                ballVs->GetBufferSize(), ballLayout_.put());
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

HRESULT Renderer::createAtlas() {
    const std::vector<atlas::Image>& mips = AtlasMips();
    D3D11_TEXTURE2D_DESC td{};
    td.Width = static_cast<UINT>(mips[0].width);
    td.Height = static_cast<UINT>(mips[0].height);
    td.MipLevels = static_cast<UINT>(mips.size());
    td.ArraySize = 1;
    td.Format = DXGI_FORMAT_R8_UNORM;
    td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_IMMUTABLE;
    td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    std::vector<D3D11_SUBRESOURCE_DATA> init(mips.size());
    for (size_t i = 0; i < mips.size(); ++i) {
        init[i].pSysMem = mips[i].pixels.data();
        init[i].SysMemPitch = static_cast<UINT>(mips[i].width);
    }
    ComPtr<ID3D11Texture2D> tex;
    HRESULT hr = gpu_.device()->CreateTexture2D(&td, init.data(), tex.put());
    if (FAILED(hr)) return hr;
    hr = gpu_.device()->CreateShaderResourceView(tex.get(), nullptr, atlas_.put());
    if (FAILED(hr)) return hr;

    D3D11_SAMPLER_DESC sd{};
    sd.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    sd.MaxLOD = static_cast<float>(mips.size() - 1);
    sd.ComparisonFunc = D3D11_COMPARISON_NEVER;
    return gpu_.device()->CreateSamplerState(&sd, sampler_.put());
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

Renderer::Result Renderer::render(const sim::World& world) {
    if (!ready()) return Result::Failed;
    ID3D11DeviceContext* ctx = gpu_.context();
    frame_.build(world);

    HRESULT hr = S_OK;
    auto fail = [&](HRESULT e) { return gpu_.isDeviceLost(e) ? Result::DeviceLost : Result::Failed; };
    if (FAILED(hr = upload(pileBuf_, frame_.piles.data(), frame_.piles.size() * sizeof(render::PileInstance)))) return fail(hr);
    if (FAILED(hr = upload(ballBuf_, frame_.balls.data(), frame_.balls.size() * sizeof(render::BallInstance)))) return fail(hr);
    if (FAILED(hr = upload(flakeBuf_, frame_.flakes.data(), frame_.flakes.size() * sizeof(render::FlakeInstance)))) return fail(hr);
    if (FAILED(hr = upload(shapeBuf_, frame_.shapes.data(), frame_.shapes.size() * sizeof(render::ShapeInstance)))) return fail(hr);

    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    const float seconds = static_cast<float>(static_cast<double>(now.QuadPart - startTime_.QuadPart) /
                                             static_cast<double>(qpcFreq_.QuadPart));
    const FrameConstants fc{{static_cast<float>(gpu_.width()), static_cast<float>(gpu_.height())},
                            std::fmod(seconds, 3600.0f), 0.0f};
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
    ctx->PSSetConstantBuffers(0, 1, &cbs);
    ID3D11ShaderResourceView* srv = atlas_.get();
    ID3D11SamplerState* smp = sampler_.get();
    ctx->PSSetShaderResources(0, 1, &srv);
    ctx->PSSetSamplers(0, 1, &smp);

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
    draw(pileBuf_, sizeof(render::PileInstance), frame_.piles.size(), pileLayout_.get(), pileVs_.get(), pilePs_.get());
    draw(ballBuf_, sizeof(render::BallInstance), frame_.balls.size(), ballLayout_.get(), ballVs_.get(), ballPs_.get());
    draw(shapeBuf_, sizeof(render::ShapeInstance), frame_.shapes.size(), shapeLayout_.get(), shapeVs_.get(), shapePs_.get());
    draw(flakeBuf_, sizeof(render::FlakeInstance), frame_.flakes.size(), flakeLayout_.get(), flakeVs_.get(), flakePs_.get());

    hr = gpu_.present();
    if (SUCCEEDED(hr)) return Result::Ok;
    return fail(hr);
}
