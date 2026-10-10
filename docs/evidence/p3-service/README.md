# Phase 3 Rust 服务原型证据

日期：2026-10-10（Asia/Shanghai）；P3-01/02 基础切片、P3-03 策略内核和 G2-B 授权 IPC；G2-B 实现与限定验证完成，G2-C publisher pin 校验实现已增加但项目证书未 enrollment；G3：未通过。
源码版本：基于 `b699715` 的本轮未提交工作树；服务在 `service/`，部署候选在 `scripts/service-control.ps1`。

G2-B 当前未提交增量：Rust 固定编码契约、真实客户端进程身份采集、策略与授权 IPC 位于 `service/src/`；C++ 编码/解码器和客户端位于 `src/broker/Authorization*`。两端使用 `tests/fixtures/authorization-envelope-v1.hex` 作为同一黄金夹具。授权 IPC 与诊断 STATUS/REVOKE 管道分离；Broker 每帧交付资源前由 Service 原子消费全部适用 envelope。console 端到端验证通过；正式 SCM 部署和映像签名者认证仍未覆盖。

## 环境与依赖

Windows 11 Pro build 26200，Rust/Cargo 1.85.1、x86_64-pc-windows-msvc。C++ / D3D11 与 Rust crate 独立构建。
`windows-sys=0.61.2`、`windows-link=0.2.1`，Cargo.lock 锁定校验和，依赖声明许可证均为 MIT OR Apache-2.0（实际读取缓存 package metadata）；项目本身未新增授权许可证。
SCM API、LocalService 与 BCrypt 官方依据见 [ADR 0005](../../ADR/0005-service-diagnostics-prototype.md)。

## 实际命令与结果

```powershell
powershell -NoProfile -File scripts/verify-service.ps1 -Offline
powershell -NoProfile -File tests/workspace-smoke.ps1
powershell -NoProfile -File scripts/verify.ps1
```

| 项目 | 实际结果 |
| --- | --- |
| Rust 策略单元测试 | G2-B 实现后 8/8：精确主体/epoch/generation/期限、重启换 epoch、帧重放、累计字节预算原子性、未知 scope、预算边界、诊断 GRANT 默认拒绝 |
| Rust Release | 锁定依赖编译/链接通过；SCM 入口存在，但未以 Windows 服务启动 |
| console IPC | 实际具名消息管道：状态返回、新随机 epoch、错误版本拒绝、GRANT 拒绝、REVOKE 代际递增、超大请求断开、8 秒结束通过 |
| IPC 资源/超时 | 诊断输入上限 128 字节；授权请求上限 336 字节、envelope 固定 152 字节；连接、响应回执有界，拒绝远程客户端，不依赖阻塞 FlushFileBuffers |
| 部署预览 | Install 无 Apply 仅输出计划，LocalService/手动启动/不自动启动，通过 smoke；未部署 |
| G2-B 完整回归 | `powershell -NoProfile -File scripts/verify.ps1` 退出码 0；C++ Release、CTest 12/12（含交互 Viewer/Capture、Broker/Service、benchmark 与共享纹理）、Rust 8/8/Release、console IPC、服务/VM 只读预览通过；SCM 仍未运行 |

## G2-B Authorization Envelope（2026-10-10）

- 固定 152 字节小端格式：magic/version/size、lease ID、boot epoch、generation、主体 PID/session/creation time、SID 与 image 摘要、角色、scope、签发时刻/deadline、nonce、最大帧数和最大字节数。Rust 与 C++ 读取同一黄金字节夹具；格式测试覆盖字节序、固定大小、错误版本、非法角色/scope 和预算。
- Rust `Policy` 是唯一 lease 状态：精确 envelope、boot epoch/generation/期限、单调帧号、累计字节预算、revoke generation；单 lease 30 秒、16 个 lease，`max_frames` 和 `max_bytes` 都按累计授权区间执行，重启换 epoch 后旧 envelope 无效。
- 独立授权管道支持 ISSUE、CONSUME_FRAME、REVOKE_ALL。请求不提供身份字段；Service 用 `GetNamedPipeClientProcessId` 打开当前客户端，读取创建时间、session、TokenUser SID 和完整映像路径，对 SID 与路径做 SHA-256，并在 SCM 模式只接受 `%ProgramFiles%\\VeilDesk\\CaptureBroker.exe`。console 模式仅用于测试，绑定同一用户/会话和显式 Service PID。
- Broker 生产端验证 named-pipe server 的 LocalService SID 和固定安装映像路径；console 测试绑定预期 PID、同 SID/session 与 Service 映像名。Broker 校验返回 envelope 属于自身实际 PID/创建时间/session/SID/image 后，才使用该票据。
- 正常 RECORD 与 VIEW 票据按帧/累计字节预算签发。每帧在 Producer 生成/交付共享纹理之前，Broker 将所有需要的票据一起发送给 Service 原子消费；未知/过期/重放/预算超限/撤销/Service 断连均停止新资源交付，无本地 lease 回退。授权撤销不能召回已交付资源。
- 端到端 console Broker smoke：approved 交付 8 帧；撤销后仅交付此前 2 帧；过期、伪造 PID、单帧超预算、Service 不可用均无新帧；错误 scope 拒签；重放在 1 帧后拒绝。Rust 策略负向覆盖伪造主体/代际/epoch、期限、重放、累计预算原子性、未知 scope 与非法 envelope 版本/角色。诊断管道仍拒绝 GRANT。
- Envelope 无 MAC；其完整性依赖 pipe ACL、实际对端身份和响应 request ID。后续代码审查增量让 Broker 以 Authenticode pin 校验 Service，Service 也以相同构建 pin 校验 Broker；该增量待 Windows 验证。本机没有 VeilDesk 项目证书，项目 publisher 正向 enrollment 未测；同用户恶意映像/注入/句柄盗用不在保证范围。G2-C 项目证书与 G2-D 故障矩阵仍开放。SCM/LocalService 服务启动、跨账户 ACL 和正式 Broker 安装路径尚未真机验证，因此本证据不代表 G3 或 G2 通过。详见 [G2-C/D 证据](../p2-broker/g2c-g2d.md)。

最终验证：`powershell -NoProfile -File scripts/verify.ps1` 通过；C++ Release、CTest 12/12（含交互测试）、Rust 8/8、Release、console IPC 和 Service/VM 预览通过；独立 `cargo fmt -- --check` 与 Clippy `-D warnings` 通过。BrokerSmoke 在本机 console Service、GPU producer、Recorder、Viewer 下验证批准/拒绝路径。正式 SCM/LocalService 未运行；未运行 GPU 性能长测。

首轮 service smoke 在 Windows PowerShell 读取退出码时得到空值；提前缓存 Process.Handle 后重跑通过。管道采用有界回执，避免 DisconnectNamedPipe 丢弃未读响应。
最初尝试 `cargo fmt -- --check` 因当时缺少 rustfmt component 未成功；后续 CI 任务补齐工具并通过 1.85.1/stable 的 fmt、Clippy 和服务验证，见 [CI 证据](../ci/README.md)。

## 未覆盖与 G3 剩余

- 服务 IPC / Broker lease 控制在 console 原型已连通；正式 SCM/LocalService ACL、登录会话切换、跨账户拒绝和正式安装路径尚无实机证据。
- Service 进程重启会作废所有内存 envelope；策略持久化、升级/迁移、Broker 心跳与崩溃恢复仍未实现。REVOKE 阻止后续授权消费，不会抹除已交付像素或回收消费者已取得的句柄。
- 未实现策略持久化、签名/篡改恢复、迁移、主体注册、Broker 心跳与重启约束；所有授权状态默认仅内存，不在重启恢复旧 grant。
- 未运行 SCM 安装/启动/停止/重启/卸载、账户、ACL、升级和失败恢复；安装候选先保留数据，不宣称自动回滚。
- 无隔离测试机，日常机器未提权安装服务；这些结果不能通过 G3。

关联：[Risk RISK-006](../../RISK_REGISTER.md)、[TD-006](../../TECH_DEBT.md)、[Roadmap G3](../../ROADMAP.md)。
