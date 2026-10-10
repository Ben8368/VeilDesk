use crate::policy::Policy;
use std::{
    ffi::c_void,
    ptr::null_mut,
    time::{Duration, Instant},
};
use windows_sys::Win32::{
    Foundation::{
        CloseHandle, GetLastError, LocalFree, ERROR_NO_DATA, ERROR_PIPE_CONNECTED,
        ERROR_PIPE_LISTENING, HANDLE, INVALID_HANDLE_VALUE,
    },
    Security::{
        Authorization::ConvertStringSecurityDescriptorToSecurityDescriptorW, SECURITY_ATTRIBUTES,
    },
    Storage::FileSystem::{ReadFile, WriteFile, FILE_FLAG_FIRST_PIPE_INSTANCE, PIPE_ACCESS_DUPLEX},
    System::Pipes::{
        ConnectNamedPipe, CreateNamedPipeW, DisconnectNamedPipe, PIPE_NOWAIT,
        PIPE_READMODE_MESSAGE, PIPE_REJECT_REMOTE_CLIENTS, PIPE_TYPE_MESSAGE,
    },
};

pub fn wide(value: &str) -> Vec<u16> {
    value.encode_utf16().chain(Some(0)).collect()
}
pub fn new_policy() -> std::io::Result<Policy> {
    use windows_sys::Win32::Security::Cryptography::{
        BCryptGenRandom, BCRYPT_USE_SYSTEM_PREFERRED_RNG,
    };
    let mut bytes = [0_u8; 8];
    let result = unsafe {
        BCryptGenRandom(
            null_mut(),
            bytes.as_mut_ptr(),
            bytes.len() as u32,
            BCRYPT_USE_SYSTEM_PREFERRED_RNG,
        )
    };
    if result != 0 {
        return Err(std::io::Error::other(format!(
            "BCryptGenRandom NTSTATUS=0x{:08x}",
            result as u32
        )));
    }
    let epoch = u64::from_le_bytes(bytes) & 0x7fff_ffff_ffff_ffff;
    Policy::new(epoch).map_err(std::io::Error::other)
}
pub struct Pipe(HANDLE);
impl Drop for Pipe {
    fn drop(&mut self) {
        unsafe {
            CloseHandle(self.0);
        }
    }
}

impl Pipe {
    pub fn new(name: &str, service: bool) -> std::io::Result<Self> {
        // Service: only SYSTEM, Administrators and LocalService. Console: current
        // owner (OW) as well, for unprivileged diagnostics in the test process.
        let acl = wide(if service {
            "D:P(A;;GA;;;SY)(A;;GA;;;BA)(A;;GA;;;LS)"
        } else {
            "D:P(A;;GA;;;SY)(A;;GA;;;BA)(A;;GA;;;OW)"
        });
        let mut descriptor: *mut c_void = null_mut();
        unsafe {
            if ConvertStringSecurityDescriptorToSecurityDescriptorW(
                acl.as_ptr(),
                1,
                &mut descriptor,
                null_mut(),
            ) == 0
            {
                return Err(std::io::Error::last_os_error());
            }
            let security = SECURITY_ATTRIBUTES {
                nLength: std::mem::size_of::<SECURITY_ATTRIBUTES>() as u32,
                lpSecurityDescriptor: descriptor,
                bInheritHandle: 0,
            };
            let handle = CreateNamedPipeW(
                wide(name).as_ptr(),
                PIPE_ACCESS_DUPLEX | FILE_FLAG_FIRST_PIPE_INSTANCE,
                PIPE_TYPE_MESSAGE
                    | PIPE_READMODE_MESSAGE
                    | PIPE_NOWAIT
                    | PIPE_REJECT_REMOTE_CLIENTS,
                1,
                256,
                128,
                1000,
                &security,
            );
            let error = GetLastError();
            LocalFree(descriptor);
            if handle == INVALID_HANDLE_VALUE {
                return Err(std::io::Error::from_raw_os_error(error as i32));
            }
            Ok(Self(handle))
        }
    }

    pub fn serve(
        &self,
        policy: &mut Policy,
        stop: impl Fn() -> bool,
        duration: Duration,
    ) -> std::io::Result<()> {
        let end = Instant::now() + duration;
        while !stop() && Instant::now() < end {
            let connected = unsafe { ConnectNamedPipe(self.0, null_mut()) } != 0;
            let error = if connected {
                0
            } else {
                unsafe { GetLastError() }
            };
            if connected || error == ERROR_PIPE_CONNECTED {
                let deadline = Instant::now() + Duration::from_secs(1);
                let mut request = [0_u8; 128];
                let mut read = 0;
                while !stop() && Instant::now() < deadline {
                    let ok = unsafe {
                        ReadFile(
                            self.0,
                            request.as_mut_ptr(),
                            request.len() as u32,
                            &mut read,
                            null_mut(),
                        )
                    } != 0;
                    if ok && read > 0 {
                        let response = policy.request(&request[..read as usize]);
                        let mut written = 0;
                        let sent = unsafe {
                            WriteFile(
                                self.0,
                                response.as_ptr(),
                                response.len() as u32,
                                &mut written,
                                null_mut(),
                            )
                        };
                        if sent == 0 || written as usize != response.len() {
                            eprintln!("IPC response failed: {}", std::io::Error::last_os_error());
                        } else {
                            // DisconnectNamedPipe discards unread bytes. A bounded client
                            // receipt avoids relying on a blocking FlushFileBuffers.
                            let receipt_deadline = Instant::now() + Duration::from_secs(1);
                            let mut receipt = [0_u8; 16];
                            while !stop() && Instant::now() < receipt_deadline {
                                let mut count = 0;
                                let received = unsafe {
                                    ReadFile(
                                        self.0,
                                        receipt.as_mut_ptr(),
                                        receipt.len() as u32,
                                        &mut count,
                                        null_mut(),
                                    )
                                } != 0;
                                if received && count > 0 {
                                    break;
                                }
                                if !received && unsafe { GetLastError() } != ERROR_NO_DATA {
                                    break;
                                }
                                std::thread::sleep(Duration::from_millis(10));
                            }
                        }
                        break;
                    }
                    let error = unsafe { GetLastError() };
                    if !ok && error != ERROR_NO_DATA {
                        break;
                    }
                    std::thread::sleep(Duration::from_millis(10));
                }
                if unsafe { DisconnectNamedPipe(self.0) } == 0 {
                    return Err(std::io::Error::last_os_error());
                }
            } else if error != ERROR_PIPE_LISTENING && error != ERROR_NO_DATA {
                return Err(std::io::Error::from_raw_os_error(error as i32));
            }
            std::thread::sleep(Duration::from_millis(10));
        }
        policy.revoke_all().map_err(std::io::Error::other)?;
        Ok(())
    }
}
