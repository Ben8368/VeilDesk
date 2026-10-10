# Phase 1 验证记录

日期：2026-10-10（Asia/Shanghai）。只记录实际执行的验证，不代表其他系统或工具的结果。

## Environment

- Windows 11 Pro 25H2，build 26200.9457；运行日志中的 manifest-aware 版本为 10.0.26200。
- 一个活动显示器，3840×2160；Viewer 初始 DPI=163；manifest 检查为 Per-Monitor V2。
- NVIDIA GeForce RTX 4060，实际创建的 D3D feature level 为 11.1。
- Visual Studio 2022 Build Tools 17.14.36915.13，MSVC 19.44.35222 / v143。
- Windows SDK 10.0.26100.0；CMake 4.4.4。
- 原 VS Installer 注册状态未完成：首次配置显式指定 CMAKE_GENERATOR_INSTANCE。
  之后实际执行常规 `cmake -S . -B build` 和 Release build 成功。详见 README 的本机配置说明。

## Verified

| 范围 | 实际证据 / 观察 |
| --- | --- |
| 第一步：最小窗口 | 先独立 Release 编译和运行 Win32 版本，再加入图形；自动窗口序列退出码 0 |
| 第二步：D3D11 | Release 编译后执行渲染、resize、最大化、最小化恢复、F11；阶段运行提交 271 帧 |
| 第三步：WDA | SetWindowDisplayAffinity 成功；GetWindowDisplayAffinity=17 / 0x11；DWM composition 启用 |
| 第四步：DISPLAY_ONLY | 本机实际 CreateSwapChainForHwnd 接受 flag；渲染、resize 和窗口模式切换成功 |
| DISPLAY_ONLY 创建回退 | 显式模拟 DXGI_ERROR_UNSUPPORTED / 0x887A0004；日志记录原因，Flags=0 创建成功；持续渲染与 resize 成功 |
| CaptureTest 无保护基线 | 普通 BitBlt 成功；WIC 写 PNG 成功；标记检测成功，人工查看可见 SECURE CONTENT、帧数、时间和状态 |
| CaptureTest 默认保护 | BitBlt 返回成功；PNG 没有 Viewer 标记，人工查看为 Viewer 后方内容 |
| CaptureTest WDA 单独启用 | 禁用 DISPLAY_ONLY；PNG 没有 Viewer 标记，人工查看为后方内容 |
| CaptureTest 模拟回退 | WDA 保持启用，DISPLAY_ONLY 回退；PNG 没有 Viewer 标记，人工查看为后方内容 |
| 最终 Release 构建 | SecureViewer.exe、CaptureTest.exe 均成功链接；/W4 构建未输出警告 |
| CTest | WindowSmoke 与 CaptureSmoke 两项通过；CaptureSmoke 顺序检查四种模式 |
| 基本资源管理 | COM 使用 ComPtr；命令行分配、DC、bitmap、选中对象采用 RAII；resize 前释放 back buffer 引用；正常退出未记录资源释放错误 |

首次基线截图由于窗口遮挡而失败，标记缺失没有被当作保护通过。
后续使用 Viewer 自己的 `--capture-ready` 临时置顶选项，再取得成功无保护基线并复测保护。
保护测试的图像不能单独证明安全，结论限定于本机普通 GDI screen BitBlt 的这些测试条件。

最终自动证据在 `build/test-results/`：

- `baseline.png`、`protected.png`、`wda-only.png`、`fallback.png`。
- 各模式的 `*-viewer.log`、`*-capture.log`。
- `build/Testing/Temporary/LastTest.log` 记录 CTest 结果。

阶段日志及首次人工检查图像保存在 `tests/results/`；这些生成文件不纳入源码版本控制。
PNG 可能含底层桌面内容，请在分享或提交源码前自行检查。

## Not verified

- Windows 10 22H2、Windows 11 23H2、Windows 11 24H2，其他 GPU / 驱动、WARP 路径。
- Windows Snipping Tool、PrintScreen、OBS Display Capture、OBS Window Capture。
- Windows Graphics Capture、Desktop Duplication 独立测试客户端；当前只实现 GDI BitBlt 测试。
- 实际第二显示器 / 不同 DPI 移动；本机只有一个显示器，自动测试明确记录跳过。
- 驱动真实拒绝 DISPLAY_ONLY；目前只有实际支持路径与显式模拟失败路径。
- WDA 实际失败、不同 affinity 读回、DWM composition 关闭及 device removed/reset。
- DISPLAY_ONLY 的真实 ResizeBuffers / Present 兼容失败；代码包含回退，未制造真实驱动故障。
- 长时间 GPU / 驱动资源泄漏分析、设备丢失后自动恢复、HDR / 远程桌面 / 锁屏 / 睡眠兼容性。
- 第三方应用保护、Capture Broker、可信录制、临时授权、Rust service、Hyper-V、WDAC/VBS/HVCI。

外部捕获工具和目标 OS 矩阵的 Actual Result 表格保留在 README 中供用户填写。
