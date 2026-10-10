use std::{
    ffi::c_void,
    ptr::null_mut,
    sync::Mutex,
    time::{Duration, Instant},
};
use veil_secure_service::{
    envelope::{AuthorizationEnvelope, SCOPE_RECORD, SCOPE_VIEW},
    identity::{pipe_client_subject, AuthenticatedClient},
    policy::Policy,
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
    System::{
        Pipes::{
            ConnectNamedPipe, CreateNamedPipeW, DisconnectNamedPipe, PIPE_NOWAIT,
            PIPE_READMODE_MESSAGE, PIPE_REJECT_REMOTE_CLIENTS, PIPE_TYPE_MESSAGE,
        },
        SystemInformation::GetTickCount64,
    },
};

const AUTH_MAGIC: u32 = 0x5654_4155;
const AUTH_VERSION: u16 = 1;
const AUTH_ISSUE: u16 = 1;
const AUTH_CONSUME: u16 = 2;
const AUTH_REVOKE: u16 = 3;
const AUTH_HEADER: usize = 16;
const AUTH_MAX_REQUEST: usize =
    AUTH_HEADER + 8 + 2 * (veil_secure_service::envelope::WIRE_SIZE + 4);
const AUTH_RESPONSE: usize = AUTH_HEADER + 4 + veil_secure_service::envelope::WIRE_SIZE;

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
// Each uniquely-owned HANDLE is moved to exactly one worker thread.
unsafe impl Send for Pipe {}
impl Drop for Pipe {
    fn drop(&mut self) {
        unsafe {
            CloseHandle(self.0);
        }
    }
}

impl Pipe {
    pub fn new(name: &str, service: bool) -> std::io::Result<Self> {
        Self::create(name, service, false)
    }
    pub fn new_authorization(name: &str, service: bool) -> std::io::Result<Self> {
        Self::create(name, service, true)
    }

    fn create(name: &str, service: bool, authorization: bool) -> std::io::Result<Self> {
        // Service: only SYSTEM, Administrators and LocalService. Console: current
        // owner (OW) as well, for unprivileged diagnostics in the test process.
        let acl = wide(if service && authorization {
            "D:P(A;;GA;;;SY)(A;;GA;;;BA)(A;;GA;;;LS)(A;;GA;;;AU)"
        } else if service {
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
                if authorization { 512 } else { 256 },
                if authorization {
                    AUTH_MAX_REQUEST as u32
                } else {
                    128
                },
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
        policy: &Mutex<Policy>,
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
                        let response = policy
                            .lock()
                            .map_err(|_| std::io::Error::other("policy mutex poisoned"))?
                            .request(&request[..read as usize]);
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
        Ok(())
    }

    pub fn serve_authorization(
        &self,
        policy: &Mutex<Policy>,
        stop: impl Fn() -> bool,
        duration: Duration,
        require_installed_broker: bool,
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
                let subject = unsafe { pipe_client_subject(self.0, require_installed_broker) }
                    .map_err(|error| error.to_string());
                if subject.is_err() {
                    eprintln!("authorization IPC client identity/publisher rejected");
                    unsafe {
                        DisconnectNamedPipe(self.0);
                    }
                    continue;
                }
                let mut request = [0_u8; AUTH_MAX_REQUEST];
                let mut idle_deadline = Instant::now() + Duration::from_secs(2);
                loop {
                    if stop() || Instant::now() >= end || Instant::now() >= idle_deadline {
                        break;
                    }
                    let mut read = 0_u32;
                    let read_ok = unsafe {
                        ReadFile(
                            self.0,
                            request.as_mut_ptr(),
                            request.len() as u32,
                            &mut read,
                            null_mut(),
                        )
                    } != 0;
                    if !read_ok {
                        let read_error = unsafe { GetLastError() };
                        if read_error == ERROR_NO_DATA {
                            std::thread::sleep(Duration::from_millis(1));
                            continue;
                        }
                        break;
                    }
                    if read == 0 {
                        break;
                    }
                    idle_deadline = Instant::now() + Duration::from_secs(2);
                    if !subject.as_ref().is_ok_and(|client| client.is_alive()) {
                        eprintln!("authorization IPC client process exited; closing authenticated connection");
                        break;
                    }
                    let now = unsafe { GetTickCount64() };
                    let (request_id, response_kind, body) = process_authorization_request(
                        &request[..read as usize],
                        &subject,
                        now,
                        policy,
                    );
                    let mut out = Vec::with_capacity(AUTH_RESPONSE);
                    put_u32(&mut out, AUTH_MAGIC);
                    put_u16(&mut out, AUTH_VERSION);
                    put_u16(&mut out, response_kind);
                    put_u32(&mut out, (AUTH_HEADER + body.len()) as u32);
                    put_u32(&mut out, request_id);
                    out.extend_from_slice(&body);
                    let mut written = 0_u32;
                    if unsafe {
                        WriteFile(
                            self.0,
                            out.as_ptr(),
                            out.len() as u32,
                            &mut written,
                            null_mut(),
                        )
                    } == 0
                        || written as usize != out.len()
                    {
                        eprintln!(
                            "authorization IPC response failed: {}",
                            std::io::Error::last_os_error()
                        );
                        break;
                    }
                }
                if unsafe { DisconnectNamedPipe(self.0) } == 0
                    && unsafe { GetLastError() }
                        != windows_sys::Win32::Foundation::ERROR_PIPE_NOT_CONNECTED
                {
                    return Err(std::io::Error::last_os_error());
                }
            } else if error != ERROR_PIPE_LISTENING && error != ERROR_NO_DATA {
                return Err(std::io::Error::from_raw_os_error(error as i32));
            }
            std::thread::sleep(Duration::from_millis(10));
        }
        Ok(())
    }
}

fn process_authorization_request(
    request: &[u8],
    subject: &Result<AuthenticatedClient, String>,
    now: u64,
    policy: &Mutex<Policy>,
) -> (u32, u16, Vec<u8>) {
    let request_id = request
        .get(12..16)
        .and_then(|v| v.try_into().ok())
        .map(u32::from_le_bytes)
        .unwrap_or(0);
    let kind = request
        .get(6..8)
        .and_then(|v| v.try_into().ok())
        .map(u16::from_le_bytes)
        .unwrap_or(0);
    let mut body = Vec::new();
    let result = (|| -> Result<(), &'static str> {
        if request.len() < AUTH_HEADER
            || u32_at(request, 0)? != AUTH_MAGIC
            || u16_at(request, 4)? != AUTH_VERSION
            || u32_at(request, 8)? as usize != request.len()
            || request_id == 0
        {
            return Err("protocol");
        }
        let caller = subject.as_ref().map_err(|_| "identity")?.subject;
        let mut state = policy.lock().map_err(|_| "state")?;
        match kind {
            AUTH_ISSUE if request.len() == AUTH_HEADER + 16 => {
                let scope = u32_at(request, AUTH_HEADER)?;
                if scope != SCOPE_VIEW && scope != SCOPE_RECORD {
                    return Err("denied");
                }
                let duration = u32_at(request, AUTH_HEADER + 4)? as u64;
                let frames = u32_at(request, AUTH_HEADER + 8)?;
                let bytes = u32_at(request, AUTH_HEADER + 12)?;
                let envelope = state.issue(caller, scope, duration, now, frames, bytes)?;
                body.extend_from_slice(&0_u32.to_le_bytes());
                body.extend_from_slice(&envelope.encode());
                Ok(())
            }
            AUTH_CONSUME if request.len() >= AUTH_HEADER + 8 => {
                let frame = u32_at(request, AUTH_HEADER)?;
                let count = u32_at(request, AUTH_HEADER + 4)? as usize;
                if !(1..=2).contains(&count)
                    || request.len()
                        != AUTH_HEADER + 8 + count * (veil_secure_service::envelope::WIRE_SIZE + 4)
                {
                    return Err("protocol");
                }
                let mut entries = Vec::with_capacity(count);
                let mut offset = AUTH_HEADER + 8;
                for _ in 0..count {
                    let envelope = AuthorizationEnvelope::decode(
                        &request[offset..offset + veil_secure_service::envelope::WIRE_SIZE],
                    )
                    .map_err(|_| "denied")?;
                    offset += veil_secure_service::envelope::WIRE_SIZE;
                    let bytes = u32_at(request, offset)?;
                    offset += 4;
                    entries.push((envelope, bytes));
                }
                state.record_frames(&entries, frame, caller, now)?;
                body.extend_from_slice(&0_u32.to_le_bytes());
                Ok(())
            }
            AUTH_REVOKE if request.len() == AUTH_HEADER => {
                state.revoke_all()?;
                body.extend_from_slice(&0_u32.to_le_bytes());
                Ok(())
            }
            _ => Err("protocol"),
        }
    })();
    if let Err(code) = result {
        body.clear();
        body.extend_from_slice(&status_code(code).to_le_bytes());
    }
    (request_id, kind | 0x8000, body)
}

fn status_code(code: &str) -> u32 {
    match code {
        "identity" => 2,
        "replay" => 3,
        "budget" => 4,
        "protocol" => 5,
        _ => 1,
    }
}
fn put_u16(out: &mut Vec<u8>, value: u16) {
    out.extend_from_slice(&value.to_le_bytes());
}
fn put_u32(out: &mut Vec<u8>, value: u32) {
    out.extend_from_slice(&value.to_le_bytes());
}
fn u16_at(bytes: &[u8], offset: usize) -> Result<u16, &'static str> {
    bytes
        .get(offset..offset + 2)
        .and_then(|b| b.try_into().ok())
        .map(u16::from_le_bytes)
        .ok_or("protocol")
}
fn u32_at(bytes: &[u8], offset: usize) -> Result<u32, &'static str> {
    bytes
        .get(offset..offset + 4)
        .and_then(|b| b.try_into().ok())
        .map(u32::from_le_bytes)
        .ok_or("protocol")
}
