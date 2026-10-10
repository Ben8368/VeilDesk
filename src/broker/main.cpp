#include "broker/Channel.h"
#include "broker/SharedFrame.h"
#include "broker/FrameReadback.h"
#include "capture/CaptureEngine.h"
#include "common/Logger.h"
#include <filesystem>
#include <iostream>

using namespace veil;
using namespace veil::broker;
namespace {
int Producer(HANDLE input, HANDLE output) {
    Handle read(input), write(output);
    if (GetFileType(read.get()) != FILE_TYPE_PIPE || GetFileType(write.get()) != FILE_TYPE_PIPE) {
        Logger::Error(L"Denied: producer requires private inherited pipe capabilities"); return 1;
    }
    if (!SetHandleInformation(read.get(), HANDLE_FLAG_INHERIT, 0) ||
        !SetHandleInformation(write.get(), HANDLE_FLAG_INHERIT, 0)) {
        Logger::Win32Error(L"Producer disable inheritance", GetLastError()); return 1;
    }
    SharedFrameProducer gpu;
    if (!gpu.Initialize()) { return 1; }
    Lease lease;
    for (;;) {
        Message request;
        if (!Receive(read.get(), request, 5000)) { return 1; }
        if (request.kind == Kind::Stop) { return 0; }
        if (!lease.granted) { lease.granted = true; lease.deadline = request.deadline; lease.generation = request.generation; }
        if (lease.Check(request, GetTickCount64()) != Status::Ok || request.handle != 0) { return 1; }
        Handle frame;
        if (!gpu.Produce(request.frame, frame)) { return 1; }
        request.handle = reinterpret_cast<std::uintptr_t>(frame.get());
        request.adapterLow = gpu.Adapter().LowPart; request.adapterHigh = gpu.Adapter().HighPart;
        if (!Send(write.get(), request)) { return 1; }
        Message ack;
        if (!Receive(read.get(), ack, 5000) || ack.kind != Kind::Ack || ack.status != Status::Ok || ack.handle != 0 ||
            ack.frame != request.frame || ack.generation != request.generation || ack.deadline != request.deadline) { return 1; }
        lease.lastFrame = request.frame;
    }
}
int Consumer(HANDLE input, HANDLE output, const std::filesystem::path& destination, bool stall) {
    Handle read(input), write(output);
    if (GetFileType(read.get()) != FILE_TYPE_PIPE || GetFileType(write.get()) != FILE_TYPE_PIPE) {
        Logger::Error(L"Denied: consumer requires private inherited pipe capabilities"); return 1;
    }
    if (!SetHandleInformation(read.get(), HANDLE_FLAG_INHERIT, 0) ||
        !SetHandleInformation(write.get(), HANDLE_FLAG_INHERIT, 0)) {
        Logger::Win32Error(L"Consumer disable inheritance", GetLastError()); return 1;
    }
    SharedFrameConsumer gpu;
    FrameReadback readback;
    const HRESULT initialized = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (!Logger::CheckHR(initialized, L"Consumer CoInitializeEx")) { return 1; }
    struct Apartment { ~Apartment() { CoUninitialize(); } } apartment;
    if (!gpu.Initialize()) { return 1; }
    Message message;
    Lease lease;
    while (Receive(read.get(), message, 5000)) {
        if (message.kind == Kind::Stop || message.kind == Kind::Denied) { return 0; }
        if (stall) { Sleep(3500); return 1; }
        if (!lease.granted) {
            lease.granted = true; lease.deadline = message.deadline; lease.generation = message.generation;
        }
        const auto status = lease.Check(message, GetTickCount64());
        Message ack = message; ack.kind = Kind::Ack; ack.handle = 0; ack.status = status;
        std::vector<BYTE> pixels;
        if (status != Status::Ok) {
            Handle rejected(reinterpret_cast<HANDLE>(static_cast<std::uintptr_t>(message.handle)));
        } else {
            SharedTexture texture;
            if (!gpu.OpenFrame(message, texture) || !readback.Read(texture.Get(), pixels) ||
                pixels != FramePixels(message.frame) || !texture.Close()) { ack.status = Status::Failed; }
        }
        if (ack.status == Status::Ok) {
            const auto finalPath = destination / (L"frame-" + std::to_wstring(message.frame) + L".png");
            const auto temporary = destination / (L"pending-" + std::to_wstring(GetCurrentProcessId()) + L".png");
            Handle reservation(CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr));
            if (!reservation) { Logger::Win32Error(L"CreateFileW(output reservation)", GetLastError()); ack.status = Status::Failed; }
            else {
                reservation.reset();
                CapturedImage image{width, height, std::move(pixels)};
                const bool saved = CaptureEngine::SavePng(image, temporary.wstring());
                const bool moved = saved && MoveFileW(temporary.c_str(), finalPath.c_str());
                if (!moved) {
                    if (saved) { Logger::Win32Error(L"Publish PNG without replacing existing file", GetLastError()); }
                    ack.status = Status::Failed;
                    if (!DeleteFileW(temporary.c_str())) { Logger::Win32Error(L"DeleteFileW(partial output)", GetLastError()); }
                }
            }
        }
        if (!Send(write.get(), ack) || ack.status != Status::Ok) { return 1; }
        lease.lastFrame = message.frame;
    }
    return 1;
}
bool Deliver(Child& child, HANDLE shared, const Message& source) {
    if (GetTickCount64() >= source.deadline) { Logger::Error(L"Lease expired before handle delivery"); return false; }
    if (WaitForSingleObject(child.process.get(), 0) != WAIT_TIMEOUT) { Logger::Error(L"Consumer already exited"); return false; }
    HANDLE remote = nullptr;
    if (!DuplicateHandle(GetCurrentProcess(), shared, child.process.get(), &remote, 0, FALSE, DUPLICATE_SAME_ACCESS)) {
        Logger::Win32Error(L"DuplicateHandle(approved retained process)", GetLastError()); return false;
    }
    Message request = source;
    request.handle = reinterpret_cast<std::uintptr_t>(remote);
    if (!Send(child.input.get(), request)) { return false; } // child termination closes undelivered duplicate
    Message ack;
    if (!Receive(child.output.get(), ack, 2000)) { return false; }
    if (ack.kind != Kind::Ack || ack.status != Status::Ok || ack.handle != 0 ||
        ack.generation != source.generation || ack.frame != source.frame || ack.deadline != source.deadline) {
        Logger::Error(L"ACK identity/frame/generation/status rejected"); return false;
    }
    return true;
}
bool ProtocolTests() {
    Message message; message.frame = 1; message.deadline = 100;
    Lease lease{1, 100, 0, false, false};
    if (lease.Check(message, 1) != Status::Invalid) { return false; }
    lease.granted = true;
    if (lease.Check(message, 1) != Status::Ok || lease.Check(message, 100) != Status::Expired) { return false; }
    lease.lastFrame = 1;
    if (lease.Check(message, 2) != Status::Replay) { return false; }
    lease.lastFrame = 0; lease.revoked = true;
    if (lease.Check(message, 2) != Status::Revoked) { return false; }
    lease.revoked = false; ++message.generation;
    if (lease.Check(message, 2) != Status::Invalid) { return false; }
    --message.generation; ++message.protocol;
    if (lease.Check(message, 2) != Status::Invalid) { return false; }
    --message.protocol; message.pixelWidth = 0;
    if (lease.Check(message, 2) != Status::Invalid) { return false; }
    Logger::Info(L"Protocol negative cases passed (state tests, not OS identity evidence)"); return true;
}
}
int wmain(int argc, wchar_t** argv) {
    Logger::Initialize(L"Broker-" + std::to_wstring(GetCurrentProcessId()) + L".log");
    std::filesystem::path destination;
    unsigned frames = 8, revokeAfter = 0;
    bool child = false, producerMode = false, stall = false, tests = false, viewer = false, expired = false, deny = false;
    HANDLE input = nullptr, output = nullptr;
    try {
        for (int i = 1; i < argc; ++i) {
            const std::wstring arg(argv[i]);
            if (arg == L"--consumer") { child = true; }
            else if (arg == L"--producer") { producerMode = true; }
            else if (arg == L"--stall") { stall = true; }
            else if (arg == L"--protocol-tests") { tests = true; }
            else if (arg == L"--viewer") { viewer = true; }
            else if (arg == L"--expired") { expired = true; }
            else if (arg == L"--deny") { deny = true; }
            else if (arg == L"--output" && i + 1 < argc) { destination = argv[++i]; }
            else if ((arg == L"--frames" || arg == L"--revoke-after") && i + 1 < argc) {
                const std::wstring value(argv[++i]); std::size_t consumed = 0;
                const auto number = std::stoul(value, &consumed);
                if (consumed != value.size() || number == 0 || number > 120) { return 1; }
                if (arg == L"--frames") { frames = number; } else { revokeAfter = number; }
            } else if (arg == L"--channel" && i + 2 < argc) {
                input = reinterpret_cast<HANDLE>(std::stoull(argv[++i])); output = reinterpret_cast<HANDLE>(std::stoull(argv[++i]));
            } else { Logger::Error(L"Unknown/incomplete option: " + arg); return 1; }
        }
        if (tests) { return ProtocolTests() ? 0 : 1; }
        if (producerMode) {
            if (child || !destination.empty()) { return 1; }
            return Producer(input, output);
        }
        if (destination.empty()) { Logger::Error(L"Explicit --output directory required"); return 1; }
        if (child) { return Consumer(input, output, destination, stall); }
        if (input || output) { Logger::Error(L"Denied: cannot attach a channel to Broker"); return 1; }
        if (!std::filesystem::create_directory(destination)) { Logger::Error(L"Output directory must be new"); return 1; }
        if (!ProtocolTests()) { return 1; }
        wchar_t executable[32768]{};
        const DWORD length = GetModuleFileNameW(nullptr, executable, static_cast<DWORD>(std::size(executable)));
        if (!length || length >= std::size(executable)) { Logger::Win32Error(L"GetModuleFileNameW", GetLastError()); return 1; }
        Child producer, recorder, display;
        if (!producer.Start(executable, L"--producer")) { return 1; }
        if (!recorder.Start(executable, L"--consumer --output \"" + destination.wstring() + L"\"" + (stall ? L" --stall" : L""))) { return 1; }
        if (viewer && !display.Start((std::filesystem::path(executable).parent_path() / L"SecureViewer.exe").wstring(),
            L"--shared-source --run-seconds 15 --capture-ready")) { return 1; }
        Lease lease{1, GetTickCount64() + (expired ? 0 : 30000), 0, !deny, false};
        unsigned delivered = 0;
        const auto started = GetTickCount64();
        for (unsigned frame = 1; frame <= frames; ++frame) {
            Message message; message.frame = frame; message.deadline = lease.deadline;
            if (revokeAfter && delivered == revokeAfter) { lease.revoked = true; }
            const auto decision = lease.Check(message, GetTickCount64());
            if (decision != Status::Ok) { Logger::Info(L"Lease terminal status=" + std::to_wstring(static_cast<unsigned>(decision))); break; }
            if (!Send(producer.input.get(), message)) { return 1; }
            Message produced;
            if (!Receive(producer.output.get(), produced, 2000) || produced.frame != message.frame ||
                produced.kind != Kind::Frame || produced.status != Status::Ok || produced.generation != message.generation ||
                produced.deadline != message.deadline || produced.pixelWidth != width || produced.pixelHeight != height || !produced.handle) { return 1; }
            HANDLE local = nullptr;
            if (!DuplicateHandle(producer.process.get(), reinterpret_cast<HANDLE>(static_cast<std::uintptr_t>(produced.handle)),
                GetCurrentProcess(), &local, 0, FALSE, DUPLICATE_SAME_ACCESS)) {
                Logger::Win32Error(L"DuplicateHandle(from retained producer)", GetLastError()); return 1;
            }
            Handle shared(local);
            message.adapterLow = produced.adapterLow; message.adapterHigh = produced.adapterHigh;
            Logger::Info(L"Broker frame adapter LUID=" + AdapterText(LUID{message.adapterLow, message.adapterHigh}));
            if (!Deliver(recorder, shared.get(), message) ||
                (viewer && !Deliver(display, shared.get(), message))) { return 1; }
            Message ack = message; ack.kind = Kind::Ack;
            if (!Send(producer.input.get(), ack)) { return 1; }
            lease.lastFrame = frame; ++delivered;
        }
        Message stop; stop.kind = Kind::Stop;
        if (!Send(producer.input.get(), stop) || !Send(recorder.input.get(), stop) || (viewer && !Send(display.input.get(), stop))) { return 1; }
        if (!producer.Finish() || !recorder.Finish() || (viewer && !display.Finish())) { return 1; }
        std::wcout << L"Delivered=" << delivered << L"; elapsed_ms=" << GetTickCount64() - started << L"\n";
        Logger::Info(L"Lease stopped; previous immutable snapshots remain readable; delivered=" + std::to_wstring(delivered));
        return 0;
    } catch (const std::exception&) { Logger::Error(L"Invalid argument or filesystem operation failed"); return 1; }
}
