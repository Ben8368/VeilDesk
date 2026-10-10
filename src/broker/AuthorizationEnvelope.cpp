#include "broker/AuthorizationEnvelope.h"
#include <algorithm>
#include <limits>
#include <type_traits>

namespace veil::broker {
namespace {
template <typename T>
bool ReadLittleEndian(std::span<const std::uint8_t> bytes, std::size_t& offset, T& value) noexcept {
    static_assert(std::is_unsigned_v<T>);
    if (offset > bytes.size() || sizeof(T) > bytes.size() - offset) { return false; }
    std::uint64_t result = 0;
    for (std::size_t index = 0; index < sizeof(T); ++index) {
        result |= static_cast<std::uint64_t>(bytes[offset + index]) << (index * 8);
    }
    value = static_cast<T>(result);
    offset += sizeof(T);
    return true;
}
bool ReadArray(std::span<const std::uint8_t> bytes, std::size_t& offset,
    std::array<std::uint8_t, 32>& value) noexcept {
    if (offset > bytes.size() || value.size() > bytes.size() - offset) { return false; }
    std::copy_n(bytes.begin() + static_cast<std::ptrdiff_t>(offset), value.size(), value.begin());
    offset += value.size();
    return true;
}
template <typename T>
bool Read(std::span<const std::uint8_t> bytes, std::size_t& offset, T& value) noexcept {
    return ReadLittleEndian(bytes, offset, value);
}
template <typename T>
void AppendLittleEndian(std::vector<std::uint8_t>& bytes, T value) {
    static_assert(std::is_unsigned_v<T>);
    for (std::size_t index = 0; index < sizeof(T); ++index) { bytes.push_back(static_cast<std::uint8_t>(value >> (index * 8))); }
}
}

bool ValidAuthorizationEnvelope(const AuthorizationEnvelope& envelope) noexcept {
    const bool roleScopeValid =
        envelope.subject.role == authorizationRoleBroker &&
        (envelope.scope == authorizationScopeView || envelope.scope == authorizationScopeRecord);
    return envelope.leaseId != 0 && envelope.epoch != 0 &&
        envelope.epoch != (std::numeric_limits<std::uint64_t>::max)() &&
        envelope.generation != 0 && envelope.subject.pid != 0 && envelope.subject.session != 0 &&
        envelope.subject.creationTime != 0 &&
        std::any_of(envelope.subject.sidHash.begin(), envelope.subject.sidHash.end(), [](auto b) { return b != 0; }) &&
        std::any_of(envelope.subject.imageHash.begin(), envelope.subject.imageHash.end(), [](auto b) { return b != 0; }) &&
        roleScopeValid && envelope.issuedAt < envelope.deadline && envelope.nonce != 0 &&
        envelope.maxFrames != 0 && envelope.maxFrames <= authorizationMaxFrames &&
        envelope.maxBytes != 0 && envelope.maxBytes <= authorizationMaxBytes;
}

std::vector<std::uint8_t> EncodeAuthorizationEnvelope(const AuthorizationEnvelope& e) {
    std::vector<std::uint8_t> bytes; bytes.reserve(authorizationEnvelopeWireSize);
    AppendLittleEndian(bytes, authorizationEnvelopeMagic); AppendLittleEndian(bytes, authorizationEnvelopeVersion);
    AppendLittleEndian(bytes, static_cast<std::uint16_t>(authorizationEnvelopeWireSize));
    AppendLittleEndian(bytes, e.leaseId); AppendLittleEndian(bytes, e.epoch); AppendLittleEndian(bytes, e.generation);
    AppendLittleEndian(bytes, e.subject.pid); AppendLittleEndian(bytes, e.subject.session); AppendLittleEndian(bytes, e.subject.creationTime);
    bytes.insert(bytes.end(), e.subject.sidHash.begin(), e.subject.sidHash.end());
    bytes.insert(bytes.end(), e.subject.imageHash.begin(), e.subject.imageHash.end());
    AppendLittleEndian(bytes, e.subject.role); AppendLittleEndian(bytes, e.scope); AppendLittleEndian(bytes, e.issuedAt);
    AppendLittleEndian(bytes, e.deadline); AppendLittleEndian(bytes, e.nonce); AppendLittleEndian(bytes, e.maxFrames); AppendLittleEndian(bytes, e.maxBytes);
    return bytes;
}

EnvelopeDecodeError DecodeAuthorizationEnvelope(
    std::span<const std::uint8_t> bytes, AuthorizationEnvelope& envelope) noexcept {
    if (bytes.size() != authorizationEnvelopeWireSize) { return EnvelopeDecodeError::InvalidSize; }
    std::size_t offset = 0;
    std::uint32_t magic = 0;
    std::uint16_t version = 0, wireSize = 0;
    if (!Read(bytes, offset, magic) || !Read(bytes, offset, version) || !Read(bytes, offset, wireSize) ||
        magic != authorizationEnvelopeMagic || version != authorizationEnvelopeVersion ||
        wireSize != authorizationEnvelopeWireSize) { return EnvelopeDecodeError::InvalidHeader; }

    AuthorizationEnvelope result;
    if (!Read(bytes, offset, result.leaseId) || !Read(bytes, offset, result.epoch) ||
        !Read(bytes, offset, result.generation) || !Read(bytes, offset, result.subject.pid) ||
        !Read(bytes, offset, result.subject.session) || !Read(bytes, offset, result.subject.creationTime) ||
        !ReadArray(bytes, offset, result.subject.sidHash) || !ReadArray(bytes, offset, result.subject.imageHash) ||
        !Read(bytes, offset, result.subject.role) || !Read(bytes, offset, result.scope) ||
        !Read(bytes, offset, result.issuedAt) || !Read(bytes, offset, result.deadline) ||
        !Read(bytes, offset, result.nonce) || !Read(bytes, offset, result.maxFrames) ||
        !Read(bytes, offset, result.maxBytes) || offset != bytes.size()) {
        return EnvelopeDecodeError::InvalidSize;
    }
    if (!ValidAuthorizationEnvelope(result)) { return EnvelopeDecodeError::InvalidField; }
    envelope = result;
    return EnvelopeDecodeError::None;
}
}
