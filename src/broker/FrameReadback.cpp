#include "broker/FrameReadback.h"
#include "common/Logger.h"
#include "common/CapturePattern.h"
#include <cstring>

namespace veil::broker {
std::vector<BYTE> FramePixels(std::uint64_t frame, UINT pixelWidth, UINT pixelHeight) {
    std::vector<BYTE> pixels(pixelWidth * pixelHeight * 4);
    for (UINT y = 0; y < pixelHeight; ++y) {
        for (UINT x = 0; x < pixelWidth; ++x) {
            const auto i = (y * pixelWidth + x) * 4;
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
                const auto i = (static_cast<UINT>(y) * pixelWidth + static_cast<UINT>(x)) * 4;
                pixels[i] = 255; pixels[i + 1] = capturePattern[cell] ? 0 : 255;
                pixels[i + 2] = capturePattern[cell] ? 255 : 0;
            }
        }
    }
    return pixels;
}

bool FrameReadback::Read(ID3D11Texture2D* source, std::vector<BYTE>& pixels, ZeroCopyStats* stats) {
    if (!source) { return false; }
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    source->GetDevice(device.GetAddressOf()); device->GetImmediateContext(context.GetAddressOf());
    D3D11_TEXTURE2D_DESC desc{}; source->GetDesc(&desc);
    if (desc.Width > 4096 || desc.Height > 4096 || desc.Format != DXGI_FORMAT_B8G8R8A8_UNORM) { return false; }
    D3D11_TEXTURE2D_DESC previous{};
    ComPtr<ID3D11Device> previousDevice;
    if (staging_) { staging_->GetDesc(&previous); staging_->GetDevice(previousDevice.GetAddressOf()); }
    if (!staging_ || previous.Width != desc.Width || previous.Height != desc.Height || previousDevice.Get() != device.Get()) {
        staging_.Reset(); desc.BindFlags = 0; desc.MiscFlags = 0;
        desc.Usage = D3D11_USAGE_STAGING; desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        if (!Logger::CheckHR(device->CreateTexture2D(&desc, nullptr, staging_.GetAddressOf()), L"Recorder CreateTexture2D(staging)")) { return false; }
    }
    context->CopyResource(staging_.Get(), source);
    D3D11_MAPPED_SUBRESOURCE mapped{};
    if (!Logger::CheckHR(context->Map(staging_.Get(), 0, D3D11_MAP_READ, 0, &mapped), L"Recorder Map")) { return false; }
    if (stats) { ++stats->cpuReadbacks; ++stats->gpuCopies; }
    pixels.resize(static_cast<size_t>(desc.Width) * desc.Height * 4);
    for (UINT y = 0; y < desc.Height; ++y) {
        std::memcpy(pixels.data() + static_cast<size_t>(y) * desc.Width * 4,
            static_cast<BYTE*>(mapped.pData) + static_cast<size_t>(y) * mapped.RowPitch, static_cast<size_t>(desc.Width) * 4);
    }
    context->Unmap(staging_.Get(), 0);
    return true;
}
}
