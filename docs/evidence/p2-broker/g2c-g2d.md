# G2-C Authenticode 与 G2-D 故障矩阵启动记录

日期：2026-10-10（Asia/Shanghai）。G2 仍未通过。源码基线 `a445dccca548ea13edf0a3a2852c1e20ac831f05`；G2-C 改动在工作树，未提交。环境为 Windows 11 Pro 25H2 build 26200.9457、x64 MSVC v143 / SDK 10.0.26100.0、NVIDIA RTX 4060 / LUID `00000000:00015667`。构建目录和原始输出均在 `build/` 忽略目录。

## G2-C 实现与验证

- `ProcessIdentity` 通过 `WinVerifyTrust` 的 Authenticode 通用文件策略验证映像；签名者证书 SHA-256 必须与构建时 `VEILDESK_AUTHENTICODE_PUBLISHER_SHA256` pin 完全相同。无效、未签名、信任链失败、空 pin 或发布者不匹配均拒绝 Broker 子进程 enrollment；Broker 自身和授权 Service 对端也必须匹配同一 pin。`WTD_CACHE_ONLY_URL_RETRIEVAL` 禁止联网刷新，撤销判断只基于本地缓存。
- `AuthenticodeProbe` 用本机 CMake 签名映像做正向链验证；准确 pin 接受，错 pin、未签名 Probe 和签名映像字节修改均拒绝。`ProcessIdentityProbe` 验证本机无 pin 构建默认拒绝。63 位 pin 的 CMake 负向配置被拒绝。
- `ctest --test-dir build -C Release -R 'AuthenticodeProbe|ProcessIdentityProbe' --output-on-failure`：2/2 通过。
- `powershell -NoProfile -File scripts/verify.ps1 -SkipInteractive`：退出码 0；Headless CTest 7/7、Rust 策略 8/8、console Service 和只读 Workspace 检查通过。Viewer/捕获/共享 GPU 交互测试按 `-SkipInteractive` 跳过。
- 本机没有 VeilDesk 测试发布者证书/私钥；因此项目自身证书 enrollment、用该证书签署所有自有映像后的正向路径未测。CMake 的 Microsoft Visual Studio 安装发现也不完整，测试使用当前 CMake 可执行文件（本机签名有效）作为独立 WinVerifyTrust 样本，不将其说成 VeilDesk 发布者验收。
- 校验对象是映像路径当前文件；没有证明该文件与已映射进程内存逐字节一致，也不防同用户运行时 patch。签名撤销缓存新鲜度未测；映像替换、同用户篡改等仍开放。
- 未配置 pin 的开发构建会 fail-closed，BrokerSmoke 和 GPU benchmark 的当前构建不能启动自有子进程；需具备项目签名证书并用同一证书签署 Broker、Viewer、producer/consumer 与 Service 后再跑集成验收。

## G2-B 后 60 FPS 性能复验

有效样本来自 HEAD 源码的独立 Release 构建（`build/g2b-head` → `build/g2b-release/Release`），避免 G2-C 新增的空 pin fail-closed 阻断旧 G2-B 测量。命令：

```powershell
powershell -NoProfile -File scripts/benchmark-zero-copy.ps1 `
  -BuildDir build/g2b-release/Release -ResultDir build/test-results `
  -ServiceExe service/target/release/veil-secure-service.exe `
  -DurationSeconds 50 -Width 1920 -Height 1080 -Fps 60 -SampleSeconds 5
```

正式结果目录：`build/test-results/zero-copy-a4f180bbdcab48b489725ae9ce98eaa4`。复验脚本退出码 0；console Service 在样本期间持续运行，Broker 完成停止路径且没有授权拒绝/授权管道错误。

| 指标 | 实测 |
| --- | ---: |
| 时长 / 分辨率 / 目标 | 50.0526 秒 / 1920×1080 / 60 FPS |
| target slots | 3000 |
| produced / displayed / dropped | 1556 / 1556 / 1444 |
| 实际显示速率 | 31.09 FPS |
| 平均 / p95 / p99 delivery | 30.925 / 35.247 / 38.360 ms |
| 平均 produce / inspect / view | 25.005 / 0.193 / 5.724 ms |
| private bytes 首/尾（聚合指标） | 165,384,192 / 180,178,944 |
| handles 首/尾（进程采样汇总） | 1244 / 1289 |
| Viewer CPU readbacks / uploads | 0 / 0 |

**60 FPS 性能目标未通过**：目标时隙 48.1% 未显示。`produce` 区间从 Broker 开始该帧操作前计时，包含 Service 每帧 envelope 消费 IPC 与 producer 响应；当前数据没有把两部分分开。与 G2-B 前约 2.2 ms 的 produce 数据相比，服务授权路径是后续诊断重点，但单次测量不能单独证明全部差值来自 Service。未触发本次资源趋势脚本的调查阈值；50 秒不能证明长期资源有界。

废弃样本：一次 60 秒运行时 console Service 的 60 秒生命周期先结束，Broker 记录管道结束及拒绝授权；该结果不作为有效性能证据。脚本现把运行限制在 50 秒，并检查 Service 生命周期和 Broker 授权错误，避免把该情形报告成完整复验。

## G2-D 已开始：有限故障场景

在相同 G2-B HEAD Release、console Service 和 RTX 4060 环境运行：

| 场景 | Actual | 结论 |
| --- | --- | --- |
| producer 首帧前退出 | 退出码 1；完成 PNG=0、pending PNG=0 | 通过本场景的拒绝与清理断言 |
| consumer 收到帧后退出 | 退出码 1；完成 PNG=0、pending PNG=0 | 通过本场景的拒绝与清理断言 |
| Service 撤销后续授权（`--revoke-after 2`） | 退出码 0；交付恰好 2 帧；pending PNG=0 | 通过已测帧边界；不表示在途 ACK/已交付像素可追回 |
| Viewer 正常显示的 BrokerSmoke 场景 | 首帧 Present 返回 occluded，测试退出码 1 | 未评审；无有效 Viewer 故障结论 |

本地 G2-D 结果目录：`build/test-results/g2d-smoke-2edc3a0726f04f1d906671da92546f46`。Broker 崩溃、在途 revoke race、producer/consumer 的真实强制终止、DXGI device removed/reset 尚未注入；已交付像素不可追回仍是边界。G2-D 正在进行，不能据局部矩阵关闭 G2。

## 后续代码审查修正（待 Windows 验证）

审查发现原 CMake pin 检查使用了不受 CMake 正则支持的 `{64}` 重复语法；现改为长度与十六进制字符分开检查，并加入 `CMakePresets.json` 统一 Windows x64 Release 配置。Rust Service 现在也在首次接纳 Broker 时执行 WinVerifyTrust 与同一 SHA-256 pin；C++ 与 Service 的 pin 都从 `VEILDESK_AUTHENTICODE_PUBLISHER_SHA256` 构建环境读取，空 pin 继续拒绝。Broker/Service 的签名验证结果在保留 PID、创建时间与映像路径后缓存，逐帧只复核实际进程身份，避免每帧调用 WinVerifyTrust。

本次改动工作区为 macOS arm64，未安装 CMake、Windows SDK 或 MSVC，且没有项目签名证书；新增 Rust WinTrust FFI、Windows CMake preset、正负 pin 配置以及签名性能路径均待 Windows x64 验证，不据源码改动宣称 G2-C 通过。
