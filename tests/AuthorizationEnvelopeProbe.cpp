#include "broker/AuthorizationEnvelope.h"
#include <fstream>
#include <cctype>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>

namespace {
int HexDigit(char value) {
    if (value >= '0' && value <= '9') { return value - '0'; }
    if (value >= 'a' && value <= 'f') { return value - 'a' + 10; }
    if (value >= 'A' && value <= 'F') { return value - 'A' + 10; }
    return -1;
}
bool ReadFixture(const wchar_t* path, std::vector<std::uint8_t>& bytes) {
    std::ifstream input(path, std::ios::binary);
    if (!input) { return false; }
    std::string text((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    int high = -1;
    for (const char value : text) {
        if (std::isspace(static_cast<unsigned char>(value))) { continue; }
        const int digit = HexDigit(value);
        if (digit < 0) { return false; }
        if (high < 0) { high = digit; }
        else { bytes.push_back(static_cast<std::uint8_t>((high << 4) | digit)); high = -1; }
    }
    return high < 0;
}
bool ExpectInvalid(const std::vector<std::uint8_t>& bytes, veil::broker::EnvelopeDecodeError expected) {
    veil::broker::AuthorizationEnvelope envelope;
    return veil::broker::DecodeAuthorizationEnvelope(bytes, envelope) == expected;
}
}

int wmain(int argc, wchar_t** argv) {
    using namespace veil::broker;
    if (argc != 2) { std::wcerr << L"Usage: AuthorizationEnvelopeProbe <fixture.hex>\n"; return 2; }
    std::vector<std::uint8_t> bytes;
    if (!ReadFixture(argv[1], bytes)) { std::wcerr << L"Fixture read/hex decode failed\n"; return 1; }
    AuthorizationEnvelope envelope;
    if (DecodeAuthorizationEnvelope(bytes, envelope) != EnvelopeDecodeError::None ||
        envelope.leaseId != 7 || envelope.epoch != 11 || envelope.generation != 13 ||
        envelope.subject.pid != 17 || envelope.subject.session != 19 ||
        envelope.subject.creationTime != 23 || envelope.subject.role != authorizationRoleBroker ||
        envelope.scope != authorizationScopeView || envelope.issuedAt != 29 || envelope.deadline != 31 ||
        envelope.nonce != 37 || envelope.maxFrames != 41 || envelope.maxBytes != 43) {
        std::wcerr << L"Canonical fixture decode failed\n"; return 1;
    }
    if (!ExpectInvalid(std::vector<std::uint8_t>(bytes.begin(), bytes.end() - 1), EnvelopeDecodeError::InvalidSize)) { return 1; }
    auto malformed = bytes; malformed[4] = 2;
    if (!ExpectInvalid(malformed, EnvelopeDecodeError::InvalidHeader)) { return 1; }
    malformed = bytes; malformed[116] = 99;
    if (!ExpectInvalid(malformed, EnvelopeDecodeError::InvalidField)) { return 1; }
    malformed = bytes; malformed[144] = 0; // zero max_frames
    if (!ExpectInvalid(malformed, EnvelopeDecodeError::InvalidField)) { return 1; }
    std::wcout << L"Authorization envelope shared fixture and malformed cases passed.\n";
    return 0;
}
