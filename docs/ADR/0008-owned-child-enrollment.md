# ADR 0008：保留进程对象的自有工具 enrollment 切片

- 状态：提议
- 日期：2026-10-10

## 背景

TD-003 CPU 往返已消除；维护者要求先收敛 Phase 2 身份，不开放任意 PID 请求。现有精确继承管道与 retained process HANDLE 保留。

## 决策

原型只启动 Broker 自身（Producer/Record）或同目录 SecureViewer（View）；未知角色和不同路径在启动前拒绝。CreateProcess 使用 CREATE_SUSPENDED，从返回的真实 process HANDLE 读取 PID、creation time、primary token SID/session、image；与父主体和自有路径匹配后 ResumeThread。
每次交付检查 enrolled 角色和保留进程对象，重新读取同一对象的身份，不 CloseHandle 后按 PID attach。私有继承管道仍是启动树的能力；本切片不提供公开 enrollment/GRANT。
publisher/signature 显式 NotVerified，不把路径/SID/角色当独立可信证明。默认失败 deny；身份缓冲有界，SID/image 不写日志。
Broker 会话 generation 来自 BCryptGenRandom，非零、排除最大值，最多 8 次尝试，失败拒绝；撤销设置终态并 bump generation，关闭旧会话，不交付新帧。资源每帧新建，旧 handle 无未来写入。

## 后果

减少 PID 复用和错误主体寻址面；启动前读取真实身份并不证明二进制未被替换或进程未被篡改。
随机 generation 本身不是授权 token 或 signed lease。G2-B 已通过 ADR 0009 将版本化 envelope 与 Service 强制关系接入限定自有工具原型。G2-C 已增加 WinVerifyTrust + 构建时 SHA-256 publisher pin，但 VeilDesk 项目测试证书缺失，实际项目 enrollment 未测；G2-D 故障矩阵已开始，这不代表 G2/G3 收敛。详细实测见 [G2-C/D 证据](../evidence/p2-broker/g2c-g2d.md)。

## 替代方案

任意 PID attach、关闭后重新按 PID 打开、只靠路径或名称授权均拒绝。签名单独 也不能替代主体、会话、能力和代际约束。

## 关联文档

[ADR 0004](0004-private-broker-prototype.md)、[Contract](../CAPTURE_CONTRACT.md)、[Risk](../RISK_REGISTER.md)、[GPU 证据](../evidence/p2-broker/zero-copy.md)。
官方依据：[GetProcessTimes](https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-getprocesstimes)、[GetTokenInformation](https://learn.microsoft.com/en-us/windows/win32/api/securitybaseapi/nf-securitybaseapi-gettokeninformation)。
