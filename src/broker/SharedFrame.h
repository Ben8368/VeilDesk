#pragma once
#include "broker/Handle.h"
#include "broker/Protocol.h"
#include <d3d11_1.h>
#include <dxgi1_2.h>
#include <wrl/client.h>
#include <string>

namespace veil::broker {
using Microsoft::WRL::ComPtr;
struct ZeroCopyStats {
    std::uint64_t cpuReadbacks = 0, cpuUploads = 0, texturesOpened = 0, gpuCopies = 0;
    void Log() const;
};
bool DeviceAdapter(ID3D11Device* device, LUID& luid);
std::wstring AdapterText(LUID luid);
bool SameAdapter(LUID a, LUID b) noexcept;
bool ValidateTexture(const D3D11_TEXTURE2D_DESC& desc, const Message& message) noexcept;
// Immutable snapshot: writer acquires 0 and publishes 1 exactly once.
// Sequential readers return 1; key 0 is never published again.
class SharedTexture final {
public:
    ~SharedTexture();
    SharedTexture() = default;
    SharedTexture(const SharedTexture&) = delete;
    SharedTexture& operator=(const SharedTexture&) = delete;
    ID3D11Texture2D* Get() const noexcept { return texture_.Get(); }
    bool Close();
private:
    friend class SharedFrameConsumer;
    ComPtr<ID3D11Texture2D> texture_;
    ComPtr<IDXGIKeyedMutex> mutex_;
    ComPtr<ID3D11DeviceContext> context_;
    bool locked_ = false;
};
class SharedFrameProducer final {
public:
    bool Initialize();
    bool Produce(std::uint64_t frame, Handle& shared, UINT pixelWidth = width, UINT pixelHeight = height);
    ID3D11Device* Device() const noexcept { return device_.Get(); }
    LUID Adapter() const noexcept { return adapter_; }
private:
    ComPtr<ID3D11Device1> device_;
    ComPtr<ID3D11DeviceContext> context_;
    ComPtr<ID3D11Texture2D> texture_;
    LUID adapter_{};
    ComPtr<ID3D11VertexShader> vertex_;
    ComPtr<ID3D11PixelShader> pixel_;
    ComPtr<ID3D11Buffer> metadata_;
};
class SharedFrameConsumer final {
public:
    bool Initialize(ID3D11Device* device = nullptr, ZeroCopyStats* stats = nullptr);
    // Consumes delivered NT handle even on rejection. No CPU frame interface.
    bool OpenFrame(const Message& message, SharedTexture& output, DWORD timeoutMs = 1000);
    ID3D11Device* Device() const noexcept { return device_.Get(); }
    LUID Adapter() const noexcept { return adapter_; }
private:
    ComPtr<ID3D11Device1> device_;
    ComPtr<ID3D11DeviceContext> context_;
    LUID adapter_{};
    ZeroCopyStats* stats_ = nullptr;
};
}
