#include "render/device.h"

#include <algorithm>

HRESULT GpuContext::createDevice() {
    const D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0,
                                        D3D_FEATURE_LEVEL_10_1, D3D_FEATURE_LEVEL_10_0};
    UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;  // required for DirectComposition interop
#ifndef NDEBUG
    flags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

    auto tryCreate = [&](D3D_DRIVER_TYPE type, UINT f) {
        HRESULT hr = D3D11CreateDevice(nullptr, type, nullptr, f, levels, ARRAYSIZE(levels),
                                       D3D11_SDK_VERSION, device_.put(), nullptr, context_.put());
        // 11_1 isn't known to old runtimes: retry without it.
        if (hr == E_INVALIDARG) {
            hr = D3D11CreateDevice(nullptr, type, nullptr, f, levels + 1, ARRAYSIZE(levels) - 1,
                                   D3D11_SDK_VERSION, device_.put(), nullptr, context_.put());
        }
        return hr;
    };

    HRESULT hr = tryCreate(D3D_DRIVER_TYPE_HARDWARE, flags);
    if (FAILED(hr) && (flags & D3D11_CREATE_DEVICE_DEBUG)) {
        // Debug layer not installed (Graphics Tools optional feature).
        flags &= ~D3D11_CREATE_DEVICE_DEBUG;
        hr = tryCreate(D3D_DRIVER_TYPE_HARDWARE, flags);
    }
    warp_ = false;
    if (FAILED(hr)) {
        hr = tryCreate(D3D_DRIVER_TYPE_WARP, flags);
        warp_ = SUCCEEDED(hr);
    }
    return hr;
}

HRESULT GpuContext::init(HWND hwnd, UINT width, UINT height) {
    shutdown();
    // D3D11 hard limit on texture size; a giant virtual desktop gets clipped.
    width_ = std::clamp<UINT>(width, 1, D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION);
    height_ = std::clamp<UINT>(height, 1, D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION);

    HRESULT hr = createDevice();
    if (FAILED(hr)) return hr;

    ComPtr<IDXGIDevice> dxgiDevice;
    if (FAILED(hr = device_.as(dxgiDevice))) return hr;

    ComPtr<IDXGIFactory2> factory;
    if (FAILED(hr = CreateDXGIFactory2(0, __uuidof(IDXGIFactory2), factory.putVoid()))) return hr;

    DXGI_SWAP_CHAIN_DESC1 desc{};
    desc.Width = width_;
    desc.Height = height_;
    desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.BufferCount = 2;
    desc.Scaling = DXGI_SCALING_STRETCH;
    desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
    desc.AlphaMode = DXGI_ALPHA_MODE_PREMULTIPLIED;
    desc.Flags = DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT;

    ComPtr<IDXGISwapChain1> swapChain1;
    hr = factory->CreateSwapChainForComposition(device_.get(), &desc, nullptr, swapChain1.put());
    if (FAILED(hr)) return hr;
    if (FAILED(hr = swapChain1.as(swapChain_))) return hr;

    swapChain_->SetMaximumFrameLatency(1);
    frameWaitable_ = swapChain_->GetFrameLatencyWaitableObject();

    ComPtr<ID3D11Texture2D> backBuffer;
    hr = swapChain_->GetBuffer(0, __uuidof(ID3D11Texture2D), backBuffer.putVoid());
    if (FAILED(hr)) return hr;
    hr = device_->CreateRenderTargetView(backBuffer.get(), nullptr, rtv_.put());
    if (FAILED(hr)) return hr;

    hr = DCompositionCreateDevice(dxgiDevice.get(), __uuidof(IDCompositionDevice),
                                  dcompDevice_.putVoid());
    if (FAILED(hr)) return hr;
    if (FAILED(hr = dcompDevice_->CreateTargetForHwnd(hwnd, TRUE, dcompTarget_.put()))) return hr;
    if (FAILED(hr = dcompDevice_->CreateVisual(dcompVisual_.put()))) return hr;
    if (FAILED(hr = dcompVisual_->SetContent(swapChain_.get()))) return hr;
    if (FAILED(hr = dcompTarget_->SetRoot(dcompVisual_.get()))) return hr;
    return dcompDevice_->Commit();
}

void GpuContext::shutdown() {
    if (context_) {
        context_->ClearState();
        context_->Flush();
    }
    dcompVisual_.reset();
    dcompTarget_.reset();
    dcompDevice_.reset();
    rtv_.reset();
    if (frameWaitable_) {
        CloseHandle(frameWaitable_);
        frameWaitable_ = nullptr;
    }
    swapChain_.reset();
    context_.reset();
    device_.reset();
}

HRESULT GpuContext::present() {
    if (!swapChain_) return E_UNEXPECTED;
    HRESULT hr = swapChain_->Present(1, 0);
    if (hr == DXGI_STATUS_OCCLUDED) hr = S_OK;
    return hr;
}

bool GpuContext::isDeviceLost(HRESULT hr) const {
    return hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET ||
           hr == DXGI_ERROR_DEVICE_HUNG || hr == DXGI_ERROR_DRIVER_INTERNAL_ERROR;
}
