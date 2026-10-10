# ADR 0005：Rust 服务与默认拒绝的诊断 IPC

- 状态：提议
- 日期：2026-10-10

## 背景

Phase 3 源码推进已获授权；G2 与正式服务身份契约尚未验收，日常机器服务安装未授权。

## 决策

候选为独立 Rust crate、windows-sys 精确版本 0.61.2 与 Cargo.lock。SCM 模式独立实现 ServiceMain、状态和 stop/shutdown；console 模式用于非特权真实 IPC 验证。
服务 IPC 为本机具名消息管道，固定请求上限 128 字节，单连接、一秒请求/回执预算；拒绝远程连接、拒绝其他协议版本与授予命令。默认不持久化、不授予、不启动 Broker。
SCM 管道 ACL 限 SYSTEM/Administrators/LocalService；console 管道额外允许 owner，用于测试，不能把 console 同用户成功当作正式 ACL 隔离证据。
候选服务账户为 LocalService，手动启动，不请求 LocalSystem 或管理员渲染。安装入口只预览，应用时把构建文件复制到受 ACL 保护的 Program Files 目录，并校验 SHA256；卸载保留文件。
策略内核限定会话、期限、资源预算、代际和撤销，进程启动用 BCryptGenRandom 创建新 epoch；服务管道尚不开放 grant，也不把内核状态当作对 Broker 的强制执行。

## 后果

服务 ABI 与 D3D11 职责独立，未确认授权默认拒绝。服务停止只关闭自身 IPC，尚不撤销独立 Broker；这属于未完成集成，G3 不能通过。
部署脚本、LocalService ACL、SCM 生命周期、持久化、升级/回滚需独立测试机验收；失败保留数据，不宣称自动恢复。

## 替代方案

LocalSystem 权限过大；在缺少主体注册和执行证据时开放远程 grant 会产生假授权。服务内直接承载 Viewer 将混入 Session 0 与渲染边界。

## 关联文档

[服务证据](../evidence/p3-service/README.md)、[Architecture](../ARCHITECTURE.md)、[Risk](../RISK_REGISTER.md)、[Gate](../ROADMAP.md)。
官方依据：[服务入口](https://learn.microsoft.com/en-us/windows/win32/services/service-entry-point)、[控制处理器](https://learn.microsoft.com/en-us/windows/win32/services/service-control-handler-function)、[LocalService](https://learn.microsoft.com/en-us/windows/win32/services/localservice-account)、[BCryptGenRandom](https://learn.microsoft.com/en-us/windows/win32/api/bcrypt/nf-bcrypt-bcryptgenrandom)。
