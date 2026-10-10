# 可移植核心基础验证

日期：2026-10-10（Asia/Shanghai）。源码基线 `1ebba22` 加本轮工作树修复；macOS arm64、Apple Clang 21.0.0、Rust 1.96.0、CMake 3.31.10。Windows 仍是主产品平台，本记录只覆盖 [ADR 0010](../../ADR/0010-portable-core-foundation.md) 的构建与测试基础。

## 实现范围

- CMake `VeilAuthorization` 独立编译既有 envelope；Windows Broker 链接同一实现。`VEILDESK_BUILD_WINDOWS_APPS` 在 Windows 默认开启，开启时要求 Windows x64/MSVC；关闭时仅构建核心，RC 不加载。
- Rust `envelope` / `policy` 不依赖 Windows；WinTrust/身份/SCM/pipe 与 `windows-sys` 依赖仅在 Windows 编译。非 Windows 二进制非零退出，未提供模拟平台后端。
- Rust WinTrust 编译/feature/lint 修复保留；`TOKEN_USER` 改为类型对齐存储并核对查询错误/返回大小，SID 字符串由 RAII 在异常路径释放；hash/trust 清理错误给出具体诊断。Windows 身份语义保持。
- C++ 与 Rust 使用相同 152 字节小端 fixture；C++ probe 同时比较重新编码结果。Windows probe 保持 `wmain` / Unicode 路径，macOS 使用 `main` / 原生路径。

## 验证记录

`PATH=/private/tmp/veildesk-foundation-tools/bin:$PATH bash scripts/verify-portable.sh` 退出码 0：CMake Release 核心构建与 CTest 1/1、Rust 策略/envelope 8/8、非 Windows 服务/console 拒绝测试 1/1、fmt 与 Clippy `-D warnings` 通过。所有测试原生运行于 macOS；不再依赖临时 Rust harness。

`cargo clippy --manifest-path service/Cargo.toml --locked --offline --target x86_64-pc-windows-msvc --all-targets -- -D warnings` 通过，包含 Windows 库、二进制和测试的类型检查；不执行链接或 Windows 程序。

负向验证：macOS 强制 `VEILDESK_BUILD_WINDOWS_APPS=ON` 时 configure 退出 1 并提示 Windows/MSVC 要求；默认无开关 configure 则选择核心。C++ probe 缺文件/非法 hex 分别退出 1，缺参数退出 2；从仓库外调用 portable 验证入口且 PATH 缺 Cargo 时退出 1，在构建前报告缺少工具。Windows 目标使用 `VEILDESK_AUTHENTICODE_PUBLISHER_SHA256=not-a-pin` 时构建被拒绝；相同值不阻止 macOS 核心检查，因为 Windows 身份模块不在该目标中编译。

临时解压官方 PowerShell 7.4.7 后，`pwsh -NoProfile -File scripts/governance-docs-check.ps1` 和 `tests/governance-smoke.ps1` 通过（含错误链接、预算与 CRLF 拒绝）；`go run github.com/rhysd/actionlint/cmd/actionlint@v1.7.12 -color .github/workflows/ci.yml` 退出 0。macOS CI 已接入 required；本记录不包含远端新工作流结果。

初次 `pwsh -NoProfile -File scripts/verify.ps1 -SkipInteractive` 因 Windows hook 回归硬编码 `powershell` 退出 1；入口补非 Windows fail-fast 后，已验证立即退出 1 并提示 Windows x64/MSVC 要求及 portable 入口。完整 Windows C++ Release/Debug、签名/身份运行、console IPC、SCM 与 GPU/捕获均未在本机执行；Rust 1.85.1 的新改动待 Windows CI，任何核心测试不改变 G2/G3/G4。

## 后续扩展边界

v1 的 SID/session/creation-time 与映像摘要仍采用 Windows 语义。未来 macOS 身份、时钟、资源 owner/同步/交付、显示和捕获权限、服务生命周期需分别设计、版本化与真机验收。当前未实现 macOS Viewer、录制/保护、IPC/服务、Workspace；G2/G3/G4 状态保持。
