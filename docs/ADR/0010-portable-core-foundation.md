# ADR 0010：Windows 主平台与可移植核心基础

- 状态：已接受
- 日期：2026-10-10

## 背景

维护者要求修复已发现问题，并为后续 macOS 扩展准备基础；主要产品环境仍为 Windows。此前 CMake 在加载 RC 语言时即依赖 Windows，Rust 库无条件加载 WinTrust/身份模块，导致平台无关的编解码和策略测试也无法在 macOS 原生运行。

## 决策

- 本次接受限于构建分层、平台无关测试与明确的未支持终态；不授权实现 macOS 显示、捕获、服务或虚拟化后端，不改变 G2/G3/G4 顺序。
- C++ `VeilAuthorization` 独立承载既有 152 字节 envelope 编解码；Windows Broker 链接同一目标。CMake 的 Windows 产品开关在 Windows 默认开启，显式关闭时只构建核心。开启时仍要求 Windows x64/MSVC；RC、Win32 和 D3D11 依赖留在该分支。
- Rust 库的 `envelope` / `policy` 保持平台无关；Authenticode、真实进程身份、SCM、命名管道和 `windows-sys` 依赖仅在 Windows 编译。非 Windows 服务二进制明确非零退出，不提供授权或成功的模拟服务。
- v1 主体仍使用 Windows SID/session/creation-time 与映像摘要语义。核心测试通过不表示这些字段适用于 macOS；后续平台身份和内容资源契约须独立 ADR、版本/拒绝规则和真机证据。
- CI 增加 macOS 核心构建/测试，保留 Windows Release/Debug、Rust MSRV/stable 与运行验证；两者共同计入 required 检查。

## 后果

可以在 macOS 复用现有策略和共享 wire fixture，及时发现协议、预算与撤销回归；核心没有图形、屏幕权限或平台 IPC 能力。Windows 是主产品平台，现有安全语义与未验证 Gate 保持。将来新增平台后端时，真实身份、单调时间、内容交付、捕获限制及不支持行为需逐项定义；不以空实现或其他平台 API 名称占位。

## 替代方案

- 全面重写为跨平台 GUI/服务框架：超出“基础”范围，当前不采用。
- 非 Windows 模拟签名、身份或服务成功：拒绝，会扩大能力表述并弱化默认拒绝。
- 只复制策略到临时测试 harness：此前用于定位编译问题，但无法形成持久的构建/CI 边界。

## 关联文档

[架构](../ARCHITECTURE.md)、[产品](../PRODUCT.md)、[工作包](../DEVELOPMENT_BLUEPRINT.md)、[质量](../QUALITY.md)、[证据](../evidence/portable-core/README.md)。
