#include "broker/SharedFrame.h"
#include "common/Logger.h"
#include "common/CapturePattern.h"
#include <cstring>

namespace veil::broker {
std::vector<BYTE> FramePixels(std::uint64_t frame) {
    std::vector<BYTE> pixels(width * height * 4);
    for (UINT y = 0; y < height; ++y) {
        for (UINT x = 0; x < width; ++x) {
            const auto i = (y * width + x) * 4;
            pixels[i] = static_cast<BYTE>(frame & 255);
            pixels[i + 1] = static_cast<BYTE>(x & 255);
            pixels[i + 2] = static_cast<BYTE>(y & 255);
            pixels[i + 3] = 255;
        }
    }
    // Same diagnostic marker as Phase 1, at native size.
    for (std::size_t cell = 0; cell < capturePattern.size(); ++cell) {
        for (int y = patternTop; y < patternTop + patternCell; ++y) {
            for (int x = patternLeft + static_cast<int>(cell) * patternCell;
                x < patternLeft + static_cast<int>(cell + 1) * patternCell; ++x) {
                const auto i = (static_cast<UINT>(y) * width + static_cast<UINT>(x)) * 4;
                pixels[i] = 255; pixels[i + 1] = capturePattern[cell] ? 0 : 255;
                pixels[i + 2] = capturePattern[cell] ? 255 : 0;
            }
        }
    }
    return pixels;
}
bool SharedFrame::Initialize() {
    Microsoft::WRL::ComPtr<ID3D11Device> base;
    const auto hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
        D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0, D3D11_SDK_VERSION,
        base.GetAddressOf(), nullptr, context_.GetAddressOf());
    if (!Logger::CheckHR(hr, L"SharedFrame D3D11CreateDevice(hardware)") ||
        !Logger::CheckHR(base.As(&device_), L"SharedFrame ID3D11Device1")) { return false; }
    return true;
}
bool SharedFrame::Produce(std::uint64_t frame, Handle& shared) {
    // Fresh immutable resource per frame: an old handle never receives future pixels.
    texture_.Reset();
    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = width; desc.Height = height; desc.MipLevels = 1; desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM; desc.SampleDesc.Count = 1;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
    desc.MiscFlags = D3D11_RESOURCE_MISC_SHARED_NTHANDLE | D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX;
    if (!Logger::CheckHR(device_->CreateTexture2D(&desc, nullptr, texture_.GetAddressOf()), L"CreateTexture2D(shared)")) { return false; }
    Microsoft::WRL::ComPtr<IDXGIKeyedMutex> mutex;
    Microsoft::WRL::ComPtr<IDXGIResource1> resource;
    if (!Logger::CheckHR(texture_.As(&mutex), L"Shared texture mutex") ||
        !Logger::CheckHR(texture_.As(&resource), L"Shared texture resource")) { return false; }
    const HRESULT acquired = mutex->AcquireSync(0, 1000);
    if (acquired != S_OK) { Logger::Error(L"Producer AcquireSync: " + Logger::HrMessage(acquired)); return false; }
    const auto pixels = FramePixels(frame);
    context_->UpdateSubresource(texture_.Get(), 0, nullptr, pixels.data(), width * 4, 0);
    context_->Flush();
    if (!Logger::CheckHR(mutex->ReleaseSync(1), L"Producer ReleaseSync")) { return false; }
    HANDLE raw = nullptr;
    if (!Logger::CheckHR(resource->CreateSharedHandle(nullptr, DXGI_SHARED_RESOURCE_READ, nullptr, &raw),
        L"CreateSharedHandle(unnamed, non-inheritable, read-only)")) { return false; }
    shared.reset(raw); return true;
}
bool SharedFrame::Read(const Message& message, std::vector<BYTE>& pixels) {
    Handle shared(reinterpret_cast<HANDLE>(static_cast<std::uintptr_t>(message.handle)));
    Microsoft::WRL::ComPtr<ID3D11Texture2D> incoming;
    if (!Logger::CheckHR(device_->OpenSharedResource1(shared.get(), IID_PPV_ARGS(incoming.GetAddressOf())),
        L"OpenSharedResource1")) { return false; }
    D3D11_TEXTURE2D_DESC desc{}; incoming->GetDesc(&desc);
    if (desc.Width != width || desc.Height != height || desc.Format != DXGI_FORMAT_B8G8R8A8_UNORM ||
        desc.ArraySize != 1 || desc.MipLevels != 1 || desc.SampleDesc.Count != 1) {
        Logger::Error(L"Shared texture descriptor rejected"); return false;
    }
    Microsoft::WRL::ComPtr<IDXGIKeyedMutex> mutex;
    if (!Logger::CheckHR(incoming.As(&mutex), L"Consumer shared mutex")) { return false; }
    const HRESULT acquired = mutex->AcquireSync(1, 1000);
    // WAIT_TIMEOUT / WAIT_ABANDONED are positive HRESULTs; FAILED alone is insufficient.
    if (acquired != S_OK) { Logger::Error(L"Consumer AcquireSync: " + Logger::HrMessage(acquired)); return false; }
    bool ok = true;
    if (!staging_) {
        desc.BindFlags = 0; desc.MiscFlags = 0; desc.Usage = D3D11_USAGE_STAGING; desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        ok = Logger::CheckHR(device_->CreateTexture2D(&desc, nullptr, staging_.GetAddressOf()), L"CreateTexture2D(staging)");
    }
    if (ok) {
        context_->CopyResource(staging_.Get(), incoming.Get());
        D3D11_MAPPED_SUBRESOURCE mapped{};
        ok = Logger::CheckHR(context_->Map(staging_.Get(), 0, D3D11_MAP_READ, 0, &mapped), L"Map(shared frame)");
        if (ok) {
            pixels.resize(width * height * 4);
            for (UINT y = 0; y < height; ++y) {
                std::memcpy(pixels.data() + y * width * 4, static_cast<BYTE*>(mapped.pData) + y * mapped.RowPitch, width * 4);
            }
            context_->Unmap(staging_.Get(), 0);
        }
    }
    const bool released = Logger::CheckHR(mutex->ReleaseSync(1), L"Consumer ReleaseSync");
    if (ok && pixels != FramePixels(message.frame)) { Logger::Error(L"Frame pixel/ID mismatch"); return false; }
    return ok && released;
}
}
