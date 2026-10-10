# ADR 0009：Service 权威 Authorization Envelope

- 状态：已接受
- 日期：2026-10-10

## 背景

G2-A 长测已记录 1800 秒资源曲线与轮询 CPU，但电功耗和高精度 timer 不可用时的回退性能未测。维护者明确要求关闭 G2-A，并立即进入 G2-B；该接受仅关闭 G2-A 的阶段审查，不代表所有测项已实测，也不通过 G2。

当前 Broker 在进程内自行创建 lease；Rust SecureService 有独立策略状态但没有 GRANT IPC，也不约束 Broker。两份状态不能成为授权真相源。G2-B 的目标是消除该分叉，且不改变 GPU 渲染/同步/资源策略。

## 决策

- SecureService 的内存策略是唯一 lease 真相源；重启产生新的随机 boot epoch，旧 envelope 一律失效。持久恢复授权不在本 ADR 范围内。
- Envelope 必须版本化并绑定 `lease_id`、boot `epoch`、`generation`、主体身份快照（PID、creation time、SID、session、image 身份及角色）、操作范围、签发时刻、基于 boot-relative 单调时钟的 deadline、单调 nonce 和最大帧/资源预算。
- Service 负责签发、续期、撤销和 generation 递增；Broker 不得自行签发、延长或恢复 lease，只能持有 Service 返回的 envelope，并在每帧交付前向 Service 原子消费全部适用 envelope。缺失、未知版本、字段不一致、过期、重放、降级或 Service 不可用均默认拒绝。
- 完整性通过受 ACL 保护的本机 IPC 通道及已验证的 Service 对端身份保障；协议设计需证明 Broker 如何认证实际 Service 进程、会话和响应关联。未经 ADR/实现证据不得把裸管道、PID、路径、名称或共享密钥假定为完整性保护。
- 同一 Broker 会话使用一个持久授权管道；连接建立时双向验证对端的真实进程主体、会话与 Authenticode publisher pin，Service 保留 Broker 进程对象，直至断连、进程退出或 2 秒空闲超时。每个有界请求仍用递增 request ID 关联响应；断连后必须重新认证，不能把旧连接的认证状态带到新连接。
- 首个实现限定当前自有 producer / Broker / Viewer / Recorder 原型；不增加任意客户端 enrollment，不引入 GPU pool，不改变帧同步或渲染路径。已交付像素不可追回，旧句柄撤销边界仍需 G2-D 证据。
- G2-B 完成定义：Rust Service 与 Broker 共用一个 envelope 数据契约；Service 持有唯一 lease 状态；Broker 每次交付前依据该状态验证；伪造、过期、重放、降级、generation 不匹配和 Service 中断均有负向证据。文档接受不代表实现或 Gate 通过。

## 后果

2026-10-10 实现增量：C++ Broker 保持已验证的授权管道并逐帧复用；Rust Service 在连接建立时以 WinVerifyTrust 验证 Broker Authenticode 信任链，并比较构建 pin 中的 signer certificate SHA-256。`verify.ps1` 从同一 CMake cache 读取 pin 并传递给 Cargo；空/格式错误 pin、信任失败或 publisher 不匹配默认拒绝。Service 保留 pipe client process HANDLE 并在请求时检查该进程仍存活。该增量尚无项目发布证书正向集成验证，也无持久连接性能复测。

诊断 STATUS/REVOKE 管道保持 GRANT 默认拒绝；授权由独立管道承载，Broker 无本地授权回退。Service 使用 30 秒 lease，并在帧数或累计字节预算耗尽前续签；服务重启换 epoch，旧 envelope 失效。console 端到端证据不能替代正式 SCM/LocalService 运维验证。

当前进度（2026-10-10）：Rust Service 与 C++ Broker 共用 152 字节固定小端 envelope 编码；Service 单独持有 lease 状态，通过独立有界消息管道签发/续期、消费和撤销。Service 从实际客户端进程句柄读取 PID、创建时间、会话、SID 和映像路径摘要；SCM 模式额外限定安装目录中的 CaptureBroker 路径。Broker 检查 Service 对端 SID/映像路径（console 测试模式绑定预期 PID、同 SID/session 和测试映像），每帧交付前原子消费 RECORD/VIEW envelope，Service 核验逐 lease 单调帧号与累计字节预算。测试覆盖批准交付、Service 过期拒绝、撤销后的拒绝，以及策略层伪造/过期/重放/预算/未知 scope 拒绝。G2-B 已完成实现与限定验证；SCM/LocalService 实际运行、Publisher/Authenticode enrollment、同用户恶意映像与 G2-D 在途故障仍未验证，不能宣称 G2 通过。

G2-A 的电功耗及 timer 回退性能作为已接受未测项登记；不得据此作功耗或低分辨率 timer 下性能承诺。G2/G3/G4 仍未通过。

## 替代方案

- Broker 继续本地维护 lease：拒绝，会保留两个授权真相源。
- 以 PID、image 路径或命名管道名称单独认证 Service：拒绝，不能证明实际对端身份。
- 共享裸 HMAC key：暂不采用，除非密钥建立、保护、轮换和重启失效机制有可审查证据。
- Service 只做诊断、Broker 自行授权：拒绝，不符合 G2-B 目标。

## 关联文档

[G2 证据](../evidence/p2-broker/README.md)、[Service 证据](../evidence/p3-service/README.md)、[授权契约](../CAPTURE_CONTRACT.md)、[工作包](../DEVELOPMENT_BLUEPRINT.md)、[G2 Gate](../ROADMAP.md)、[RISK-003/004/006](../RISK_REGISTER.md)。
