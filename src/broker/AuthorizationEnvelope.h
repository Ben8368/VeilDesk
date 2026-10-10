#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace veil::broker {
inline constexpr std::size_t authorizationEnvelopeWireSize = 152;
inline constexpr std::uint32_t authorizationEnvelopeMagic = 0x56444145;
inline constexpr std::uint16_t authorizationEnvelopeVersion = 1;
inline constexpr std::uint32_t authorizationMaxFrames = 1'728'000;
inline constexpr std::uint32_t authorizationMaxBytes = 1'073'741'824;
inline constexpr std::uint32_t authorizationScopeView = 1;
inline constexpr std::uint32_t authorizationScopeRecord = 2;
inline constexpr std::uint32_t authorizationRoleBroker = 3;

struct AuthorizationSubject {
    std::uint32_t pid = 0;
    std::uint32_t session = 0;
    std::uint64_t creationTime = 0;
    std::array<std::uint8_t, 32> sidHash{};
    std::array<std::uint8_t, 32> imageHash{};
    std::uint32_t role = 0;
};

struct AuthorizationEnvelope {
    std::uint64_t leaseId = 0;
    std::uint64_t epoch = 0;
    std::uint64_t generation = 0;
    AuthorizationSubject subject;
    std::uint32_t scope = 0;
    std::uint64_t issuedAt = 0;
    std::uint64_t deadline = 0;
    std::uint64_t nonce = 0;
    std::uint32_t maxFrames = 0;
    std::uint32_t maxBytes = 0;
};

enum class EnvelopeDecodeError { None, InvalidSize, InvalidHeader, InvalidField };

// Decoding validates a canonical payload only. Callers must authenticate the Service IPC
// peer and compare this payload against current Service state before authorizing delivery.
EnvelopeDecodeError DecodeAuthorizationEnvelope(
    std::span<const std::uint8_t> bytes, AuthorizationEnvelope& envelope) noexcept;
bool ValidAuthorizationEnvelope(const AuthorizationEnvelope& envelope) noexcept;
std::vector<std::uint8_t> EncodeAuthorizationEnvelope(const AuthorizationEnvelope& envelope);
}
