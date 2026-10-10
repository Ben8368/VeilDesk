use veil_secure_service::policy;
mod scm;
mod transport;
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
            println!(
                "Console diagnostic endpoint ready; grants disabled; protocol={}",
                policy::PROTOCOL
            );
            pipe.serve(
                &mut transport::new_policy()?,
                || false,
                Duration::from_secs(seconds),
            )?;
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
