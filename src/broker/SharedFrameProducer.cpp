#include "broker/SharedFrame.h"
#include "common/Logger.h"
#include <d3dcompiler.h>

namespace veil::broker {
namespace {
// Only 16 bytes of frame metadata are uploaded. Every pixel is generated on GPU.
constexpr char patternShader[] = R"(
cbuffer Metadata : register(b0) { uint frame; uint3 reserved; };
float4 VS(uint id : SV_VertexID) : SV_Position {
    float2 p = float2((id << 1) & 2, id & 2);
    return float4(p * float2(2, -2) + float2(-1, 1), 0, 1);
}
float4 PS(float4 position : SV_Position) : SV_Target {
    uint2 p = uint2(position.xy);
    float4 c = float4(p.y & 255, p.x & 255, frame & 255, 255) / 255.0;
    if (p.x >= 24 && p.x < 216 && p.y >= 24 && p.y < 36) {
        const uint cells[16] = {1,0,1,1,0,0,1,0,0,1,0,1,0,1,1,0};
        c = cells[(p.x - 24) / 12] ? float4(1,0,1,1) : float4(0,1,1,1);
    }
    return c;
}
)";
bool Compile(const char* entry, const char* target, ComPtr<ID3DBlob>& code) {
    ComPtr<ID3DBlob> errors;
    const HRESULT hr = D3DCompile(patternShader, sizeof(patternShader) - 1, "VeilDeskGpuPattern", nullptr,
        nullptr, entry, target, D3DCOMPILE_ENABLE_STRICTNESS, 0, code.GetAddressOf(), errors.GetAddressOf());
    if (errors) { Logger::Warn(L"GPU pattern shader diagnostics present"); }
    return Logger::CheckHR(hr, L"Producer D3DCompile");
}
}
bool SharedFrameProducer::Initialize() {
    ComPtr<ID3D11Device> base;
    if (!Logger::CheckHR(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
        D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0, D3D11_SDK_VERSION,
        base.GetAddressOf(), nullptr, context_.GetAddressOf()), L"Producer D3D11CreateDevice(hardware)") ||
        !Logger::CheckHR(base.As(&device_), L"Producer ID3D11Device1") || !DeviceAdapter(base.Get(), adapter_)) { return false; }
    ComPtr<ID3DBlob> vs, ps;
    if (!Compile("VS", "vs_4_0", vs) || !Compile("PS", "ps_4_0", ps) ||
        !Logger::CheckHR(device_->CreateVertexShader(vs->GetBufferPointer(), vs->GetBufferSize(), nullptr, vertex_.GetAddressOf()), L"Producer vertex shader") ||
        !Logger::CheckHR(device_->CreatePixelShader(ps->GetBufferPointer(), ps->GetBufferSize(), nullptr, pixel_.GetAddressOf()), L"Producer pixel shader")) { return false; }
    D3D11_BUFFER_DESC desc{}; desc.ByteWidth = 16; desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    return Logger::CheckHR(device_->CreateBuffer(&desc, nullptr, metadata_.GetAddressOf()), L"Producer metadata buffer");
}
bool SharedFrameProducer::Produce(std::uint64_t frame, Handle& shared, UINT pixelWidth, UINT pixelHeight) {
    if (!device_ || !frame || pixelWidth < 216 || pixelHeight < 36 || pixelWidth > 4096 || pixelHeight > 4096) { return false; }
    // Never write a published resource again, including across revoke/generation changes.
    texture_.Reset(); shared.reset();
    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = pixelWidth; desc.Height = pixelHeight; desc.MipLevels = 1; desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM; desc.SampleDesc.Count = 1;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
    desc.MiscFlags = D3D11_RESOURCE_MISC_SHARED_NTHANDLE | D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX;
    if (!Logger::CheckHR(device_->CreateTexture2D(&desc, nullptr, texture_.GetAddressOf()), L"Producer CreateTexture2D(shared)")) { return false; }
    ComPtr<IDXGIKeyedMutex> mutex;
    ComPtr<IDXGIResource1> resource;
    ComPtr<ID3D11RenderTargetView> target;
    if (!Logger::CheckHR(texture_.As(&mutex), L"Producer mutex") ||
        !Logger::CheckHR(texture_.As(&resource), L"Producer resource") ||
        !Logger::CheckHR(device_->CreateRenderTargetView(texture_.Get(), nullptr, target.GetAddressOf()), L"Producer render target")) { return false; }
    const HRESULT acquired = mutex->AcquireSync(0, 1000);
    if (acquired != S_OK) { Logger::Error(L"Location: Producer AcquireSync; Cause: " + Logger::HrMessage(acquired) + L"; Action: exit"); return false; }
    const UINT metadata[]{static_cast<UINT>(frame), 0, 0, 0};
    context_->UpdateSubresource(metadata_.Get(), 0, nullptr, metadata, 0, 0);
    const D3D11_VIEWPORT viewport{0, 0, static_cast<float>(pixelWidth), static_cast<float>(pixelHeight), 0, 1};
    context_->RSSetViewports(1, &viewport);
    ID3D11RenderTargetView* targets[]{target.Get()}; context_->OMSetRenderTargets(1, targets, nullptr);
    context_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    context_->VSSetShader(vertex_.Get(), nullptr, 0); context_->PSSetShader(pixel_.Get(), nullptr, 0);
    ID3D11Buffer* buffers[]{metadata_.Get()}; context_->PSSetConstantBuffers(0, 1, buffers);
    context_->Draw(3, 0); context_->OMSetRenderTargets(0, nullptr, nullptr); context_->Flush();
    if (!Logger::CheckHR(mutex->ReleaseSync(1), L"Producer ReleaseSync(read key)") ||
        !Logger::CheckHR(device_->GetDeviceRemovedReason(), L"Producer GPU device status")) { return false; }
    HANDLE raw = nullptr;
    if (!Logger::CheckHR(resource->CreateSharedHandle(nullptr, DXGI_SHARED_RESOURCE_READ, nullptr, &raw),
        L"CreateSharedHandle(unnamed, non-inheritable, read-only)")) { return false; }
    shared.reset(raw); return true;
}
}
