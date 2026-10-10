# Phase 3 Rust 服务原型证据

日期：2026-10-10（Asia/Shanghai）；P3-01/02 基础切片、P3-03 策略内核；G3：未通过。
源码版本：基于 `b699715` 的本轮未提交工作树；服务在 `service/`，部署候选在 `scripts/service-control.ps1`。

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
| Rust 策略单元测试 | 4/4：会话/代际/过期/撤销、重启后旧 grant、预算/溢出、协议和远程 grant 默认拒绝 |
| Rust Release | 锁定依赖编译/链接通过；SCM 入口存在，但未以 Windows 服务启动 |
| console IPC | 实际具名消息管道：状态返回、新随机 epoch、错误版本拒绝、GRANT 拒绝、REVOKE 代际递增、超大请求断开、8 秒结束通过 |
| IPC 资源/超时 | 请求/回执各最多一秒，单实例、128 字节输入；不做阻塞 FlushFileBuffers，结束后关闭管道 |
| 部署预览 | Install 无 Apply 仅输出计划，LocalService/手动启动/不自动启动，通过 smoke；未部署 |
| 完整回归 | `scripts/verify.ps1` 退出码 0；Rust 4/4、Release、console IPC 与服务/VM 预览检查通过；SCM 仍未运行 |

首轮 service smoke 在 Windows PowerShell 读取退出码时得到空值；提前缓存 Process.Handle 后重跑通过。管道采用有界回执，避免 DisconnectNamedPipe 丢弃未读响应。
最初尝试 `cargo fmt -- --check` 因当时缺少 rustfmt component 未成功；后续 CI 任务补齐工具并通过 1.85.1/stable 的 fmt、Clippy 和服务验证，见 [CI 证据](../ci/README.md)。

## 未覆盖与 G3 剩余

- G2/服务信任边界 ADR 未接受；只有 console owner 的实际连接，不是正式 LocalService ACL 或跨用户/会话拒绝验收。
- 服务 IPC 只支持 STATUS/REVOKE，GRANT 默认拒绝；策略内核没有连接 Broker。REVOKE 只改变服务自身状态，不能宣称撤销独立 Broker 的 lease。
- 未实现策略持久化、签名/篡改恢复、迁移、主体注册、Broker 心跳与重启约束；所有授权状态默认仅内存，不在重启恢复旧 grant。
- 未运行 SCM 安装/启动/停止/重启/卸载、账户、ACL、升级和失败恢复；安装候选先保留数据，不宣称自动回滚。
- 无隔离测试机，日常机器未提权安装服务；这些结果不能通过 G3。

关联：[Risk RISK-006](../../RISK_REGISTER.md)、[TD-006](../../TECH_DEBT.md)、[Roadmap G3](../../ROADMAP.md)。
