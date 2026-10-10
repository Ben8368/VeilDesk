# Phase 2 Broker 原型证据

日期：2026-10-10（Asia/Shanghai）；工作包：P2-01..05 的限定原型切片；G2：未通过。维护者已限定接受 G2-A 未测项并关闭 G2-A；G2-B 实现与限定验证完成，授权路径后测约 31 FPS；G2-C 项目证书 enrollment 未完成，G2-D 故障矩阵进行中；详见 [G2-C/D 与性能证据](g2c-g2d.md)。
源码版本：基于 `b699715` 的本轮未提交工作树；源文件在 `src/broker/`、Viewer 输入与 CMake。原始日志/PNG 留在受忽略结果目录。

## 环境与输入

本机 Windows 11 Pro build 26200，NVIDIA RTX 4060 驱动 32.0.16.1062；MSVC v143、SDK 10.0.26100.0、CMake 4.4.4、x64 Release /W4。
自生成 320×180 BGRA8，像素编码单调 frame ID 和坐标，保留 Phase 1 彩色标记；不是截图/第三方应用源。
独立 producer/Broker/Viewer/recorder 进程，同默认硬件设备；匿名只读 NT handle、私有精确继承通道、keyed mutex。

## 实际命令与结果

```powershell
cmake --build build --config Release
ctest --test-dir build -C Release -R 'Broker|SharedTexture' --output-on-failure
powershell -NoProfile -File scripts/verify.ps1
```

| 用例 | 实际结果 |
| --- | --- |
| BrokerProtocol | 未授权、版本、过期、撤销、重放、generation/尺寸错误的状态测试通过；这不是 OS 身份测试 |
| approved | 8 帧通过独立 producer→Broker→消费者；逐像素匹配；Viewer 共享输入正常结束，WDA 必须确认，可信路径不关闭 WDA |
| PNG 独立解码 | System.Drawing 解码所有完成帧，320×180 与像素内 frame ID 匹配；不是只断言文件存在 |
| revoke-after 2 | 输出恰好 2 帧；后续帧不交付；正常 stop/ACK 与进程退出 |
| expired / deny | 输出 0 帧；资源交付前拒绝 |
| stalled consumer | 真实 ACK deadline 失败，以非零退出；不输出帧，清理自有子进程 |
| 未附加消费者/producer | 无私有管道能力时拒绝，以非零退出 |
| 输出冲突 | 已有目录拒绝，不覆盖完成输出；PNG 采用临时文件后不覆盖改名 |
| SharedTextureProbe | 已产生新帧时旧句柄仍读到旧帧；新帧像素正确；共享 HANDLE 非继承；错误 key 的实际 AcquireSync 返回 WAIT_TIMEOUT |
| 完整回归 | `scripts/verify.ps1` 退出码 0；/W4 Release 无警告；CTest 5/5，包括 Phase 1 WindowSmoke/CaptureSmoke；自动标记断言不替代完整人工/外部捕获矩阵 |

首次 BrokerSmoke 因 PNG 消费者缺 COM 初始化失败；修复 CoInitializeEx/RAII 后重跑通过。没有将该失败计作通过。

## 生命周期与能力限制

每帧新纹理、一帧在途；完成 ACK 后释放 HANDLE。通道失败停止会话、关闭管道，并只终止自己创建且保留进程对象的子进程；部分完成 PNG 保留，未完成 PNG 清理。
旧资源和已保存像素不能追回；不可变纹理只保证原型不向旧资源再写未来帧。
本机 3/3 新 CTest 通过，包括一次实际 GPU 同步超时；未据此宣称长期 GPU/driver 零泄漏、故障自动恢复或性能。

## G2-B Service 授权接线

Broker 现在不再自行决定授权：从 SecureService 独立授权管道取得 RECORD/VIEW envelope，在每帧向 producer 请求纹理前将所有适用票据、当前租约内帧序号与 BGRA 字节数一起提交 Service。Service 精确核验主体/lease/epoch/generation/deadline，并原子递增单调帧号与累计字节；拒绝时 Broker 不向消费者交付共享 HANDLE。票据接近 30 秒期限或累计预算耗尽时向 Service 续签。
console 端到端场景中 approved 交付 8 帧；撤销后只交付此前 2 帧；过期、伪造 PID、单帧超预算和 Service 不可用均无新帧；错误 scope 拒签；重放在 1 帧后拒绝。策略单元测试覆盖伪造主体/epoch/generation、过期、重放、累计预算原子失败与未知 scope。完整 `scripts/verify.ps1` CTest 12/12；实现限制和 SCM 未测项见 [Service 证据](../p3-service/README.md)。本段不代表 G2 总体验收，也不新增 GPU 性能结论。

## 未覆盖与 G2 剩余

- G2 启动设计/威胁范围未获维护者有限接受；原型无通用签名主体注册，不能防同用户进程篡改或句柄窃取/转移。
- PID 复用通过保留进程对象规避，但未制造真实 PID 复用或恶意句柄转移实测。
- producer/consumer 异常退出、Broker 崩溃、在途撤销、GPU/device removed/reset、尺寸重建和资源峰值仍需故障矩阵；固定尺寸不支持动态源大小。
- 跨 adapter 未验证，不支持承诺；Viewer 经 CPU readback/上传，未交付零拷贝或生产录屏格式。
- 共享模式的普通捕获/可信输出同场景对照、OBS/WGC/DD、其他 OS/GPU、多显示器及长期性能尚未验收。

关联：[ADR 0004](../../ADR/0004-private-broker-prototype.md)、[Contract](../../CAPTURE_CONTRACT.md)、[RISK-003..005](../../RISK_REGISTER.md)、[Roadmap G2](../../ROADMAP.md)。
