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
| [0004](0004-private-broker-prototype.md) | 提议 | 自有启动树的私有通道、只读 NT texture 与不可变帧原型 |
| [0005](0005-service-diagnostics-prototype.md) | 提议 | Rust SCM/console、默认拒绝诊断 IPC 与 LocalService 部署候选 |
| [0006](0006-workspace-lifecycle-preview.md) | 提议 | 只读能力探针、VM 生命周期预览与未实现 Guest 通路 |
| [0007](0007-gpu-viewer.md) | 已接受 | TD-003 同 adapter GPU 输入、不可变快照与 Recorder CPU 边界 |
| [0008](0008-owned-child-enrollment.md) | 提议 | 自有工具挂起启动、真实身份快照、retained object 与撤销代际 |
| [0009](0009-authorization-envelope.md) | 已接受（设计边界） | SecureService 唯一 lease 真相源与 Broker 执行 Authorization Envelope |

0001 的接受依据为维护者本轮移植治理并首提交的指令；0002/0003 的依据为既有 Phase 1 要求与本轮记录后续阶段的指令。
0004–0006、0008 记录实际原型的待评审决策；不能借开发授权或既有 0003 视作正式安全设计/Gate 已接受。0009 接受并实现 G2-B 授权边界；G2-C/D 与 G2 Gate 仍未完成。
