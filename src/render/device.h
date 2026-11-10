// D3D11 device + composition swap chain + DirectComposition visual tree.
#pragma once

#include <windows.h>

#include <d3d11.h>
#include <dcomp.h>
#include <dxgi1_3.h>

#include "render/com_ptr.h"

class GpuContext {
public:
    GpuContext() = default;
    GpuContext(const GpuContext&) = delete;
    GpuContext& operator=(const GpuContext&) = delete;
    ~GpuContext() { shutdown(); }

    // Creates everything for `hwnd` with a back buffer of width x height px.
    HRESULT init(HWND hwnd, UINT width, UINT height);
    void shutdown();

    // Returns S_OK, or a DXGI error (DEVICE_REMOVED/RESET => recreate everything).
    HRESULT present();
    bool isDeviceLost(HRESULT hr) const;

    ID3D11Device* device() const { return device_.get(); }
    ID3D11DeviceContext* context() const { return context_.get(); }
    ID3D11RenderTargetView* backBufferRtv() const { return rtv_.get(); }
    // Signalled when the swap chain is ready to accept a new frame.
    HANDLE frameWaitable() const { return frameWaitable_; }
    UINT width() const { return width_; }
    UINT height() const { return height_; }
    bool isWarp() const { return warp_; }

private:
    HRESULT createDevice();

    ComPtr<ID3D11Device> device_;
    ComPtr<ID3D11DeviceContext> context_;
    ComPtr<IDXGISwapChain2> swapChain_;
    ComPtr<ID3D11RenderTargetView> rtv_;
    ComPtr<IDCompositionDevice> dcompDevice_;
    ComPtr<IDCompositionTarget> dcompTarget_;
    ComPtr<IDCompositionVisual> dcompVisual_;
    HANDLE frameWaitable_ = nullptr;
    UINT width_ = 0;
    UINT height_ = 0;
    bool warp_ = false;
};
