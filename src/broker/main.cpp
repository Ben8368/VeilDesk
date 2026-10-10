#include "broker/Channel.h"
#include "broker/AuthorizationClient.h"
#include "broker/Benchmark.h"
#include "broker/SharedFrame.h"
#include "broker/FrameReadback.h"
#include "capture/CaptureEngine.h"
#include "common/Logger.h"
#include <filesystem>
#include <iostream>
#include <memory>

using namespace veil;
using namespace veil::broker;
namespace {
int Producer(HANDLE input, HANDLE output, bool exitOnFrame, Status simulatedDeviceLoss) {
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
        if (!Receive(read.get(), request, 5000, L"producer next frame")) { return 1; }
        if (request.kind == Kind::Stop) { return 0; }
        if (!lease.granted) { lease.granted = true; lease.deadline = request.deadline; lease.generation = request.generation;
            lease.pixelWidth = request.pixelWidth; lease.pixelHeight = request.pixelHeight; }
        if (lease.granted && request.generation == lease.generation && request.deadline > lease.deadline && request.frame == lease.lastFrame + 1) { lease.deadline = request.deadline; }
        if (lease.Check(request, GetTickCount64()) != Status::Ok || request.handle != 0) { return 1; }
        if (exitOnFrame) { Logger::Error(L"TEST: producer exits before delivering frame"); return 1; }
        if (simulatedDeviceLoss != Status::Ok) {
            request.kind = Kind::Denied;
            request.status = simulatedDeviceLoss;
            request.handle = 0;
            Logger::Error(simulatedDeviceLoss == Status::DeviceRemoved
                ? L"TEST: simulated DXGI_ERROR_DEVICE_REMOVED; fail closed before handle publication"
                : L"TEST: simulated DXGI_ERROR_DEVICE_RESET; fail closed before handle publication");
            Send(write.get(), request);
            return 1;
        }
        Handle frame;
        if (!gpu.Produce(request.frame, frame, request.pixelWidth, request.pixelHeight)) { return 1; }
        request.handle = reinterpret_cast<std::uintptr_t>(frame.get());
        request.adapterLow = gpu.Adapter().LowPart; request.adapterHigh = gpu.Adapter().HighPart;
        if (!Send(write.get(), request)) { return 1; }
        Message ack;
        if (!Receive(read.get(), ack, 5000, L"producer frame ACK") || ack.kind != Kind::Ack || ack.status != Status::Ok || ack.handle != 0 ||
            ack.frame != request.frame || ack.generation != request.generation || ack.deadline != request.deadline) { return 1; }
        lease.lastFrame = request.frame;
    }
}
int Consumer(HANDLE input, HANDLE output, const std::filesystem::path& destination, bool stall, bool exitOnFrame) {
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
        if (exitOnFrame) { Logger::Error(L"TEST: consumer exits after receiving frame"); return 1; }
        if (stall) { Sleep(3500); return 1; }
        if (!lease.granted) {
            lease.granted = true; lease.deadline = message.deadline; lease.generation = message.generation;
        }
        if (lease.granted && message.generation == lease.generation && message.deadline > lease.deadline && message.frame == lease.lastFrame + 1) { lease.deadline = message.deadline; }
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
    if (!child.enrolled || (child.role != ChildRole::View && child.role != ChildRole::Record) ||
        !MatchesRetainedProcess(child.process.get(), child.identity)) { return false; }
    HANDLE remote = nullptr;
    if (!DuplicateHandle(GetCurrentProcess(), shared, child.process.get(), &remote, 0, FALSE, DUPLICATE_SAME_ACCESS)) {
        Logger::Win32Error(L"DuplicateHandle(approved retained process)", GetLastError()); return false;
    }
    Message request = source;
    request.handle = reinterpret_cast<std::uintptr_t>(remote);
    if (!Send(child.input.get(), request)) { return false; } // child termination closes undelivered duplicate
    Message ack;
    if (!Receive(child.output.get(), ack, 2000, L"broker consumer/viewer ACK")) { return false; }
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
    auto revoked = lease; revoked.Revoke();
    if (revoked.generation != 2 || revoked.Check(message, 2) != Status::Revoked) { return false; }
    lease.lastFrame = 1;
    if (lease.Check(message, 2) != Status::Replay) { return false; }
    lease.lastFrame = 0; lease.revoked = true;
    if (lease.Check(message, 2) != Status::Revoked) { return false; }
    lease.revoked = false; ++message.generation;
    if (lease.Check(message, 2) != Status::Invalid) { return false; }
    --message.generation; ++message.protocol;
    if (lease.Check(message, 2) != Status::Invalid) { return false; }
    --message.protocol;
    for (unsigned i = 0; i < 64; ++i) {
        auto malformed = message;
        malformed.signature ^= (i + 1);
        if (lease.Check(malformed, 2) != Status::Invalid) { return false; }
    }
    for (auto invalidKind : {Kind::Ack, Kind::Stop, Kind::Denied, static_cast<Kind>(999)}) {
        auto malformed = message; malformed.kind = invalidKind;
        if (lease.Check(malformed, 2) != Status::Invalid) { return false; }
    }
    auto invalidStatus = message; invalidStatus.status = Status::Failed;
    if (lease.Check(invalidStatus, 2) != Status::Invalid) { return false; }
    message.pixelWidth = 0;
    if (lease.Check(message, 2) != Status::Invalid) { return false; }
    Logger::Info(L"Protocol negative cases passed (state tests, not OS identity evidence)"); return true;
}
}
int wmain(int argc, wchar_t** argv) {
    Logger::Initialize(L"Broker-" + std::to_wstring(GetCurrentProcessId()) + L".log");
    std::filesystem::path destination;
    unsigned frames = 8, revokeAfter = 0;
    BenchmarkConfig benchmarkConfig; bool benchmarkOptions = false;
    bool producerExit = false, consumerExit = false, exitOnFrame = false;
    bool crashAfterDelivery = false;
    Status simulatedDeviceLoss = Status::Ok;
    bool revokeInFlight = false;
    bool benchmark = false, child = false, producerMode = false, stall = false, tests = false, viewer = false, expired = false, deny = false;
    bool forgedEnvelope = false, downgradedScope = false, undersizedBudget = false, replayAuthorization = false;
    HANDLE input = nullptr, output = nullptr;
    std::wstring authorizationPipe;
    bool allowConsoleServicePeer = false;
    DWORD consoleServicePid = 0;
    try {
        for (int i = 1; i < argc; ++i) {
            const std::wstring arg(argv[i]);
            if (arg == L"--benchmark") { benchmark = true; viewer = true; }
            else if (arg == L"--consumer") { child = true; }
            else if (arg == L"--producer") { producerMode = true; }
            else if (arg == L"--producer-exit") { producerExit = true; }
            else if (arg == L"--consumer-exit") { consumerExit = true; }
            else if (arg == L"--exit-on-frame") { exitOnFrame = true; }
            else if (arg == L"--crash-after-delivery") { crashAfterDelivery = true; }
            else if (arg == L"--simulate-device-removed") { simulatedDeviceLoss = Status::DeviceRemoved; }
            else if (arg == L"--simulate-device-reset") { simulatedDeviceLoss = Status::DeviceReset; }
            else if (arg == L"--revoke-in-flight") { revokeInFlight = true; }
            else if (arg == L"--stall") { stall = true; }
            else if (arg == L"--protocol-tests") { tests = true; }
            else if (arg == L"--viewer") { viewer = true; }
            else if (arg == L"--expired") { expired = true; }
            else if (arg == L"--deny") { deny = true; }
            else if (arg == L"--forged-envelope") { forgedEnvelope = true; }
            else if (arg == L"--downgraded-scope") { downgradedScope = true; }
            else if (arg == L"--undersized-budget") { undersizedBudget = true; }
            else if (arg == L"--replay-authorization") { replayAuthorization = true; }
            else if (arg == L"--authorization-pipe" && i + 1 < argc) { authorizationPipe = argv[++i]; }
            else if (arg == L"--allow-console-service-peer") { allowConsoleServicePeer = true; }
            else if (arg == L"--console-service-pid" && i + 1 < argc) { consoleServicePid = static_cast<DWORD>(std::stoul(argv[++i])); }
            else if ((arg == L"--duration-seconds" || arg == L"--width" || arg == L"--height" ||
                arg == L"--fps" || arg == L"--sample-seconds") && i + 1 < argc) {
                const std::wstring value(argv[++i]); std::size_t consumed = 0;
                const auto number = std::stoul(value, &consumed);
                if (value.empty() || value.front() == L'-' || consumed != value.size() || number > 7200) { return 1; }
                benchmarkOptions = true;
                if (arg == L"--duration-seconds") { benchmarkConfig.durationSeconds = number; }
                else if (arg == L"--width") { benchmarkConfig.pixelWidth = number; }
                else if (arg == L"--height") { benchmarkConfig.pixelHeight = number; }
                else if (arg == L"--fps") { benchmarkConfig.fps = number; }
                else { benchmarkConfig.sampleSeconds = number; }
            }
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
        if (benchmark && (child || producerMode || stall || expired || deny || revokeAfter || frames != 8)) { return 1; }
        if ((benchmarkOptions && !benchmark) || !benchmarkConfig.Valid()) { Logger::Error(L"Invalid benchmark configuration"); return 1; }
        if (benchmark) { frames = benchmarkConfig.Slots(); }
        if (tests) { return ProtocolTests() ? 0 : 1; }
        if (producerMode || child) { Logger::SetExternalSinks(false); }
        if (producerMode) {
            if (child || !destination.empty()) { return 1; }
            return Producer(input, output, exitOnFrame, simulatedDeviceLoss);
        }
        if (destination.empty()) { Logger::Error(L"Explicit --output directory required"); return 1; }
        if (child) { return Consumer(input, output, destination, stall, exitOnFrame); }
        if (input || output) { Logger::Error(L"Denied: cannot attach a channel to Broker"); return 1; }
        if (!std::filesystem::create_directory(destination)) { Logger::Error(L"Output directory must be new"); return 1; }
        if (!ProtocolTests()) { return 1; }
        wchar_t executable[32768]{};
        const DWORD length = GetModuleFileNameW(nullptr, executable, static_cast<DWORD>(std::size(executable)));
        if (!length || length >= std::size(executable)) { Logger::Win32Error(L"GetModuleFileNameW", GetLastError()); return 1; }
        Child producer, recorder, display;
        std::wstring producerOptions = producerExit ? L"--producer --exit-on-frame" : L"--producer";
        if (simulatedDeviceLoss == Status::DeviceRemoved) { producerOptions += L" --simulate-device-removed"; }
        else if (simulatedDeviceLoss == Status::DeviceReset) { producerOptions += L" --simulate-device-reset"; }
        if (!producer.Start(executable, producerOptions, ChildRole::Producer)) { return 1; }
        if (!benchmark && !recorder.Start(executable, L"--consumer --output \"" + destination.wstring() + L"\"" + (stall ? L" --stall" : L"") + (consumerExit ? L" --exit-on-frame" : L""), ChildRole::Record)) { return 1; }
        if (viewer && !display.Start((std::filesystem::path(executable).parent_path() / L"SecureViewer.exe").wstring(),
            (benchmark ? L"--shared-source --run-seconds " + std::to_wstring(benchmarkConfig.durationSeconds + 30) +
                L" --benchmark-display --benchmark-size " + std::to_wstring(benchmarkConfig.pixelWidth) + L" " + std::to_wstring(benchmarkConfig.pixelHeight) : L"--shared-source --run-seconds 15 --capture-ready"), ChildRole::View)) { return 1; }
        if (authorizationPipe.empty()) {
            authorizationPipe = L"\\\\.\\pipe\\VeilDesk.SecureService.authorization.v1";
        }
        if (allowConsoleServicePeer != (consoleServicePid != 0)) { Logger::Error(L"Console peer mode requires an explicit service PID"); return 1; }
        AuthorizationClient authorization(authorizationPipe, allowConsoleServicePeer, consoleServicePid);
        if (deny) { Logger::Error(L"No Service grant requested (--deny); fail closed"); return 0; }
        const std::uint32_t pixelWidth = benchmark ? benchmarkConfig.pixelWidth : width;
        const std::uint32_t pixelHeight = benchmark ? benchmarkConfig.pixelHeight : height;
        const std::uint64_t byteCount64 = static_cast<std::uint64_t>(pixelWidth) * pixelHeight * 4;
        if (!byteCount64 || byteCount64 > authorizationMaxBytes) { Logger::Error(L"Frame byte budget outside Authorization Envelope bounds"); return 1; }
        std::uint32_t authorizationBatchFrames = 0;
        const auto issueBatch = [&](std::uint32_t remaining, std::vector<std::pair<AuthorizationEnvelope, std::uint32_t>>& issued) {
            const std::uint32_t duration = expired ? 1 : 30000;
            authorizationBatchFrames = (std::min)(remaining, authorizationMaxBytes / static_cast<std::uint32_t>(byteCount64));
            if (!authorizationBatchFrames) { return false; }
            auto byteBudget = authorizationBatchFrames * static_cast<std::uint32_t>(byteCount64);
            if (undersizedBudget) { byteBudget = static_cast<std::uint32_t>(byteCount64) - 1; }
            auto issue = [&](std::uint32_t scope) {
                AuthorizationEnvelope envelope;
                if (!authorization.Issue(downgradedScope ? 99 : scope, duration, authorizationBatchFrames, byteBudget, envelope)) { return false; }
                if (forgedEnvelope) { ++envelope.subject.pid; }
                issued.emplace_back(envelope, static_cast<std::uint32_t>(byteCount64));
                return true;
            };
            return (benchmark || issue(authorizationScopeRecord)) && (!viewer || issue(authorizationScopeView));
        };
        std::vector<std::pair<AuthorizationEnvelope, std::uint32_t>> authorizationLeases;
        if (!issueBatch(frames, authorizationLeases) || authorizationLeases.empty()) { Logger::Error(L"SecureService grant unavailable; no local fallback"); return 1; }
        Lease lease{authorizationLeases.front().first.generation, authorizationLeases.front().first.deadline, 0, true, false};
        std::uint32_t authorizationBaseFrame = 0;
        if (benchmark) { lease.pixelWidth = benchmarkConfig.pixelWidth; lease.pixelHeight = benchmarkConfig.pixelHeight; }
        SharedFrameConsumer brokerGpu;
        if (!brokerGpu.Initialize()) { return 1; }
        std::unique_ptr<Benchmark> metrics;
        if (benchmark) { metrics = std::make_unique<Benchmark>(benchmarkConfig, destination); }
        unsigned slot = 0;
        unsigned delivered = 0;
        const auto started = GetTickCount64();
        for (unsigned frame = 1; frame <= frames; ++frame) {
            if (benchmark) {
                slot = (std::max)(slot, metrics->ElapsedSlots());
                if (!metrics->Pace(slot++)) { break; }
                if (!metrics->Sample(producer.process.get(), display.process.get())) { return 1; }
            }
            const auto now = GetTickCount64();
            if (((frame - authorizationBaseFrame) > authorizationBatchFrames || now + 5000 >= lease.deadline) && frame > 1) {
                authorizationLeases.clear();
                if (!issueBatch(frames - frame + 1, authorizationLeases) || authorizationLeases.empty()) { Logger::Error(L"SecureService lease renewal failed; stopping delivery"); break; }
                lease.generation = authorizationLeases.front().first.generation;
                lease.deadline = authorizationLeases.front().first.deadline;
                lease.lastFrame = frame - 1;
                authorizationBaseFrame = frame - 1;
            }
            if (replayAuthorization && delivered == 1 && frame == 2) {
                if (authorization.ConsumeFrame(authorizationLeases, frame - 1 - authorizationBaseFrame)) {
                    Logger::Error(L"SecureService accepted a replayed frame authorization"); return 1;
                }
                Logger::Info(L"SecureService rejected replayed frame authorization before delivery"); break;
            }
            const auto deliveryStart = Benchmark::Clock::now();
            Message message; message.frame = frame; message.deadline = lease.deadline; message.generation = lease.generation;
            message.pixelWidth = lease.pixelWidth; message.pixelHeight = lease.pixelHeight;
            if (revokeAfter && delivered == revokeAfter && !authorization.RevokeAll()) { Logger::Error(L"SecureService revoke failed; stopping delivery"); break; }
            bool consumed = false;
            if (expired && frame == 1) {
                Sleep(10);
                consumed = authorization.ConsumeFrame(authorizationLeases, frame - authorizationBaseFrame);
                if (!consumed) { Logger::Info(L"SecureService rejected expired frame authorization; no resource delivered"); break; }
            }
            const auto decision = lease.Check(message, GetTickCount64());
            if (decision != Status::Ok) { Logger::Info(L"Lease terminal status=" + std::to_wstring(static_cast<unsigned>(decision))); break; }
            if (!consumed && !authorization.ConsumeFrame(authorizationLeases, frame - authorizationBaseFrame)) {
                Logger::Info(L"SecureService rejected frame authorization; no resource delivered");
                break;
            }
            if (revokeInFlight) {
                revokeInFlight = false;
                if (!authorization.RevokeAll()) { Logger::Error(L"SecureService in-flight revoke failed"); return 1; }
                Logger::Info(L"Revoke completed after frame authorization was consumed and before resource production; this authorized in-flight frame may complete, subsequent frames are denied");
            }
            if (!Send(producer.input.get(), message)) { return 1; }
            Message produced;
            if (!Receive(producer.output.get(), produced, 2000, L"broker produced frame")) { return 1; }
            if (produced.kind == Kind::Denied &&
                (produced.status == Status::DeviceRemoved || produced.status == Status::DeviceReset)) {
                Logger::Error(L"Producer reported device loss status=" + std::to_wstring(static_cast<unsigned>(produced.status)) + L"; no shared resource delivered");
                return 1;
            }
            if (produced.frame != message.frame || produced.kind != Kind::Frame || produced.status != Status::Ok || produced.generation != message.generation ||
                produced.deadline != message.deadline || produced.pixelWidth != message.pixelWidth || produced.pixelHeight != message.pixelHeight || !produced.handle) { return 1; }
            if (benchmark) { metrics->Produced(); metrics->Record(Benchmark::Stage::Produce, deliveryStart); }
            const auto inspectStart = Benchmark::Clock::now();
            HANDLE local = nullptr;
            if (!DuplicateHandle(producer.process.get(), reinterpret_cast<HANDLE>(static_cast<std::uintptr_t>(produced.handle)),
                GetCurrentProcess(), &local, 0, FALSE, DUPLICATE_SAME_ACCESS)) {
                Logger::Win32Error(L"DuplicateHandle(from retained producer)", GetLastError()); return 1;
            }
            Handle shared(local);
            message.adapterLow = produced.adapterLow; message.adapterHigh = produced.adapterHigh;
            if (frame == 1) { Logger::Info(L"Broker frame adapter LUID=" + AdapterText(LUID{message.adapterLow, message.adapterHigh})); }
            HANDLE inspection = nullptr;
            if (!DuplicateHandle(GetCurrentProcess(), shared.get(), GetCurrentProcess(), &inspection, 0, FALSE, DUPLICATE_SAME_ACCESS)) {
                Logger::Win32Error(L"DuplicateHandle(Broker resource validation)", GetLastError()); return 1;
            }
            Message inspected = message; inspected.handle = reinterpret_cast<std::uintptr_t>(inspection);
            SharedTexture snapshot;
            if (!brokerGpu.OpenFrame(inspected, snapshot) || !snapshot.Close()) { return 1; }
            if (benchmark) { metrics->Record(Benchmark::Stage::Inspect, inspectStart); }
            const auto viewStart = Benchmark::Clock::now();
            if ((!benchmark && !Deliver(recorder, shared.get(), message)) ||
                (viewer && !Deliver(display, shared.get(), message))) { return 1; }
            if (benchmark) { metrics->Record(Benchmark::Stage::View, viewStart); }
            Message ack = message; ack.kind = Kind::Ack;
            if (!Send(producer.input.get(), ack)) { return 1; }
            lease.lastFrame = frame; ++delivered;
            if (benchmark) { metrics->Delivered(deliveryStart); }
            if (crashAfterDelivery) {
                Logger::Error(L"TEST: terminating Broker process after delivered frame to exercise crash cleanup");
                TerminateProcess(GetCurrentProcess(), 0xDEAD);
                return 1;
            }
        }
        if (benchmark) {
            metrics->Pace(benchmarkConfig.Slots());
            if (!metrics->Sample(producer.process.get(), display.process.get(), true) || !metrics->Save(AdapterText(brokerGpu.Adapter()))) { return 1; }
        }
        Message stop; stop.kind = Kind::Stop; stop.generation = lease.generation;
        if (!Send(producer.input.get(), stop) || (!benchmark && !Send(recorder.input.get(), stop)) || (viewer && !Send(display.input.get(), stop))) { return 1; }
        if (!producer.Finish() || (!benchmark && !recorder.Finish()) || (viewer && !display.Finish())) { return 1; }
        std::wcout << L"Delivered=" << delivered << L"; elapsed_ms=" << GetTickCount64() - started << L"\n";
        Logger::Info(L"Lease stopped; previous immutable snapshots remain readable; delivered=" + std::to_wstring(delivered));
        return 0;
    } catch (const std::exception&) { Logger::Error(L"Invalid argument or filesystem operation failed"); return 1; }
}
