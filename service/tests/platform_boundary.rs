// A portable library build must never imply that an unsupported service started.
#[cfg(not(windows))]
#[test]
fn unsupported_runtime_rejects_service_and_console_modes() {
    for args in [vec!["--service"], vec!["--console", "1"]] {
        let output = std::process::Command::new(env!("CARGO_BIN_EXE_veil-secure-service"))
            .args(args)
            .output()
            .expect("service binary starts for explicit platform rejection");
        assert_eq!(output.status.code(), Some(1));
        assert!(output.stdout.is_empty());
        assert!(String::from_utf8_lossy(&output.stderr).contains("runtime requires Windows"));
    }
}
