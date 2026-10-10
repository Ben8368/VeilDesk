use std::env;

fn main() {
    println!("cargo:rerun-if-env-changed=VEILDESK_AUTHENTICODE_PUBLISHER_SHA256");
    if env::var("CARGO_CFG_TARGET_OS").as_deref() != Ok("windows") {
        return;
    }
    let pin = env::var("VEILDESK_AUTHENTICODE_PUBLISHER_SHA256").unwrap_or_default();
    if !pin.is_empty() && (pin.len() != 64 || !pin.bytes().all(|byte| byte.is_ascii_hexdigit())) {
        panic!("VEILDESK_AUTHENTICODE_PUBLISHER_SHA256 must be empty or exactly 64 hexadecimal characters");
    }
    println!("cargo:rustc-env=VEILDESK_AUTHENTICODE_PUBLISHER_SHA256={pin}");
}
