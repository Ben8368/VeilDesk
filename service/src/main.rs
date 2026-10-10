use veil_secure_service::policy;
mod scm;
mod transport;
use std::sync::Mutex;
use std::time::Duration;

fn run() -> Result<(), Box<dyn std::error::Error>> {
    let args: Vec<String> = std::env::args().skip(1).collect();
    match args.as_slice() {
        [mode] if mode == "--service" => scm::run()?,
        [mode, seconds] if mode == "--console" => {
            let seconds: u64 = seconds.parse()?;
            if !(1..=60).contains(&seconds) {
                return Err("console duration must be 1..60".into());
            }
            let name = format!(
                r"\\.\pipe\VeilDesk.SecureService.test.{}",
                std::process::id()
            );
            let pipe = transport::Pipe::new(&name, false)?;
            let authorization_pipe =
                transport::Pipe::new_authorization(&(name.clone() + ".authorization"), false)?;
            println!(
                "Console diagnostic endpoint ready; grants disabled; protocol={}; authorization endpoint={}.authorization",
                policy::PROTOCOL,
                name
            );
            let state = Mutex::new(transport::new_policy()?);
            let duration = Duration::from_secs(seconds);
            std::thread::scope(|scope| -> Result<(), std::io::Error> {
                let state_ref = &state;
                let diagnostic = scope.spawn(move || pipe.serve(state_ref, || false, duration));
                let state_ref = &state;
                let authorization = scope.spawn(move || {
                    authorization_pipe.serve_authorization(state_ref, || false, duration, false)
                });
                diagnostic
                    .join()
                    .map_err(|_| std::io::Error::other("diagnostic IPC worker panicked"))??;
                authorization
                    .join()
                    .map_err(|_| std::io::Error::other("authorization IPC worker panicked"))??;
                state
                    .lock()
                    .map_err(|_| std::io::Error::other("policy mutex poisoned"))?
                    .revoke_all()
                    .map_err(std::io::Error::other)?;
                Ok(())
            })?;
        }
        _ => return Err("Usage: veil-secure-service --service | --console 1..60".into()),
    }
    Ok(())
}
fn main() {
    if let Err(error) = run() {
        eprintln!("SecureService failed: {error}");
        std::process::exit(1);
    }
}
