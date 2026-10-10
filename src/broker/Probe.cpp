#include "broker/SharedFrame.h"
#include "broker/FrameReadback.h"
#include "common/Logger.h"
#include <iostream>
#include <array>

using namespace veil;
using namespace veil::broker;
namespace {
bool Duplicate(HANDLE handle, Message& message) {
    HANDLE raw = nullptr;
    if (!DuplicateHandle(GetCurrentProcess(), handle, GetCurrentProcess(), &raw, 0, FALSE, DUPLICATE_SAME_ACCESS)) {
        Logger::Win32Error(L"Probe DuplicateHandle", GetLastError()); return false;
    }
    message.handle = reinterpret_cast<std::uintptr_t>(raw); return true;
}
bool CrossAdapterCase(HANDLE shared, const Message& message) {
    ComPtr<IDXGIFactory1> factory;
    if (!Logger::CheckHR(CreateDXGIFactory1(IID_PPV_ARGS(factory.GetAddressOf())), L"Probe DXGI factory")) { return false; }
    for (UINT i = 0;; ++i) {
        ComPtr<IDXGIAdapter1> adapter;
        const HRESULT result = factory->EnumAdapters1(i, adapter.GetAddressOf());
        if (result == DXGI_ERROR_NOT_FOUND) { break; }
        if (!Logger::CheckHR(result, L"Probe EnumAdapters1")) { return false; }
        DXGI_ADAPTER_DESC1 desc{};
        if (!Logger::CheckHR(adapter->GetDesc1(&desc), L"Probe adapter descriptor")) { return false; }
        if ((desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) || SameAdapter(desc.AdapterLuid, {message.adapterLow, message.adapterHigh})) { continue; }
        ComPtr<ID3D11Device> device;
        const HRESULT created = D3D11CreateDevice(adapter.Get(), D3D_DRIVER_TYPE_UNKNOWN, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT,
            nullptr, 0, D3D11_SDK_VERSION, device.GetAddressOf(), nullptr, nullptr);
        if (FAILED(created)) { Logger::Warn(L"Alternate adapter device unavailable: " + Logger::HrMessage(created)); continue; }
        SharedFrameConsumer other; SharedTexture denied;
        auto mismatch = message;
        if (!other.Initialize(device.Get()) || !Duplicate(shared, mismatch)) { return false; }
        if (other.OpenFrame(mismatch, denied) || denied.Get()) { return false; }
        Logger::Info(L"Real cross-adapter rejection passed; Consumer=" + std::wstring(desc.Description));
        return true;
    }
    Logger::Warn(L"NOT TESTED: real second hardware adapter unavailable; metadata mismatch tested separately");
    return true;
}
bool DescriptorCases(ID3D11Device* device, const Message& message) {
    D3D11_TEXTURE2D_DESC d{};
    d.Width = width; d.Height = height; d.MipLevels = 1; d.ArraySize = 1;
    d.Format = DXGI_FORMAT_B8G8R8A8_UNORM; d.SampleDesc.Count = 1;
    d.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
    d.MiscFlags = D3D11_RESOURCE_MISC_SHARED_NTHANDLE | D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX;
    if (!ValidateTexture(d, message)) { return false; }
    using Mutation = void (*)(D3D11_TEXTURE2D_DESC&);
    const std::array<Mutation, 11> mutations{{
        [](auto& v) { ++v.Width; }, [](auto& v) { ++v.Height; },
        [](auto& v) { v.Format = DXGI_FORMAT_R8G8B8A8_UNORM; },
        [](auto& v) { v.MipLevels = 2; }, [](auto& v) { v.ArraySize = 2; },
        [](auto& v) { v.SampleDesc.Count = 2; }, [](auto& v) { v.SampleDesc.Quality = 1; },
        [](auto& v) { v.BindFlags = D3D11_BIND_SHADER_RESOURCE; }, [](auto& v) { v.MiscFlags = 0; },
        [](auto& v) { v.Usage = D3D11_USAGE_STAGING; }, [](auto& v) { v.CPUAccessFlags = D3D11_CPU_ACCESS_READ; }
    }};
    for (auto mutate : mutations) {
        auto invalid = d; mutate(invalid);
        if (ValidateTexture(invalid, message)) { Logger::Error(L"Invalid descriptor accepted"); return false; }
    }
    // Actual shared texture disagrees with metadata, not just a constructed descriptor.
    ++d.Width;
    ComPtr<ID3D11Texture2D> invalid;
    ComPtr<IDXGIResource1> resource;
    if (!Logger::CheckHR(device->CreateTexture2D(&d, nullptr, invalid.GetAddressOf()), L"Probe invalid dimensions") ||
        !Logger::CheckHR(invalid.As(&resource), L"Probe resource")) { return false; }
    HANDLE raw = nullptr;
    if (!Logger::CheckHR(resource->CreateSharedHandle(nullptr, DXGI_SHARED_RESOURCE_READ, nullptr, &raw), L"Probe invalid resource handle")) { return false; }
    Message rejected = message; rejected.handle = reinterpret_cast<std::uintptr_t>(raw);
    SharedFrameConsumer consumer; SharedTexture output;
    return consumer.Initialize() && !consumer.OpenFrame(rejected, output) && !output.Get();
}
}
int wmain() {
    Logger::Initialize(L"SharedTextureProbe.log");
    SharedFrameProducer producer;
    SharedFrameConsumer consumer, other;
    FrameReadback readback;
    if (!producer.Initialize() || !consumer.Initialize() || !other.Initialize()) { return 1; }
    Handle old, current;
    if (!producer.Produce(1, old) || !producer.Produce(2, current)) { return 1; }
    DWORD flags = HANDLE_FLAG_INHERIT;
    if (!GetHandleInformation(old.get(), &flags)) { Logger::Win32Error(L"GetHandleInformation(shared)", GetLastError()); return 1; }
    if (flags & HANDLE_FLAG_INHERIT) { Logger::Error(L"Shared handle unexpectedly inheritable"); return 1; }
    Message message; message.frame = 1;
    message.adapterLow = producer.Adapter().LowPart; message.adapterHigh = producer.Adapter().HighPart;
    if (!DescriptorCases(producer.Device(), message) || !CrossAdapterCase(old.get(), message)) { return 1; }
    Message mismatch = message; mismatch.adapterLow ^= 1;
    SharedTexture rejected;
    if (!Duplicate(old.get(), mismatch) || consumer.OpenFrame(mismatch, rejected) || rejected.Get()) { return 1; }
    std::vector<BYTE> pixels;
    SharedTexture snapshot;
    for (unsigned index : {1U, 2U, 1U}) {
        message.frame = index;
        if (!Duplicate(index == 1 ? old.get() : current.get(), message) || !consumer.OpenFrame(message, snapshot)) { return 1; }
        if (!readback.Read(snapshot.Get(), pixels) || pixels != FramePixels(index)) { return 1; }
        if (index == 2) {
            Message blocked = message;
            if (!Duplicate(current.get(), blocked) || other.OpenFrame(blocked, rejected, 20) || rejected.Get()) { return 1; }
        }
        if (!snapshot.Close()) { return 1; }
    }
    std::wcout << L"SharedTextureProbe passed: immutable old snapshot, GPU pattern pixels, non-inheritance, real synchronization timeout, adapter metadata mismatch, 11 descriptor cases and real malformed resource\n";
    return 0;
}
