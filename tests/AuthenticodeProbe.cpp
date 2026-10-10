#include "broker/ProcessIdentity.h"
#include "common/Logger.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <vector>

using namespace veil::broker;

int wmain(int argc, wchar_t** argv) {
    veil::Logger::Initialize(L"AuthenticodeProbe.log");
    const auto fail = [](const wchar_t* stage, LONG trust) { std::wcerr << L"AuthenticodeProbe failed: " << stage << L" trust=0x" << std::hex << static_cast<unsigned long>(trust) << L"\n"; return 1; };
    if (argc != 2) { return fail(L"expected signed sample path", 0); }
    const std::wstring signedImage = argv[1];
    wchar_t self[MAX_PATH]{};
    const DWORD selfLength = GetModuleFileNameW(nullptr, self, static_cast<DWORD>(std::size(self)));
    if (!selfLength || selfLength >= std::size(self)) { return fail(L"GetModuleFileNameW", 0); }
    std::wstring thumbprint; PublisherState state{}; LONG trustStatus = ERROR_SUCCESS;
    if (VerifyAuthenticodePublisher(signedImage, L"", thumbprint, state, &trustStatus) ||
        state != PublisherState::PublisherMismatch || thumbprint.size() != 64) {
        std::wcerr << L"state=" << static_cast<unsigned>(state) << L" thumbprint_length=" << thumbprint.size() << L"\n";
        return fail(L"signed image certificate extraction", trustStatus);
    }
    const std::wstring expected = thumbprint;
    if (!VerifyAuthenticodePublisher(signedImage, expected, thumbprint, state, &trustStatus) || state != PublisherState::Verified) { return fail(L"signed image exact pin", trustStatus); }
    std::wstring wrong = thumbprint; wrong[0] = wrong[0] == L'0' ? L'1' : L'0';
    std::wstring actual;
    if (VerifyAuthenticodePublisher(signedImage, wrong, actual, state, &trustStatus) || state != PublisherState::PublisherMismatch) { return fail(L"wrong publisher pin", trustStatus); }
    if (VerifyAuthenticodePublisher(self, thumbprint, actual, state, &trustStatus) ||
        (state != PublisherState::Unsigned && state != PublisherState::TrustRejected)) { return fail(L"unsigned image", trustStatus); }
    const auto tampered = std::filesystem::current_path() / L"Authenticode-tampered.exe";
    std::error_code error;
    std::filesystem::copy_file(signedImage, tampered, std::filesystem::copy_options::overwrite_existing, error);
    if (error) { return fail(L"copy signed image", 0); }
    {
        std::fstream image(tampered, std::ios::binary | std::ios::in | std::ios::out);
        if (!image) { std::filesystem::remove(tampered, error); return fail(L"open tampered copy", 0); }
        image.seekg(0x400);
        char value{};
        if (!image.read(&value, 1)) { std::filesystem::remove(tampered, error); return fail(L"read tampered copy", 0); }
        value ^= 0x5A;
        image.seekp(0x400);
        image.write(&value, 1);
        image.flush();
        if (!image) { std::filesystem::remove(tampered, error); return fail(L"write tampered copy", 0); }
    }
    const bool tamperedAccepted = VerifyAuthenticodePublisher(tampered.wstring(), thumbprint, actual, state, &trustStatus);
    std::filesystem::remove(tampered, error);
    if (tamperedAccepted || error) { return fail(L"tampered image", trustStatus); }
    std::wcout << L"AuthenticodeProbe passed: trusted OS signer accepted only with exact SHA-256 certificate pin; wrong pin, unsigned image, and modified signed image rejected. This does not enroll the VeilDesk publisher certificate.\n";
    return 0;
}
