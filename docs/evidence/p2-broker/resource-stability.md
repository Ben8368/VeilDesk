# G2-A 长时间资源稳定性

日期：2026-10-10（Asia/Shanghai）。仅自有测试 producer → Broker → protected Viewer；授权事实源仍是既有原型，不能据本工作包关闭 G2。

## 复现与测量

先运行完整 `scripts/verify.ps1`、Debug build/CTest、Rust fmt/clippy/tests。长测命令：

```powershell
powershell -NoProfile -File scripts/benchmark-zero-copy.ps1 -DurationSeconds 1800 -Width 1920 -Height 1080 -Fps 60 -SampleSeconds 5
# 或在新的 ignored 工作目录直接运行：
CaptureBroker --benchmark --duration-seconds 1800 --width 1920 --height 1080 --fps 60 --sample-seconds 5 --output metrics
```

CLI 有界：duration 1..7200s、fps 1..240、width 216..4096、height 36..4096、sample 1..60s；参数只用于显式 benchmark。Viewer/lease 预算为 duration+30s。无 PNG、外部消费者入口或 texture pool；一帧在途，每帧新建不可变纹理。

- produced：收到有效 producer Frame；displayed：Viewer Present 后匹配 ACK，随后完成 producer ACK。dropped：目标时间槽减 displayed（尚未生产的跳过时间槽），不称作丢弃已生产帧。中断使运行失败，部分 CSV 保留。
- 平均/p95/p99：Broker frame request → Viewer Present/ACK → producer ACK 的 steady-clock 时长；nearest-rank 百分位。不测物理 scanout 或用户实际看到画面的时刻。
- 每 5 秒逐进程采样 private bytes/working set（GetProcessMemoryInfo）、HANDLE（GetProcessHandleCount）、线程（Toolhelp snapshot）、CPU（GetProcessTimes 相邻样本差）；CPU 100%=一个逻辑处理器，不按核心数稀释。
- PDH English wildcard GPU Process Memory / GPU Engine，严格匹配 retained PID 的实例；dedicated/shared 为各实例求和，utilization 为最忙 engine，不能将 engine 相加解释为整卡百分比。有效 CStatus 才使用；缺失/错误/非有限值写 NOT AVAILABLE，不写零。首个 CPU 样本无区间，明确缺测。
- GPU 为 WDDM 进程计账，跨进程共享分配可能重复计账；不是独占物理分配或完整 COM/driver object 数。未提供可靠逐对象计数：NOT AVAILABLE。工具不声称证明没有任意种类泄漏。
- latency 向量预留全部 target slots，resource 向量预留采样预算；记录增长有明确上界。CSV 每周期 flush，原始日志、CSV、曲线及硬件/二进制 hash 留在 ignored build/test-results，不提交桌面图像。

## 判断标准

`scripts/analyze-benchmark.ps1` 生成全程曲线与 JSON：排除前 120 秒做时间/每 displayed frame 回归；保留六个完整五分钟窗口的 min/median/max；检查启动、热身和整个运行，不能只看末几秒。

明显阻断模式：HANDLE/线程/CPU private bytes 随帧数持续线性增长；GPU dedicated 在多个窗口继续上升且没有回落或平台。需要查增长的主体和资源 owner；不得自动引入 pool。shared/working set 与 private/dedicated 交叉检查；单独 cache 高水位不直接归类泄漏。

脚本的调查阈值（热身后首尾增长：HANDLE >16、threads >2、private >32MiB、dedicated >64MiB 且正斜率）只提醒调查，**不是低于阈值即可 PASS**。小幅但持续的窗口基线增长仍须人工评估；分析输出始终 REVIEW REQUIRED。缺测、观察精度、测试环境和未解释增长必须保留。

新增回归：BenchmarkSmoke 使用实际短时 GPU 通路和非法参数；BenchmarkAnalysis 的 flat/linear/NaN/negative/PID change/time reversal/unknown role/missing column 为具名合成夹具，不计入真机性能证据。

## 本轮执行

第一次 1800 秒运行约 8 分钟中断：Viewer Present=occluded，现有逻辑拒绝 ACK，管道终止；仅保留 partial CSV，不计作 30 分钟通过。随后只读 WTS 探针观察到会话由 LOCKED 恢复 UNLOCKED；不能将中断归因于资源增长。新增 benchmark 要求 active/unlocked（开始及采样），测试期间临时 SetThreadExecutionState 保持显示活动、退出恢复；不改变电源/安全策略或绕过手动锁屏。第二次完整运行在约 106 秒后 IPC deadline exceeded；桌面仍 UNLOCKED，未定位具体等待阶段，不将其归因于资源泄漏或判通过。补充 role/stage 超时日志、慢采样记录与 modal move/resize 事件；子进程明确继承 NUL 作为标准 I/O，业务 capability 仍为精确继承的私有管道；这属于消除隐式标准句柄歧义，尚未证明是超时根因。第三次运行约 41 秒后再次 broker consumer/viewer ACK timeout；producer frame ACK 随后到期，无 slow sample/modal 记录，NUL 改动未解决超时。新增 benchmark Viewer 单个 progress observer（最多 128 条独立文件事件），只读取主线程检查点，不调用 D3D 或授权 API；GPU copy / D2D / Present / ACK / frame-log 分阶段诊断。子进程保留文件日志，关闭可能阻塞的外部 debug/stdout sinks；尚未证明是根因。后续完整 1800 秒重跑待执行；尚未评审资源稳定性。最终环境、source commit、数值、曲线结论和测试结果待运行结束回填。G2-A：NOT REVIEWED；G2-B/C/D 未完成；G2/G3/G4 未通过。

启动前完整 Release verify 通过（8/8）；随后 Debug 与 Release 捕获基线失效，截图只有桌面背景。自有 HWND visible=true / affinity=0，input/thread desktop=Default；前台激活未接受；后续 WTSINFOEX 只读探针确认 session flags=LOCKED，screensaver=false。不能把锁屏条件当作保护通过。复现脚本记录只读桌面状态，不尝试解锁。恢复 UNLOCKED 后完整 verify PASS：Release CTest 10/10、Debug build/CTest 10/10；Rust 1.85.1/stable fmt、clippy -D warnings、策略 4/4，以及 console IPC PASS。新增 BenchmarkSmoke/BenchmarkAnalysis 两种构建均通过；这些不替代待执行的完整长测。加入进度诊断后 Release/Debug CTest 11/11、完整 verify 与 Rust/console IPC 再次通过；ProgressWatchdogProbe 的模拟停顿/正常 join 仅为诊断测试。

## 官方 API 依据

[PDH formatted arrays](https://learn.microsoft.com/en-us/windows/win32/api/pdh/nf-pdh-pdhgetformattedcounterarrayw)、[English counters](https://learn.microsoft.com/en-us/windows/win32/api/pdh/nf-pdh-pdhaddenglishcounterw)、[GetProcessTimes](https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-getprocesstimes)、[GetProcessMemoryInfo](https://learn.microsoft.com/en-us/windows/win32/api/psapi/nf-psapi-getprocessmemoryinfo)。

关联：[GPU 证据](zero-copy.md)、[TD-005](../../TECH_DEBT.md)、[RISK-005](../../RISK_REGISTER.md)、[Gate](../../ROADMAP.md)。
