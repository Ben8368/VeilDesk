use crate::transport::{new_policy, wide, Pipe};
use std::{
    ptr::{null, null_mut},
    sync::{
        atomic::{AtomicBool, Ordering},
        Mutex,
    },
    time::Duration,
};
use windows_sys::Win32::System::Services::*;

static STOP: AtomicBool = AtomicBool::new(false);

unsafe extern "system" fn control(
    code: u32,
    _: u32,
    _: *mut std::ffi::c_void,
    _: *mut std::ffi::c_void,
) -> u32 {
    match code {
        SERVICE_CONTROL_STOP | SERVICE_CONTROL_SHUTDOWN => {
            STOP.store(true, Ordering::Release);
            0
        }
        SERVICE_CONTROL_INTERROGATE => 0,
        _ => 120, // ERROR_CALL_NOT_IMPLEMENTED
    }
}
fn status(handle: SERVICE_STATUS_HANDLE, state: u32, code: u32) -> bool {
    let value = SERVICE_STATUS {
        dwServiceType: SERVICE_WIN32_OWN_PROCESS,
        dwCurrentState: state,
        dwControlsAccepted: if state == SERVICE_RUNNING {
            SERVICE_ACCEPT_STOP | SERVICE_ACCEPT_SHUTDOWN
        } else {
            0
        },
        dwWin32ExitCode: code,
        dwServiceSpecificExitCode: 0,
        dwCheckPoint: if state == SERVICE_START_PENDING || state == SERVICE_STOP_PENDING {
            1
        } else {
            0
        },
        dwWaitHint: if state == SERVICE_START_PENDING || state == SERVICE_STOP_PENDING {
            2000
        } else {
            0
        },
    };
    if unsafe { SetServiceStatus(handle, &value) } == 0 {
        eprintln!("SetServiceStatus: {}", std::io::Error::last_os_error());
        false
    } else {
        true
    }
}
unsafe extern "system" fn service_main(_: u32, _: *mut *mut u16) {
    let name = wide("VeilDeskSecureService");
    let handle = RegisterServiceCtrlHandlerExW(name.as_ptr(), Some(control), null_mut());
    if handle.is_null() {
        eprintln!(
            "RegisterServiceCtrlHandlerExW: {}",
            std::io::Error::last_os_error()
        );
        return;
    }
    STOP.store(false, Ordering::Release);
    if !status(handle, SERVICE_START_PENDING, 0) {
        return;
    }
    let pipe = match Pipe::new(r"\\.\pipe\VeilDesk.SecureService.v1", true) {
        Ok(pipe) => pipe,
        Err(error) => {
            status(
                handle,
                SERVICE_STOPPED,
                error.raw_os_error().unwrap_or(1) as u32,
            );
            return;
        }
    };
    let authorization_pipe =
        match Pipe::new_authorization(r"\\.\pipe\VeilDesk.SecureService.authorization.v1", true) {
            Ok(pipe) => pipe,
            Err(error) => {
                status(
                    handle,
                    SERVICE_STOPPED,
                    error.raw_os_error().unwrap_or(1) as u32,
                );
                return;
            }
        };
    if !status(handle, SERVICE_RUNNING, 0) {
        return;
    }
    let result = new_policy().and_then(|policy| {
        let policy = Mutex::new(policy);
        std::thread::scope(|scope| {
            let policy_ref = &policy;
            let diagnostic = scope.spawn(move || {
                pipe.serve(
                    policy_ref,
                    || STOP.load(Ordering::Acquire),
                    Duration::from_secs(365 * 86400),
                )
            });
            let policy_ref = &policy;
            let authorization = scope.spawn(move || {
                authorization_pipe.serve_authorization(
                    policy_ref,
                    || STOP.load(Ordering::Acquire),
                    Duration::from_secs(365 * 86400),
                    true,
                )
            });
            diagnostic
                .join()
                .map_err(|_| std::io::Error::other("diagnostic IPC worker panicked"))??;
            authorization
                .join()
                .map_err(|_| std::io::Error::other("authorization IPC worker panicked"))??;
            policy
                .lock()
                .map_err(|_| std::io::Error::other("policy mutex poisoned"))?
                .revoke_all()
                .map_err(std::io::Error::other)?;
            Ok(())
        })
    });
    status(handle, SERVICE_STOP_PENDING, 0);
    let error = result
        .err()
        .map(|error| error.raw_os_error().unwrap_or(1) as u32)
        .unwrap_or(0);
    status(handle, SERVICE_STOPPED, error);
}
pub fn run() -> std::io::Result<()> {
    let mut name = wide("VeilDeskSecureService");
    let table = [
        SERVICE_TABLE_ENTRYW {
            lpServiceName: name.as_mut_ptr(),
            lpServiceProc: Some(service_main),
        },
        SERVICE_TABLE_ENTRYW {
            lpServiceName: null::<u16>() as *mut u16,
            lpServiceProc: None,
        },
    ];
    if unsafe { StartServiceCtrlDispatcherW(table.as_ptr()) } == 0 {
        Err(std::io::Error::last_os_error())
    } else {
        Ok(())
    }
}
