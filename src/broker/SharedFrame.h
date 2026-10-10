#pragma once
#include "broker/Handle.h"
#include "broker/Protocol.h"
#include <d3d11_1.h>
#include <dxgi1_2.h>
#include <wrl/client.h>
#include <vector>

namespace veil::broker {
class SharedFrame final {
public:
    bool Initialize();
    bool Produce(std::uint64_t frame, Handle& shared);
    bool Read(const Message& message, std::vector<BYTE>& pixels);
    ID3D11Device* Device() const noexcept { return device_.Get(); }
private:
    Microsoft::WRL::ComPtr<ID3D11Device1> device_;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> context_;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> texture_, staging_;
};
std::vector<BYTE> FramePixels(std::uint64_t frame);
}
