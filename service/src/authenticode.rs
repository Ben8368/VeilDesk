use std::{
    mem::size_of,
    ptr::{null, null_mut},
    sync::{Mutex, OnceLock},
};
use windows_sys::Win32::{
    Foundation::BOOL,
    Security::{
        Cryptography::{
            BCryptHash, BCryptOpenAlgorithmProvider, BCRYPT_ALG_HANDLE, BCRYPT_SHA256_ALGORITHM,
            CERT_CONTEXT,
        },
        WinTrust::{
            WTHelperGetProvSignerFromChain, WTHelperProvDataFromStateData, WinVerifyTrust,
            CRYPT_PROVIDER_CERT, CRYPT_PROVIDER_DATA, CRYPT_PROVIDER_SGNR,
            WINTRUST_ACTION_GENERIC_VERIFY_V2, WINTRUST_DATA, WINTRUST_DATA_0, WINTRUST_FILE_INFO,
            WTD_CACHE_ONLY_URL_RETRIEVAL, WTD_CHOICE_FILE, WTD_REVOCATION_CHECK_CHAIN_EXCLUDE_ROOT,
            WTD_REVOKE_WHOLECHAIN, WTD_STATEACTION_CLOSE, WTD_STATEACTION_VERIFY, WTD_UI_NONE,
        },
    },
};

type VerifiedIdentity = (u32, u64, String);
static VERIFIED_BROKER: OnceLock<Mutex<Option<VerifiedIdentity>>> = OnceLock::new();

struct WinTrustStateGuard {
    data: *mut WINTRUST_DATA,
    action: *const windows_sys::core::GUID,
}

impl Drop for WinTrustStateGuard {
    fn drop(&mut self) {
        unsafe {
            (*self.data).dwStateAction = WTD_STATEACTION_CLOSE;
            let _ = WinVerifyTrust(null_mut(), self.action, self.data);
        }
    }
}

fn certificate_sha256(certificate: &CERT_CONTEXT) -> std::io::Result<String> {
    if certificate.pbCertEncoded.is_null() || certificate.cbCertEncoded == 0 {
        return Err(std::io::Error::other(
            "Authenticode signer certificate is empty",
        ));
    }
    let mut algorithm: BCRYPT_ALG_HANDLE = null_mut();
    let status =
        unsafe { BCryptOpenAlgorithmProvider(&mut algorithm, BCRYPT_SHA256_ALGORITHM, null(), 0) };
    if status < 0 {
        return Err(std::io::Error::other(format!(
            "BCryptOpenAlgorithmProvider NTSTATUS=0x{:08x}",
            status as u32
        )));
    }
    let mut digest = [0_u8; 32];
    let status = unsafe {
        BCryptHash(
            algorithm,
            null_mut(),
            0,
            certificate.pbCertEncoded,
            certificate.cbCertEncoded,
            digest.as_mut_ptr(),
            digest.len() as u32,
        )
    };
    unsafe {
        windows_sys::Win32::Security::Cryptography::BCryptCloseAlgorithmProvider(algorithm, 0);
    }
    if status < 0 {
        return Err(std::io::Error::other(format!(
            "BCryptHash signer certificate NTSTATUS=0x{:08x}",
            status as u32
        )));
    }
    let mut thumbprint = String::with_capacity(64);
    for byte in digest {
        thumbprint.push_str(&format!("{byte:02X}"));
    }
    Ok(thumbprint)
}

fn signer_thumbprint(path: &str) -> std::io::Result<String> {
    use std::os::windows::ffi::OsStrExt;
    let mut image: Vec<u16> = std::ffi::OsStr::new(path)
        .encode_wide()
        .chain(Some(0))
        .collect();
    let mut file = WINTRUST_FILE_INFO {
        cbStruct: size_of::<WINTRUST_FILE_INFO>() as u32,
        pcwszFilePath: image.as_mut_ptr(),
        hFile: null_mut(),
        pgKnownSubject: null_mut(),
    };
    let mut data = WINTRUST_DATA::default();
    data.cbStruct = size_of::<WINTRUST_DATA>() as u32;
    data.dwUIChoice = WTD_UI_NONE;
    data.fdwRevocationChecks = WTD_REVOKE_WHOLECHAIN;
    data.dwUnionChoice = WTD_CHOICE_FILE;
    data.Anonymous = WINTRUST_DATA_0 { pFile: &mut file };
    data.dwStateAction = WTD_STATEACTION_VERIFY;
    data.dwProvFlags = WTD_REVOCATION_CHECK_CHAIN_EXCLUDE_ROOT | WTD_CACHE_ONLY_URL_RETRIEVAL;
    let action = &WINTRUST_ACTION_GENERIC_VERIFY_V2;
    let status = unsafe { WinVerifyTrust(null_mut(), action, &mut data) };
    let _cleanup = WinTrustStateGuard {
        data: &mut data,
        action,
    };
    if status != 0 {
        return Err(std::io::Error::other(format!(
            "WinVerifyTrust rejected Broker image: 0x{:08x}",
            status as u32
        )));
    }
    let provider: *mut CRYPT_PROVIDER_DATA =
        unsafe { WTHelperProvDataFromStateData(data.hWVTStateData) };
    if provider.is_null() {
        return Err(std::io::Error::other(
            "WinVerifyTrust returned no provider state",
        ));
    }
    let signer: *mut CRYPT_PROVIDER_SGNR =
        unsafe { WTHelperGetProvSignerFromChain(provider, 0, 0 as BOOL, 0) };
    if signer.is_null() || unsafe { (*signer).csCertChain == 0 || (*signer).pasCertChain.is_null() }
    {
        return Err(std::io::Error::other(
            "WinVerifyTrust returned no primary signer",
        ));
    }
    let cert: *mut CRYPT_PROVIDER_CERT = unsafe { (*signer).pasCertChain };
    let context = unsafe { (*cert).pCert };
    if context.is_null() {
        return Err(std::io::Error::other(
            "WinVerifyTrust primary signer certificate is missing",
        ));
    }
    let context = unsafe { &*context };
    certificate_sha256(context)
}

pub fn verify_broker_publisher(
    pid: u32,
    creation_time: u64,
    image_path: &str,
) -> std::io::Result<()> {
    let expected = env!("VEILDESK_AUTHENTICODE_PUBLISHER_SHA256");
    if expected.len() != 64 || !expected.bytes().all(|byte| byte.is_ascii_hexdigit()) {
        return Err(std::io::Error::new(
            std::io::ErrorKind::PermissionDenied,
            "SecureService publisher pin is unset or malformed",
        ));
    }
    let cache = VERIFIED_BROKER.get_or_init(|| Mutex::new(None));
    let mut verified = cache
        .lock()
        .map_err(|_| std::io::Error::other("publisher verification cache poisoned"))?;
    if verified.as_ref().is_some_and(|cached| {
        cached.0 == pid && cached.1 == creation_time && cached.2.eq_ignore_ascii_case(image_path)
    }) {
        return Ok(());
    }
    *verified = None;
    let actual = signer_thumbprint(image_path)?;
    if !actual.eq_ignore_ascii_case(expected) {
        return Err(std::io::Error::new(
            std::io::ErrorKind::PermissionDenied,
            "SecureService rejected Broker publisher pin",
        ));
    }
    *verified = Some((pid, creation_time, image_path.to_owned()));
    Ok(())
}
