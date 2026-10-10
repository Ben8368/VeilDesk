#include "broker/AuthorizationClient.h"
#include "broker/ProcessIdentity.h"
#include "broker/Handle.h"
#include "common/Logger.h"
#include <bcrypt.h>
#include <filesystem>
#include <limits>

namespace veil::broker {
namespace {
constexpr std::uint32_t magic = 0x56544155;
constexpr std::uint16_t version = 1;
constexpr std::uint16_t issueKind = 1;
constexpr std::uint16_t consumeKind = 2;
constexpr std::uint16_t revokeKind = 3;
constexpr std::size_t headerSize = 16;
void Put16(std::vector<std::uint8_t>& out, std::uint16_t value) { out.push_back(static_cast<std::uint8_t>(value)); out.push_back(static_cast<std::uint8_t>(value >> 8)); }
void Put32(std::vector<std::uint8_t>& out, std::uint32_t value) { for (unsigned i = 0; i < 4; ++i) { out.push_back(static_cast<std::uint8_t>(value >> (i * 8))); } }
std::uint32_t Get32(const std::uint8_t* p) { return static_cast<std::uint32_t>(p[0]) | (static_cast<std::uint32_t>(p[1]) << 8) | (static_cast<std::uint32_t>(p[2]) << 16) | (static_cast<std::uint32_t>(p[3]) << 24); }
bool Hash(const void* data, ULONG bytes, std::array<std::uint8_t, 32>& digest) {
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    NTSTATUS status = BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0);
    if (status < 0) { Logger::Error(L"BCryptOpenAlgorithmProvider authorization peer; NTSTATUS=" + std::to_wstring(static_cast<unsigned long>(status))); return false; }
    status = BCryptHash(algorithm, nullptr, 0, static_cast<PUCHAR>(const_cast<void*>(data)), bytes, digest.data(), static_cast<ULONG>(digest.size()));
    const NTSTATUS closeStatus = BCryptCloseAlgorithmProvider(algorithm, 0);
    if (status < 0 || closeStatus < 0) { Logger::Error(L"BCryptHash authorization identity failed; NTSTATUS=" + std::to_wstring(static_cast<unsigned long>(status < 0 ? status : closeStatus))); return false; }
    return true;
}
bool SubjectMatchesSelf(const AuthorizationSubject& subject) {
    ProcessIdentity identity;
    if (!ReadProcessIdentity(GetCurrentProcess(), identity)) { return false; }
    std::array<std::uint8_t, 32> sidHash{}, imageHash{};
    if (!Hash(identity.userSid.data(), static_cast<ULONG>(identity.userSid.size() * sizeof(wchar_t)), sidHash) ||
        !Hash(identity.imagePath.data(), static_cast<ULONG>(identity.imagePath.size() * sizeof(wchar_t)), imageHash)) { return false; }
    return subject.pid == identity.pid && subject.creationTime == identity.creationTime && subject.session == identity.sessionId &&
        subject.sidHash == sidHash && subject.imageHash == imageHash && subject.role == authorizationRoleBroker;
}
bool ReadExact(HANDLE pipe, void* output, DWORD size) {
    DWORD read = 0;
    return ReadFile(pipe, output, size, &read, nullptr) && read == size;
}
}

AuthorizationClient::AuthorizationClient(std::wstring pipeName, bool allowConsolePeer, DWORD consolePeerPid)
    : pipeName_(std::move(pipeName)), allowConsolePeer_(allowConsolePeer), consolePeerPid_(consolePeerPid) {
    if (!ReadProcessIdentity(GetCurrentProcess(), selfIdentity_) || !VerifyProcessPublisher(selfIdentity_)) {
        Logger::Error(L"Authorization Broker publisher verification failed; authorization disabled");
    }
}

bool AuthorizationClient::VerifyServer(HANDLE pipe) {
    ULONG pid = 0;
    if (!GetNamedPipeServerProcessId(pipe, &pid) || !pid) { Logger::Win32Error(L"GetNamedPipeServerProcessId; authorization denied", GetLastError()); return false; }
    if (allowConsolePeer_ && (!consolePeerPid_ || pid != consolePeerPid_)) {
        Logger::Error(L"Authorization console peer PID mismatch; denied"); return false;
    }
    ProcessIdentity peer;
    if (verifiedServerProcess_ && verifiedServerIdentity_.pid == pid &&
        MatchesRetainedProcess(verifiedServerProcess_.get(), verifiedServerIdentity_)) {
        peer = verifiedServerIdentity_;
    } else {
        HANDLE raw = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
        if (!raw) { Logger::Win32Error(L"OpenProcess(SecureService peer)", GetLastError()); return false; }
        Handle process(raw);
        if (!ReadProcessIdentity(process.get(), peer) || !VerifyProcessPublisher(peer) ||
            peer.publisherState != PublisherState::Verified || peer.publisher.empty()) {
            Logger::Error(L"Authorization Service publisher verification failed; denied"); return false;
        }
        verifiedServerIdentity_ = peer;
        verifiedServerProcess_ = std::move(process);
    }
    if (allowConsolePeer_) {
        const bool matches = peer.sessionId == selfIdentity_.sessionId && peer.userSid == selfIdentity_.userSid &&
            selfIdentity_.publisherState == PublisherState::Verified && peer.publisher == selfIdentity_.publisher &&
            CompareStringOrdinal(std::filesystem::path(peer.imagePath).filename().c_str(), -1, L"veil-secure-service.exe", -1, TRUE) == CSTR_EQUAL;
        if (!matches) { Logger::Error(L"Authorization console Service publisher/identity mismatch; denied"); }
        return matches;
    }
    wchar_t programFiles[32768]{};
    const DWORD length = GetEnvironmentVariableW(L"ProgramFiles", programFiles, static_cast<DWORD>(std::size(programFiles)));
    if (!length || length >= std::size(programFiles)) { return false; }
    const auto expected = (std::filesystem::path(programFiles) / L"VeilDesk" / L"SecureService" / L"veil-secure-service.exe").wstring();
    if (peer.userSid != L"S-1-5-19" || !SameImage(peer.imagePath, expected) ||
        selfIdentity_.publisherState != PublisherState::Verified || peer.publisher != selfIdentity_.publisher) {
        Logger::Error(L"Authorization server publisher/identity/path mismatch; denied"); return false;
    }
    return true;
}

bool AuthorizationClient::Connect() {
    if (pipe_) { return true; }
    if (!WaitNamedPipeW(pipeName_.c_str(), 2000)) { Logger::Win32Error(L"WaitNamedPipeW(SecureService authorization)", GetLastError()); return false; }
    pipe_.reset(CreateFileW(pipeName_.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
    if (!pipe_) { Logger::Win32Error(L"CreateFileW(SecureService authorization)", GetLastError()); return false; }
    if (!VerifyServer(pipe_.get())) { pipe_.reset(); return false; }
    DWORD mode = PIPE_READMODE_MESSAGE;
    if (!SetNamedPipeHandleState(pipe_.get(), &mode, nullptr, nullptr)) { Logger::Win32Error(L"SetNamedPipeHandleState(authorization)", GetLastError()); pipe_.reset(); return false; }
    return true;
}

bool AuthorizationClient::Exchange(std::uint16_t kind, const std::vector<std::uint8_t>& payload, std::vector<std::uint8_t>& body) {
    if (payload.size() > 336 - headerSize) { Logger::Error(L"Authorization request exceeds protocol bound"); return false; }
    if (!Connect()) { return false; }
    if (!requestId_) {
        if (BCryptGenRandom(nullptr, reinterpret_cast<PUCHAR>(&requestId_), sizeof(requestId_), BCRYPT_USE_SYSTEM_PREFERRED_RNG) < 0 || !requestId_) { Logger::Error(L"BCryptGenRandom authorization request id failed"); return false; }
    } else { ++requestId_; if (!requestId_) { ++requestId_; } }
    std::vector<std::uint8_t> request;
    Put32(request, magic); Put16(request, version); Put16(request, kind); Put32(request, static_cast<std::uint32_t>(headerSize + payload.size())); Put32(request, requestId_);
    request.insert(request.end(), payload.begin(), payload.end());
    DWORD written = 0;
    if (!WriteFile(pipe_.get(), request.data(), static_cast<DWORD>(request.size()), &written, nullptr) || written != request.size()) { Logger::Win32Error(L"WriteFile(authorization request)", GetLastError()); pipe_.reset(); return false; }
    std::array<std::uint8_t, headerSize + 4 + authorizationEnvelopeWireSize> responseBuffer{};
    DWORD responseRead = 0;
    if (!ReadFile(pipe_.get(), responseBuffer.data(), static_cast<DWORD>(responseBuffer.size()), &responseRead, nullptr)) { Logger::Win32Error(L"ReadFile(authorization response)", GetLastError()); pipe_.reset(); return false; }
    const std::span<const std::uint8_t> header(responseBuffer.data(), responseRead);
    if (responseRead < headerSize) { Logger::Error(L"Authorization response truncated; denied"); pipe_.reset(); return false; }
    const auto responseSize = Get32(header.data() + 8);
    if (Get32(header.data()) != magic || header[4] != 1 || header[5] != 0 || Get32(header.data() + 12) != requestId_ ||
        responseSize < headerSize + 4 || responseSize > headerSize + 4 + authorizationEnvelopeWireSize ||
        responseSize != responseRead ||
        (static_cast<std::uint16_t>(header[6] | (header[7] << 8)) != static_cast<std::uint16_t>(kind | 0x8000))) {
        Logger::Error(L"Authorization response protocol mismatch; denied"); pipe_.reset(); return false;
    }
    const std::span<const std::uint8_t> response(responseBuffer.data() + headerSize, responseSize - headerSize);
    if (Get32(response.data()) != 0) { Logger::Error(L"SecureService denied authorization request; status=" + std::to_wstring(Get32(response.data()))); return false; }
    body.assign(response.begin() + 4, response.end());
    return true;
}

bool AuthorizationClient::Issue(std::uint32_t scope, std::uint32_t durationMs, std::uint32_t maxFrames,
    std::uint32_t maxBytes, AuthorizationEnvelope& envelope) {
    std::vector<std::uint8_t> payload; Put32(payload, scope); Put32(payload, durationMs); Put32(payload, maxFrames); Put32(payload, maxBytes);
    std::vector<std::uint8_t> body;
    if (!Exchange(issueKind, payload, body) || body.size() != authorizationEnvelopeWireSize) { return false; }
    if (DecodeAuthorizationEnvelope(body, envelope) != EnvelopeDecodeError::None || !SubjectMatchesSelf(envelope.subject)) {
        Logger::Error(L"Service returned envelope for a different Broker subject; denied"); return false;
    }
    return true;
}

bool AuthorizationClient::ConsumeFrame(const std::vector<std::pair<AuthorizationEnvelope, std::uint32_t>>& leases, std::uint32_t frame) {
    if (leases.empty() || leases.size() > 2) { return false; }
    std::vector<std::uint8_t> payload; Put32(payload, frame); Put32(payload, static_cast<std::uint32_t>(leases.size()));
    for (const auto& [envelope, bytes] : leases) {
        if (!ValidAuthorizationEnvelope(envelope) || !bytes) { return false; }
        const auto wire = EncodeAuthorizationEnvelope(envelope);
        payload.insert(payload.end(), wire.begin(), wire.end()); Put32(payload, bytes);
    }
    std::vector<std::uint8_t> body;
    return Exchange(consumeKind, payload, body) && body.empty();
}
bool AuthorizationClient::RevokeAll() {
    std::vector<std::uint8_t> body;
    return Exchange(revokeKind, {}, body) && body.empty();
}
}
