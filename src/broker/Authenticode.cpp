#include "broker/ProcessIdentity.h"
#include "common/Logger.h"
#include <Windows.h>
#include <Softpub.h>
#include <Wincrypt.h>
#include <Wintrust.h>
#include <bcrypt.h>
#include <cwctype>
#include <iomanip>
#include <memory>
#include <sstream>
#include <vector>

namespace veil::broker {
namespace {
struct WinTrustCloser {
    GUID* action;
    WINTRUST_DATA* data;
    ~WinTrustCloser() {
        data->dwStateAction = WTD_STATEACTION_CLOSE;
        const LONG status = WinVerifyTrust(nullptr, action, data);
        if (status != ERROR_SUCCESS) {
            std::wostringstream message;
            message << L"Authenticode WinVerifyTrust state close returned 0x" << std::uppercase << std::hex
                << static_cast<unsigned long>(status);
            Logger::Error(message.str());
        }
    }
};
std::wstring Sha256Thumbprint(PCCERT_CONTEXT certificate) {
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0) { return {}; }
    struct AlgorithmCloser { BCRYPT_ALG_HANDLE value; ~AlgorithmCloser() { BCryptCloseAlgorithmProvider(value, 0); } } close{algorithm};
    DWORD size = 0, returned = 0;
    if (BCryptGetProperty(algorithm, BCRYPT_OBJECT_LENGTH, reinterpret_cast<PUCHAR>(&size), sizeof(size), &returned, 0) < 0 ||
        returned != sizeof(size) || !size || size > 4096) { return {}; }
    std::vector<BYTE> object(size);
    BCRYPT_HASH_HANDLE hash = nullptr;
    if (BCryptCreateHash(algorithm, &hash, object.data(), size, nullptr, 0, 0) < 0) { return {}; }
    struct HashCloser { BCRYPT_HASH_HANDLE value; ~HashCloser() { BCryptDestroyHash(value); } } hashClose{hash};
    if (BCryptHashData(hash, certificate->pbCertEncoded, certificate->cbCertEncoded, 0) < 0) { return {}; }
    BYTE digest[32]{};
    if (BCryptFinishHash(hash, digest, sizeof(digest), 0) < 0) { return {}; }
    std::wostringstream output;
    output << std::uppercase << std::hex << std::setfill(L'0');
    for (BYTE value : digest) { output << std::setw(2) << static_cast<unsigned>(value); }
    return output.str();
}
PCCERT_CONTEXT ReadVerifiedSigner(WINTRUST_DATA& data) {
    if (!data.hWVTStateData) { return nullptr; }
    auto provider = WTHelperProvDataFromStateData(data.hWVTStateData);
    if (!provider) { return nullptr; }
    auto signer = WTHelperGetProvSignerFromChain(provider, 0, FALSE, 0);
    if (!signer || !signer->csCertChain || !signer->pasCertChain) { return nullptr; }
    return signer->pasCertChain[0].pCert;
}
}

bool VerifyAuthenticodePublisher(const std::wstring& imagePath, const std::wstring& expectedThumbprint,
    std::wstring& signerThumbprint, PublisherState& state, LONG* trustStatus) {
    signerThumbprint.clear(); state = PublisherState::NotVerified;
    WINTRUST_FILE_INFO file{}; file.cbStruct = sizeof(file); file.pcwszFilePath = imagePath.c_str();
    WINTRUST_DATA data{}; data.cbStruct = sizeof(data); data.dwUIChoice = WTD_UI_NONE;
    data.fdwRevocationChecks = WTD_REVOKE_WHOLECHAIN; data.dwUnionChoice = WTD_CHOICE_FILE; data.pFile = &file;
    data.dwStateAction = WTD_STATEACTION_VERIFY;
    data.dwProvFlags = WTD_REVOCATION_CHECK_CHAIN_EXCLUDE_ROOT | WTD_CACHE_ONLY_URL_RETRIEVAL;
    GUID action = WINTRUST_ACTION_GENERIC_VERIFY_V2;
    LONG status = WinVerifyTrust(nullptr, &action, &data);
    WinTrustCloser cleanup{&action, &data};
    if (trustStatus) { *trustStatus = status; }
    if (status != ERROR_SUCCESS) {
        state = (status == TRUST_E_NOSIGNATURE || status == TRUST_E_SUBJECT_FORM_UNKNOWN)
            ? PublisherState::Unsigned : PublisherState::TrustRejected;
        std::wostringstream message;
        message << L"Location: Authenticode; Cause: WinVerifyTrust returned 0x" << std::uppercase << std::hex
            << static_cast<unsigned long>(status) << L"; Action: deny publisher enrollment";
        Logger::Error(message.str());
        return false;
    }
    PCCERT_CONTEXT signer = ReadVerifiedSigner(data);
    if (!signer) { state = PublisherState::TrustRejected; Logger::Error(L"Authenticode verified file has no primary signer certificate; Action: deny"); return false; }
    signerThumbprint = Sha256Thumbprint(signer);
    if (signerThumbprint.empty()) { state = PublisherState::TrustRejected; Logger::Error(L"Authenticode signer SHA-256 calculation failed; Action: deny"); return false; }
    std::wstring expected = expectedThumbprint;
    for (auto& ch : expected) { ch = static_cast<wchar_t>(towupper(ch)); }
    if (expected.size() != 64 || signerThumbprint != expected) {
        state = PublisherState::PublisherMismatch;
        Logger::Error(L"Authenticode signer certificate SHA-256 does not match enrolled publisher pin; Action: deny");
        return false;
    }
    state = PublisherState::Verified;
    return true;
}
}
