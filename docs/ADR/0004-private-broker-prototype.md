# ADR 0004：自有启动树内的 Broker 原型

- 状态：提议
- 日期：2026-10-10

## 背景

维护者明确授权继续 Phase 2–4。先实现能在本机验证的自有内容切片；实现授权不代表维护者已接受通用身份模型或 G2。

## 决策

候选实现采用独立 C++ producer、Broker、Viewer 与 PNG 序列消费者。Broker 仅启动固定自有程序，保留 CreateProcess 返回的进程对象，通过精确继承列表交付私有匿名管道；不允许按路径/PID 附加外部消费者。
共享纹理为匿名、非继承、只读 NT handle；父进程从 producer 进程对象复制，再复制给已授权子进程。
每帧创建不可变资源，320×180 BGRA8、同一默认硬件 adapter；keyed mutex 和固定长度 ACK 为同步边界。
授权是本地操作员启动测试切片时显式允许的子进程角色。lease/代际/帧序号拒绝过期与重放；撤销停止新资源交付，已有快照不追回。
Viewer 保持自己的 WDA；可信输出直接来自 producer，不截取 Viewer。

## 后果

保留进程对象避免重新按 PID 寻址导致复用；资源名称不公开，精确继承列表避免其他句柄意外交付。
本切片不防御同用户进程内存篡改、合法消费者转移句柄、代码替换或管理员；不能代替 G2 的生产身份方案。
Viewer GPU consumer 的后续决定见 [ADR 0007](0007-gpu-viewer.md)；每帧资源创建成本与长期资源使用需要独立证据。独立 producer 退出使通道失败，恢复依赖新会话而非隐式回退。

## 替代方案

按进程名/路径/PID 直接授权不足以证明身份；公开命名纹理扩大寻址面；连续改写同一纹理会让旧句柄得到未来内容。通用具名 IPC、签名主体注册和资源池留待证据支持的后续设计。

## 关联文档

[Contract](../CAPTURE_CONTRACT.md)、[Broker 证据](../evidence/p2-broker/README.md)、[Risk](../RISK_REGISTER.md)、[Gate](../ROADMAP.md)。
官方依据：[CreateSharedHandle](https://learn.microsoft.com/en-us/windows/win32/api/dxgi1_2/nf-dxgi1_2-idxgiresource1-createsharedhandle)、[OpenSharedResource1](https://learn.microsoft.com/en-us/windows/win32/api/d3d11_1/nf-d3d11_1-id3d11device1-opensharedresource1)、[AcquireSync](https://learn.microsoft.com/en-us/windows/win32/api/dxgi/nf-dxgi-idxgikeyedmutex-acquiresync)、[handle list](https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-updateprocthreadattribute)。
