# 捕获与授权语义

## 当前测试语义

CaptureTest 使用客户区对应的屏幕像素矩形进行普通 BitBlt，随后编码 PNG。
API 成功只说明复制/编码完成，不说明 Viewer 内容存在于输出中。
标记出现是获得部分测试画面的证据；标记缺失可能是排除、遮挡、黑屏或无效环境，需要无遮挡无保护基线和人工检查。
错误、空区域、隐藏/最小化、屏幕外区域等不计作保护通过。命令/退出码见 [README](../README.md)。

## Phase 2 原型协议

原型为本机 x64 v2 固定 64 字节消息，magic/version、Kind/Status、generation/frame、NT handle、boot-relative deadline、尺寸；定义在 `src/broker/Protocol.h`。
只允许自有启动树中的进程，以私有继承管道作为能力；父进程保留实际 process HANDLE，而非重新按 PID 打开。
常规帧为 320×180 BGRA8，明确 LUID 的同一硬件 adapter，跨 adapter 拒绝且无 CPU fallback；keyed mutex 为 0→1，读者 AcquireSync(1) 后 ReleaseSync(1)，超时/abandoned 不能按 SUCCEEDED 算成功。
ACK 必须匹配帧、代际和期限，不带句柄；对端中断/超时使会话失败，关闭通道并清理自己启动的进程。
lease 默认最多 30 秒，输出最多 120 帧；到期/撤销后不交付新资源。每帧纹理不再改写，旧句柄可以继续读取旧快照，不能召回已保存像素。
PNG 写入显式新目录；临时文件编码完成才改名，拒绝覆盖既有文件，失败清理未完成输出并保留已完成帧。
契约原型与证据见 [ADR 0004](ADR/0004-private-broker-prototype.md) / [Broker 证据](evidence/p2-broker/README.md)。以下是扩大到通用客户端仍必须满足的验收语义：

| 对象 | 必需语义 |
| --- | --- |
| 内容源 | 源身份、资源 owner、设备/adapter、格式、尺寸、版本及 generation |
| 共享资源 | 有界句柄数量、非继承策略、仅向授权目标交付、同步和销毁责任 |
| 授权 lease | 验证过的主体、会话、允许操作、期限、显式审批依据和撤销状态 |
| 帧交付 | 单调 frame ID、时间语义、丢帧/超时、同步失败与目标 generation |
| 错误 | 身份拒绝、过期、资源失效、跨 adapter 不支持、设备丢失、取消与断连 |
| 撤销 | 阻止新帧/新资源；说明已交付句柄与在途帧的有效范围，必要时轮换资源 |
| 录制输出 | 有界写入、显式目的地、取消/磁盘错误终态、完整性检查和残留清理 |

PID、路径、进程名不能单独充当身份凭据；不能把“同一用户可访问对象”当作消费者可信保证。
授权不得靠全局关闭 WDA 来放行目标程序，可信录制从 Broker 内容源获得画面。
撤销无法召回消费者已保存的像素，也不能在未验证时承诺旧资源马上不可读。
本原型不提供任意外部客户端注册、代码签名认证或防止同用户进程篡改/窃取句柄的边界；通用授权能力仍需 G2 审查。

## 后续生命周期

Phase 3 收敛服务重启、策略持久化和 broker generation；服务不可用时的拒绝/恢复必须可解释。
Phase 4 为 Guest / Host 输入、显示源、会话身份与外部传输制定独立协议，不能直接把本进程纹理当作虚拟机边界。
每个协议变更都有版本、兼容策略与负向测试。Gate 验收见 [ROADMAP](ROADMAP.md)，工作包见 [DEVELOPMENT_BLUEPRINT](DEVELOPMENT_BLUEPRINT.md)。
