#include "broker/SharedFrame.h"
#include "common/Logger.h"
#include <iostream>

using namespace veil;
using namespace veil::broker;
int wmain() {
    Logger::Initialize(L"SharedTextureProbe.log");
    SharedFrame producer, consumer;
    if (!producer.Initialize() || !consumer.Initialize()) { return 1; }
    Handle old, current;
    if (!producer.Produce(1, old) || !producer.Produce(2, current)) { return 1; }
    DWORD flags = HANDLE_FLAG_INHERIT;
    if (!GetHandleInformation(old.get(), &flags)) { Logger::Win32Error(L"GetHandleInformation(shared)", GetLastError()); return 1; }
    if (flags & HANDLE_FLAG_INHERIT) { Logger::Error(L"Shared handle unexpectedly inheritable"); return 1; }
    Message message;
    message.frame = 1; message.handle = reinterpret_cast<std::uintptr_t>(old.release());
    std::vector<BYTE> pixels;
    // Source frame 2 is already live: old handle must still contain frame 1.
    if (!consumer.Read(message, pixels)) { return 1; }
    Microsoft::WRL::ComPtr<ID3D11Device1> device;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
    Microsoft::WRL::ComPtr<IDXGIKeyedMutex> mutex;
    if (!Logger::CheckHR(consumer.Device()->QueryInterface(IID_PPV_ARGS(device.GetAddressOf())), L"Probe device1") ||
        !Logger::CheckHR(device->OpenSharedResource1(current.get(), IID_PPV_ARGS(texture.GetAddressOf())), L"Probe shared resource") ||
        !Logger::CheckHR(texture.As(&mutex), L"Probe mutex")) { return 1; }
    const HRESULT timeout = mutex->AcquireSync(999, 20);
    if (timeout != WAIT_TIMEOUT) { Logger::Error(L"Expected real keyed mutex WAIT_TIMEOUT, got " + Logger::HrMessage(timeout)); return 1; }
    message.frame = 2; message.handle = reinterpret_cast<std::uintptr_t>(current.release());
    if (!consumer.Read(message, pixels)) { return 1; }
    std::wcout << L"SharedTextureProbe passed: immutable old snapshot, correct new frame, non-inheritance, real keyed mutex timeout\n";
    return 0;
}
