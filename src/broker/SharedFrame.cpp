#include "broker/SharedFrame.h"
#include "common/Logger.h"
#include <sstream>
#include <iomanip>

namespace veil::broker {
void ZeroCopyStats::Log() const {
    Logger::Info(L"ZeroCopyStats:\nCPU frame readbacks: " + std::to_wstring(cpuReadbacks) +
        L"\nCPU frame uploads: " + std::to_wstring(cpuUploads) +
        L"\nShared textures opened: " + std::to_wstring(texturesOpened) +
        L"\nGPU copies: " + std::to_wstring(gpuCopies));
}
bool SameAdapter(LUID a, LUID b) noexcept { return a.LowPart == b.LowPart && a.HighPart == b.HighPart; }
std::wstring AdapterText(LUID luid) {
    std::wostringstream text;
    text << std::hex << std::setfill(L'0') << std::setw(8) << static_cast<UINT>(luid.HighPart)
        << L":" << std::setw(8) << luid.LowPart;
    return text.str();
}
bool DeviceAdapter(ID3D11Device* device, LUID& luid) {
    ComPtr<IDXGIDevice> dxgi;
    ComPtr<IDXGIAdapter> adapter;
    DXGI_ADAPTER_DESC desc{};
    if (!Logger::CheckHR(device->QueryInterface(IID_PPV_ARGS(dxgi.GetAddressOf())), L"SharedFrame IDXGIDevice") ||
        !Logger::CheckHR(dxgi->GetAdapter(adapter.GetAddressOf()), L"SharedFrame GetAdapter") ||
        !Logger::CheckHR(adapter->GetDesc(&desc), L"SharedFrame adapter descriptor")) { return false; }
    luid = desc.AdapterLuid;
    Logger::Info(L"Shared frame GPU=" + std::wstring(desc.Description) + L" LUID=" + AdapterText(luid));
    return true;
}
bool ValidateTexture(const D3D11_TEXTURE2D_DESC& d, const Message& m) noexcept {
    return m.pixelWidth > 0 && m.pixelHeight > 0 && m.pixelWidth <= 4096 && m.pixelHeight <= 4096 &&
        d.Width == m.pixelWidth && d.Height == m.pixelHeight && d.Format == DXGI_FORMAT_B8G8R8A8_UNORM &&
        d.MipLevels == 1 && d.ArraySize == 1 && d.SampleDesc.Count == 1 && d.SampleDesc.Quality == 0 &&
        d.Usage == D3D11_USAGE_DEFAULT && d.CPUAccessFlags == 0 &&
        d.BindFlags == (D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET) &&
        d.MiscFlags == (D3D11_RESOURCE_MISC_SHARED_NTHANDLE | D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX);
}
SharedTexture::~SharedTexture() { Close(); }
bool SharedTexture::Close() {
    bool ok = true;
    if (locked_) {
        // Submit copies while ownership is held; ReleaseSync orders shared GPU access.
        context_->Flush();
        ok = Logger::CheckHR(mutex_->ReleaseSync(1), L"SharedTexture ReleaseSync(read key); Action: deny on failure");
        locked_ = false;
    }
    texture_.Reset(); mutex_.Reset(); context_.Reset();
    return ok;
}
bool SharedFrameConsumer::Initialize(ID3D11Device* device, ZeroCopyStats* stats) {
    ComPtr<ID3D11Device> base;
    if (device) { base = device; device->GetImmediateContext(context_.GetAddressOf()); }
    else if (!Logger::CheckHR(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
        D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0, D3D11_SDK_VERSION,
        base.GetAddressOf(), nullptr, context_.GetAddressOf()), L"Consumer D3D11CreateDevice(hardware)")) { return false; }
    stats_ = stats;
    return Logger::CheckHR(base.As(&device_), L"Consumer ID3D11Device1") && DeviceAdapter(base.Get(), adapter_);
}
bool SharedFrameConsumer::OpenFrame(const Message& m, SharedTexture& output, DWORD timeoutMs) {
    Handle shared(reinterpret_cast<HANDLE>(static_cast<std::uintptr_t>(m.handle)));
    if (!output.Close()) { return false; }
    if (!device_ || !shared || m.signature != magic || m.protocol != version || m.kind != Kind::Frame ||
        m.status != Status::Ok || !m.generation || !m.frame || timeoutMs > 1000) {
        Logger::Error(L"Location: SharedFrameConsumer; Cause: invalid frame; Action: deny"); return false;
    }
    const LUID producer{m.adapterLow, m.adapterHigh};
    if (!SameAdapter(producer, adapter_)) {
        Logger::Error(L"Shared frame adapter mismatch; Producer LUID=" + AdapterText(producer) +
            L" Consumer LUID=" + AdapterText(adapter_) + L"; Action: deny (cross-adapter Unsupported)"); return false;
    }
    ComPtr<ID3D11Texture2D> incoming;
    if (!Logger::CheckHR(device_->OpenSharedResource1(shared.get(), IID_PPV_ARGS(incoming.GetAddressOf())),
        L"Consumer OpenSharedResource1; Action: deny")) { return false; }
    D3D11_TEXTURE2D_DESC desc{}; incoming->GetDesc(&desc);
    if (!ValidateTexture(desc, m)) {
        Logger::Error(L"Location: SharedFrameConsumer; Cause: shared texture descriptor rejected; Action: deny"); return false;
    }
    ComPtr<IDXGIKeyedMutex> mutex;
    if (!Logger::CheckHR(incoming.As(&mutex), L"Consumer keyed mutex")) { return false; }
    const HRESULT acquired = mutex->AcquireSync(1, timeoutMs);
    if (acquired != S_OK) {
        Logger::Error(L"Location: Consumer AcquireSync; Cause: " + Logger::HrMessage(acquired) +
            L"; Action: deny, discard session on timeout/abandoned"); return false;
    }
    output.texture_ = incoming; output.mutex_ = mutex; output.context_ = context_; output.locked_ = true;
    if (stats_) { ++stats_->texturesOpened; }
    return true;
}
}
