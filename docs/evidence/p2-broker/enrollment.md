# G2 自有工具身份与撤销增量

日期：2026-10-10（Asia/Shanghai）；基于 `9d04ebd` 的后续身份增量。G2 未通过。
环境同 [GPU 证据](zero-copy.md)：本地工作机，Windows 11 Pro 26200.9457，MSVC v143、x64；GPU 回归为 RTX 4060，第二硬件 adapter 为 Intel UHD 630。原始日志留在 build 忽略目录。

## 已实现边界

Broker 仅自身 Producer/Record 和同目录 SecureViewer/View；角色在 Broker 内赋值。未知角色、路径、身份查询失败、SID/session/image 不匹配默认 deny。挂起 CreateProcess，读取真实对象的 PID/creation time/primary SID/session/image 后才 ResumeThread。
交付前核对同一个 retained process HANDLE 的身份和存活状态；不调用 OpenProcess 按 PID attach。private inherited pipes 仍是 capability；未开放任意消费者入口。
`ProcessIdentity.publisherState=NotVerified`，publisher 为空，未声称签名安全；路径与同 SID 不是完整信任证明。
每次 Broker 会话由系统 RNG 生成非零 generation；撤销 bump、停止 handle delivery、关闭旧 session。每帧新资源，旧 handle 的像素不召回，也不接收新资源的未来写入。
此文记录 P2-03/G2-B 接通前的 Broker 本地 enrollment 基线。当前授权状态已由 [ADR 0009](../../ADR/0009-authorization-envelope.md) 定义：Service 签发并逐帧核验 envelope；本文件关于签名者未验证、同用户映像篡改和句柄转移的限制仍有效。

## 测试命令与结果

命令：`ctest --test-dir build -C Release -R 'ProcessIdentity|Broker|SharedTexture|ZeroCopy|ViewerTexture' --output-on-failure`。
相关测试 6/6 PASS；最终 `scripts/verify.ps1` 退出 0，Release 全部 8/8，Debug 全部 8/8；两种构建无 /W4 警告。Rust pinned 1.85.1 的策略 4/4、console IPC、fmt/clippy -D warnings 通过；stable 的 fmt/clippy 和策略 4/4 通过。

| Expected | Actual |
| --- | --- |
| 从真实对象读取身份 | 本进程及挂起创建的子进程成功，primary SID/session 与父主体匹配，creation time 非零 |
| 错主体拒绝 | PID/creation time/session/SID/image 五种篡改快照均拒绝；是注入快照测试，不是实际 PID 复用或跨 session 攻击 |
| 未 enrollment 不启动 | None 角色和非自有路径在 CreateProcess 前拒绝 |
| 进程退出拒绝 | 保留子进程 HANDLE 的额外副本，子进程结束后仍指向旧对象且拒绝交付 |
| 撤销代际 | Revoke 增加 generation，旧有效消息得到 Revoked；实际 revoke-after 2 输出恰好 2 帧，退出旧会话 |
| 既有显示/资源行为 | BrokerSmoke 的 Viewer 0 readback/0 upload、Producer/Consumer 退出、超时、旧纹理、真实跨 adapter 等回归通过 |

## 限制 / Gate

publisher/signature、二进制替换、恶意同用户进程/句柄转移、真实 PID 复用、真实 session 切换、在途 revoke、Broker crash/device loss 未验收。
canonical lease_id/subject/session/scope/issued_at/expires_at/nonce 与 Service 强制控制已在 G2-B 限定实现；Publisher/Authenticode enrollment 仍待 G2-C。随机 generation 不是单独授权 token。
性能数字来自 `9d04ebd` GPU 工作包（约 19FPS、热身后 private bytes 增长）；身份增量尚不构成性能复验或无泄漏保证。
G2 仍未通过，未推进 Phase 3/4、SCM 安装或系统策略/虚拟化修改。当前入口与优先级见 [Context](../../../CONTEXT.md)。
