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
#include <vector>

namespace veil {
class D3D11Renderer final {
public:
    D3D11Renderer() = default;
    ~D3D11Renderer();
    D3D11Renderer(const D3D11Renderer&) = delete;
    D3D11Renderer& operator=(const D3D11Renderer&) = delete;
    bool Initialize(HWND hwnd, bool tryDisplayOnly = true, bool simulateDisplayOnlyFailure = false);
    bool Resize(UINT width, UINT height);
    // Output/presentation is kept separate from the test source. Phase 2 can
    // replace source drawing with sampling an ID3D11Texture2D in this path.
    bool RenderFrame(UINT dpi);
    bool SetSourcePixels(const std::vector<BYTE>& pixels, UINT width, UINT height);
    void SetProtectionStatus(std::wstring status) { protectionStatus_ = std::move(status); }
    std::uint64_t FrameCount() const noexcept { return frame_; }
    bool DisplayOnlyEnabled() const noexcept { return (swapFlags_ & DXGI_SWAP_CHAIN_FLAG_DISPLAY_ONLY) != 0; }
private:
    bool CreateTargets();
    void ReleaseTargets();
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
