#!/usr/bin/env bash
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$root"
for tool in cmake ctest cargo; do
    command -v "$tool" >/dev/null || { printf 'Required verification tool missing: %s\n' "$tool" >&2; exit 1; }
done

cmake --preset portable-core
cmake --build --preset portable-core
ctest --preset portable-core --no-tests=error
cargo fmt --manifest-path service/Cargo.toml -- --check
cargo clippy --manifest-path service/Cargo.toml --locked --all-targets -- -D warnings
cargo test --manifest-path service/Cargo.toml --locked
printf '%s\n' 'Portable core verification passed; platform runtime, capture, identity and isolation are not exercised.'
