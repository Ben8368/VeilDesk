use crate::envelope::{Subject, ROLE_BROKER};
use std::{mem::size_of, ptr::null_mut};
use windows_sys::Win32::{
    Foundation::{
        CloseHandle, LocalFree, ERROR_INSUFFICIENT_BUFFER, FILETIME, HANDLE, INVALID_HANDLE_VALUE,
    },
    Security::{
        Authorization::ConvertSidToStringSidW,
        Cryptography::{
            BCryptHash, BCryptOpenAlgorithmProvider, BCRYPT_ALG_HANDLE, BCRYPT_SHA256_ALGORITHM,
        },
        GetTokenInformation, TokenSessionId, TokenUser, SID_AND_ATTRIBUTES, TOKEN_QUERY,
        TOKEN_USER,
    },
    System::{
        Pipes::GetNamedPipeClientProcessId,
        Threading::{
            GetProcessTimes, OpenProcess, OpenProcessToken, QueryFullProcessImageNameW,
            PROCESS_QUERY_LIMITED_INFORMATION,
        },
    },
};

struct OwnedHandle(HANDLE);
impl Drop for OwnedHandle {
    fn drop(&mut self) {
        unsafe {
            CloseHandle(self.0);
        }
    }
}

struct OwnedLocalString(*mut u16);
impl Drop for OwnedLocalString {
    fn drop(&mut self) {
        unsafe {
            if !LocalFree(self.0.cast()).is_null() {
                eprintln!(
                    "LocalFree(SID string) failed: {}",
                    std::io::Error::last_os_error()
                );
            }
        }
    }
}

fn hash(bytes: &[u8]) -> std::io::Result<[u8; 32]> {
    let mut algorithm: BCRYPT_ALG_HANDLE = null_mut();
    let status = unsafe {
        BCryptOpenAlgorithmProvider(&mut algorithm, BCRYPT_SHA256_ALGORITHM, null_mut(), 0)
    };
    if status != 0 {
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
            bytes.as_ptr(),
            bytes.len() as u32,
            digest.as_mut_ptr(),
            digest.len() as u32,
        )
    };
    let close_status = unsafe {
        windows_sys::Win32::Security::Cryptography::BCryptCloseAlgorithmProvider(algorithm, 0)
    };
    if status != 0 {
        return Err(std::io::Error::other(format!(
            "BCryptHash NTSTATUS=0x{:08x}",
            status as u32
        )));
    }
    if close_status != 0 {
        return Err(std::io::Error::other(format!(
            "BCryptCloseAlgorithmProvider NTSTATUS=0x{:08x}",
            close_status as u32
        )));
    }
    Ok(digest)
}

/// # Safety
/// `pipe` must be a live connected server-side named-pipe handle.
pub unsafe fn pipe_client_subject(
    pipe: HANDLE,
    require_installed_broker: bool,
) -> std::io::Result<Subject> {
    let mut pid = 0_u32;
    if unsafe { GetNamedPipeClientProcessId(pipe, &mut pid) } == 0 || pid == 0 {
        return Err(std::io::Error::last_os_error());
    }
    let process = unsafe { OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, 0, pid) };
    if process.is_null() || process == INVALID_HANDLE_VALUE {
        return Err(std::io::Error::last_os_error());
    }
    let process = OwnedHandle(process);
    let mut created = FILETIME::default();
    let mut exited = FILETIME::default();
    let mut kernel = FILETIME::default();
    let mut user = FILETIME::default();
    if unsafe { GetProcessTimes(process.0, &mut created, &mut exited, &mut kernel, &mut user) } == 0
    {
        return Err(std::io::Error::last_os_error());
    }
    let creation_time =
        (u64::from(created.dwHighDateTime) << 32) | u64::from(created.dwLowDateTime);
    let mut image = vec![0_u16; 32768];
    let mut image_len = image.len() as u32;
    if unsafe { QueryFullProcessImageNameW(process.0, 0, image.as_mut_ptr(), &mut image_len) } == 0
    {
        return Err(std::io::Error::last_os_error());
    }
    image.truncate(image_len as usize);
    let image_path = String::from_utf16(&image).map_err(std::io::Error::other)?;
    let image_name = image_path.rsplit(['\\', '/']).next().unwrap_or_default();
    if !image_name.eq_ignore_ascii_case("CaptureBroker.exe") {
        return Err(std::io::Error::new(
            std::io::ErrorKind::PermissionDenied,
            "authorization client image is not CaptureBroker.exe",
        ));
    }
    if require_installed_broker {
        let program_files = std::env::var("ProgramFiles").map_err(std::io::Error::other)?;
        let expected = format!("{program_files}\\VeilDesk\\CaptureBroker.exe");
        if !image_path.eq_ignore_ascii_case(&expected) {
            return Err(std::io::Error::new(
                std::io::ErrorKind::PermissionDenied,
                "authorization client image path is not the installed CaptureBroker",
            ));
        }
    }
    crate::authenticode::verify_broker_publisher(pid, creation_time, &image_path)?;
    let mut token = null_mut();
    if unsafe { OpenProcessToken(process.0, TOKEN_QUERY, &mut token) } == 0 {
        return Err(std::io::Error::last_os_error());
    }
    let token = OwnedHandle(token);
    let mut session = 0_u32;
    let mut returned = 0_u32;
    if unsafe {
        GetTokenInformation(
            token.0,
            TokenSessionId,
            (&mut session as *mut u32).cast(),
            size_of::<u32>() as u32,
            &mut returned,
        )
    } == 0
    {
        return Err(std::io::Error::last_os_error());
    }
    let mut needed = 0_u32;
    if unsafe { GetTokenInformation(token.0, TokenUser, null_mut(), 0, &mut needed) } != 0 {
        return Err(std::io::Error::other(
            "TokenUser size query unexpectedly succeeded",
        ));
    }
    let size_error = std::io::Error::last_os_error();
    if size_error.raw_os_error() != Some(ERROR_INSUFFICIENT_BUFFER as i32) {
        return Err(size_error);
    }
    if needed < size_of::<TOKEN_USER>() as u32 || needed > 65536 {
        return Err(std::io::Error::other("TokenUser size outside bound"));
    }
    // TOKEN_USER contains pointers; a byte vector does not guarantee its alignment.
    let mut storage =
        vec![TOKEN_USER::default(); (needed as usize).div_ceil(size_of::<TOKEN_USER>())];
    if unsafe {
        GetTokenInformation(
            token.0,
            TokenUser,
            storage.as_mut_ptr().cast(),
            needed,
            &mut returned,
        )
    } == 0
    {
        return Err(std::io::Error::last_os_error());
    }
    if returned < size_of::<TOKEN_USER>() as u32 || returned > needed {
        return Err(std::io::Error::other(
            "TokenUser returned size outside bound",
        ));
    }
    let token_user = &storage[0];
    let sid_attributes: SID_AND_ATTRIBUTES = token_user.User;
    let mut sid_string = null_mut();
    if unsafe { ConvertSidToStringSidW(sid_attributes.Sid, &mut sid_string) } == 0 {
        return Err(std::io::Error::last_os_error());
    }
    if sid_string.is_null() {
        return Err(std::io::Error::other(
            "ConvertSidToStringSidW returned no string",
        ));
    }
    let sid_string = OwnedLocalString(sid_string);
    let sid_len = unsafe { (0..1024).position(|index| *sid_string.0.add(index) == 0) }
        .ok_or_else(|| std::io::Error::other("SID string exceeds bound"))?;
    let sid_words = unsafe { std::slice::from_raw_parts(sid_string.0, sid_len) };
    let sid_bytes = sid_words
        .iter()
        .flat_map(|word| word.to_le_bytes())
        .collect::<Vec<_>>();
    let mut image_bytes = Vec::with_capacity(image.len() * 2);
    for word in image {
        image_bytes.extend_from_slice(&word.to_le_bytes());
    }
    Ok(Subject {
        pid,
        session,
        creation_time,
        sid_hash: hash(&sid_bytes)?,
        image_hash: hash(&image_bytes)?,
        role: ROLE_BROKER,
    })
}
