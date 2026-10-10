# TD-003 GPU Viewer 验证

日期：2026-10-10（Asia/Shanghai）；G2 未通过。机器为本地开发机（机器名不公开），Windows 11 Pro build 26200.9457，NVIDIA RTX 4060 / 32.0.16.1062，MSVC v143 / SDK 10.0.26100.0。

## 首轮实现验证

基线 `scripts/verify.ps1` 退出 0：Release、CTest 5/5、Rust 策略 4/4、console IPC；本次重构后同命令再次退出 0。Debug build 无警告；Debug 的 BrokerProtocol/BrokerSmoke/SharedTextureProbe 3/3。
命令：`cmake --build build --config Debug`、`ctest --test-dir build -C Debug -R 'Broker|SharedTexture' --output-on-failure`。
实际 approved Viewer 打开 8 帧，ZeroCopyStats 为 CPU readbacks=0、CPU uploads=0、opens=8、GPU copies=40（包含重绘）；WDA 已读回确认，DISPLAY_ONLY 创建接受。
Producer shader 输出经独立 readback 逐像素匹配诊断图案；Recorder PNG 独立解码匹配 frame ID。旧快照不变、非继承 HANDLE、真实 keyed mutex 超时通过。

## 扩展测试

GPU 实现为 `09d509e`；扩展测试与性能基线为 `9d04ebd`；Release/Debug 扩展构建无警告。最终 `scripts/verify.ps1` 退出 0：Release CTest 7/7、Rust 4/4、console IPC；Debug 全部 CTest 7/7。

| Expected | Actual |
| --- | --- |
| 正常 Viewer 不做 CPU 帧往返 | BrokerSmoke 读取真实退出日志断言 readbacks=0、uploads=0、opens=8、copies>0；源码结构检查无 Map/staging/CPU 帧 API，且未链接 readback 库 |
| Renderer GPU copy 正确且拒绝异 device | ViewerTextureProbe 在测试二进制内 readback 本地 snapshot，320/640×180 全像素匹配；源尺寸变化、resize、Present 通过；正常显示计数 0/0，测试独立计数 readbacks=2 |
| 不可信资源被拒绝 | 11 项 descriptor 单元负向输入及实际 321×180 texture 与 320×180 metadata 不符，被拒绝 |
| Adapter 边界 | 元数据 LUID 篡改拒绝；实际 Intel UHD 630（30.0.100.9805，LUID 00000000:000167d4）device 拒绝 NVIDIA 来源（LUID 00000000:00015667） |
| 旧 handle 无未来帧 | 产生 frame 2 后，frame 1→2→1 的全像素均匹配对应快照 |
| 超时不假成功/无限阻塞 | 第二 device 争用已锁定 frame，20ms 有限等待后拒绝；真实 WAIT_TIMEOUT |
| 子进程退出使会话失败 | producer 在首帧前退出、consumer 收到首帧后退出，Broker 非零退出、0 PNG，拥有的进程对象清理 |
| 授权与畸形协议 | 未授权、过期、撤销、重放、错 generation、错版本/尺寸，以及 64 个 magic 变体、未知 kind/status 被拒绝；授权状态测试不等于完整 OS 主体安全证明 |

## 1080p 性能基线

命令：`CaptureBroker --benchmark --output <新目录>`；GPU 图案→NT handle→独立 Broker 校验→独立 Viewer，1920×1080 源和 back buffer，目标 60FPS，单帧在途，持续 60 秒；benchmark 不运行 PNG Recorder。
三进程 aggregate private bytes / HANDLE 每秒采样；CSV/JSON/日志只在忽略目录。延迟从 Broker 帧请求到 Viewer Present 后 ACK，不是物理扫描延迟。统计 delivered 只在真实 Present 成功后计数；3600 目标槽位中未交付的计作 dropped。

| 指标 | 首轮 | 去除空闲重复 Present 后 |
| --- | --- | --- |
| 时长 | 60.0535s | 60.0272s |
| produced / displayed | 1071 / 1071 | 1117 / 1117 |
| dropped 目标槽位 | 2529 | 2483 |
| average / p95 delivery | 56.0647 / 72.0738ms | 53.7334 / 61.8244ms |
| 首/尾 private bytes | 131006464 / 170627072 | 132046848 / 173015040 |
| 10 秒后线性趋势 | +33743 bytes/s | +65799.8 bytes/s |
| 首/尾 HANDLE | 983 / 1256 | 975 / 1259 |
| Viewer readbacks / uploads | 0 / 0 | 0 / 0 |

Expected：零 CPU 帧往返、有界资源且无持续增长、接近 60FPS。Actual：零 CPU 往返通过，吞吐仅约 18FPS；初始化后 HANDLE 基本稳定，第二轮最后约 5 秒 private bytes 均为 173015040，仍有热身后增长。**资源长期稳定性与性能 Gate 未通过**，不能宣称无泄漏。
每进程 device 只在初始化创建；源码与初始化日志均无每帧 device 重建。没有观察到每帧 HANDLE 线性增长，但这不证明 GPU/driver 零泄漏；private bytes 不包括全部 GPU 分配。
决策（首轮）：保留 TD-005，尚未定位成本；后续定位见下文 TD-005 节。重复执行入口为 `scripts/benchmark-zero-copy.ps1`，同时断言零拷贝与尺寸/时长/帧统计；性能结果需人工评审。

可重复脚本第三轮（退出 0）：60.008s，produced/displayed=1148/1148、dropped=2452，average=52.2657ms、p95=60.5865ms；readbacks/uploads=0/0，private bytes=119476224→172818432，热身后趋势 +73899.5 bytes/s，HANDLE=919→1239。脚本的 1080p、60 秒、帧统计和零拷贝断言均通过；性能/资源 Gate 仍未通过。

## TD-005 吞吐定位（2026-10-10）

Broker benchmark 新增分段计时：produce（请求→producer 回帧）、inspect（Broker 打开校验）、view（投递→Viewer Present/ACK）。同一机器、命令不变。

| 运行 | displayed / 3600 | average / p95 | produce / inspect / view | 热身后趋势 | HANDLE 首/尾 |
| --- | --- | --- | --- | --- | --- |
| 修改前分段基线 | 1082 | 55.48 / 63.84ms | 26.10 / 0.46 / 28.91ms | 未记 | 未记 |
| 诊断：进程级 `timeBeginPeriod(1)`（已回退） | 3554 | 16.10 / 18.37ms | 3.22 / 0.19 / 12.69ms | 未记 | 892 / 1228 |
| 实现：每次等待的高精度 waitable timer | 3566 | 16.05 / 17.71ms | 2.21 / 0.20 / 13.64ms | +39514 bytes/s | 903 / 1258 |
| 实现复跑 | 3556 | 16.52 / 17.69ms | 未摘录 | +38775 bytes/s | — / 1253 |

结论：瓶颈是 `Receive` 的 `Sleep(2)`、Viewer `MsgWait` 8ms 与 pacing `Sleep(1)` 被系统 ~15.6ms tick 取整，不是每帧创建纹理（inspect<0.5ms，未单测 CreateTexture2D 本身）。改为 `PreciseTimer`（`CREATE_WAITABLE_TIMER_HIGH_RESOLUTION`，失败时记录并回退粗粒度等待），不改进程级计时器分辨率，也不改匿名继承管道的安全模型。view≈13ms 主要是 `Present(1)` vsync。
资源：private bytes 前 5 秒升至约 171.7MB，之后 10→55 秒 172.8→174.8MB 且增速递减；帧数增至约 3 倍而总增长未同比例上升，未观察到逐帧线性增长，但 60 秒不能证明有界。
决策：**不引入 texture pool / ring buffer**。吞吐已近 60FPS，池化需要复写已发布资源，会削弱"已发布快照不再写入"的撤销边界；若后续长时运行证明创建成本或资源不可接受，再按 generation 设计并单独评审。
未测：长时（≥30 分钟）资源曲线、GPU 专用内存、轮询 CPU/功耗、高精度 timer 不可用时的回退性能、其他 GPU/刷新率。

## 限制与 Gate

TD-003 的 CPU 显示往返已经消除并实测；Phase 1 测试绘制保持回归。资源池、scaling、device loss 恢复、真实 abandoned mutex、服务强制授权 NOT IMPLEMENTED/NOT TESTED，依对象而定。
OBS/WGC/DD/PrintScreen/会议共享、其他 OS/GPU、恶意主体/句柄转移和真实 PID 复用 NOT TESTED。零拷贝不是捕获保护的新增安全保证。
G2 未通过：身份/enrollment、统一服务授权事实源、在途撤销和 Broker 故障矩阵、共享 Viewer 捕获对照及性能/资源证据仍不完整。G3/G4 不推进。
历史原型结果保留在 [Broker 证据](README.md)，其 CPU 显示描述仅适用于历史版本。

后续 G2 身份/代际增量与剩余缺口见 [enrollment 证据](enrollment.md)，不将该增量当作性能复验。

G2-B Service envelope 接线后的有效 50 秒 1080p/60 FPS 性能复验为 1556/3000 个时隙完成显示（31.09 FPS），平均 produce 25.005ms、平均 delivery 30.925ms，未达目标；零 CPU readback/upload 保持 0/0。样本及 Service 生命周期断言见 [G2-C/D 证据](g2c-g2d.md)。G2-B 前的约 60 FPS 数据不可外推到 Service 授权后的路径。

G2-A 长测方法、判断标准与本轮资源记录见 [resource stability](resource-stability.md)；进行中的运行不构成长期稳定性证据。
