# Architecture Decision Records

ADR 说明长期决定及理由；Gate 与执行顺序分别在 Roadmap / Blueprint。
状态为提议、已接受、已替代、已拒绝；已接受不等于已实现或能力已验证。

文件名 `NNNN-short-title.md`，必须包含状态、ISO 日期、背景、决策、后果、替代方案和关联文档。
改变已接受边界时新增替代 ADR，保留历史和双向链接；模板见 [TEMPLATE](TEMPLATE.md)。

| ADR | 状态 | 决定 |
| --- | --- | --- |
| [0001](0001-governance-entry.md) | 已接受 | 从 uNAS 参考移植单一事实源、路由、预算与证据治理 |
| [0002](0002-controlled-display-poc.md) | 已接受 | Windows 官方受控显示 PoC，禁止注入/Hook，捕获限制不当作完整安全边界 |
| [0003](0003-staged-secure-workspace.md) | 已接受 | 保留 Phase 2 Broker、Phase 3 Rust 服务、Phase 4 Hyper-V 的顺序，独立授权与 Gate |

0001 的接受依据为维护者本轮移植治理并首提交的指令；0002/0003 的依据为既有 Phase 1 要求与本轮记录后续阶段的指令。
精确共享资源/IPC/身份方案尚未锁定，需后续独立 ADR，不能借 0003 视作全部实现细节已接受。
