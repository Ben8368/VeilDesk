#pragma once
#include <Windows.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <d2d1_1.h>
#include <dwrite.h>
#include <wrl/client.h>
#include <chrono>
#include <cstdint>
#include <string>
#include <utility>
#include "broker/SharedFrame.h"

namespace veil {
class D3D11Renderer final {
public:
    D3D11Renderer() = default;
    ~D3D11Renderer();
    D3D11Renderer(const D3D11Renderer&) = delete;
    D3D11Renderer& operator=(const D3D11Renderer&) = delete;
    bool Initialize(HWND hwnd, bool tryDisplayOnly = true, bool simulateDisplayOnlyFailure = false, bool requireHardware = false);
    bool Resize(UINT width, UINT height);
    // Shared mode uses a GPU snapshot; Phase 1 retains its diagnostic drawing.
    bool RenderFrame(UINT dpi);
    bool SetSourceTexture(ID3D11Texture2D* texture);
    ID3D11Device* Device() const noexcept { return device_.Get(); }
    void SetStats(broker::ZeroCopyStats* stats) noexcept { stats_ = stats; }
    void SetProtectionStatus(std::wstring status) { protectionStatus_ = std::move(status); }
    std::uint64_t FrameCount() const noexcept { return frame_; }
    bool DisplayOnlyEnabled() const noexcept { return (swapFlags_ & DXGI_SWAP_CHAIN_FLAG_DISPLAY_ONLY) != 0; }
private:
    friend struct D3D11RendererTestAccess;
    bool CreateTargets();
    void ReleaseTargets();
    broker::ZeroCopyStats* stats_ = nullptr;
    HWND hwnd_ = nullptr;
    UINT width_ = 0;
    UINT height_ = 0;
    UINT swapFlags_ = 0;
    bool occluded_ = false;
    std::uint64_t frame_ = 0;
    std::wstring protectionStatus_ = L"Protection not configured";
    std::chrono::steady_clock::time_point start_ = std::chrono::steady_clock::now();
    Microsoft::WRL::ComPtr<ID3D11Device> device_;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> source_;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> context_;
    Microsoft::WRL::ComPtr<IDXGISwapChain1> swapChain_;
    Microsoft::WRL::ComPtr<ID3D11RenderTargetView> renderTarget_;
    Microsoft::WRL::ComPtr<ID2D1Factory1> textFactory_;
    Microsoft::WRL::ComPtr<ID2D1Device> textDevice_;
    Microsoft::WRL::ComPtr<ID2D1DeviceContext> textContext_;
    Microsoft::WRL::ComPtr<ID2D1Bitmap1> textTarget_;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brush_;
    Microsoft::WRL::ComPtr<IDWriteFactory> writeFactory_;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> titleFormat_;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> bodyFormat_;
};
}
