# VeilDesk — Phase 1 PoC

## Documentation

当前阶段与授权范围见 [CONTEXT.md](CONTEXT.md)，协作入口见 [AGENTS.md](AGENTS.md)。
产品边界见 [PRODUCT](docs/PRODUCT.md)，安全政策见 [SECURITY](SECURITY.md)，治理事实归属见 [GOVERNANCE](docs/GOVERNANCE.md)。
Phase 2–4 的成果与 Gate 统一记录在 [ROADMAP](docs/ROADMAP.md)，具体工作包记录在 [DEVELOPMENT_BLUEPRINT](docs/DEVELOPMENT_BLUEPRINT.md)。
架构、捕获契约、质量、风险/技术债与决策分别在 [ARCHITECTURE](docs/ARCHITECTURE.md)、[CAPTURE_CONTRACT](docs/CAPTURE_CONTRACT.md)、[QUALITY](docs/QUALITY.md)、[RISK_REGISTER](docs/RISK_REGISTER.md)、[TECH_DEBT](docs/TECH_DEBT.md)、[ADR](docs/ADR/README.md)。

## Overview

这是一个原生 Windows 显示捕获限制实验，包含 `SecureViewer.exe` 和 `CaptureTest.exe`。
Viewer 使用 D3D11 / DXGI 输出动态背景，使用 Windows SDK 的 Direct2D / DirectWrite 在同一 GPU 表面绘制
`SECURE CONTENT`、帧计数、系统本地时间、保护状态及彩色测试标记。

该 PoC 用于测试 Windows 官方显示捕获限制能力，目标是阻止或限制常见用户态捕获路径。
**当前仅保护 Viewer 自己的输出窗口，不保护整个 Windows 桌面，也不保护 Chrome、Edge、微信、QQ、飞书或 Office 的原始窗口。**
把其他窗口放在 Viewer 后面不会保护它们；WDA 排除 Viewer 时，截图可能显示其后面的桌面内容。
本阶段没有第三方应用接入、可信录制、临时进程授权或 Capture Broker。

## Threat Model

测试目标是普通截图工具、Windows 标准捕获路径及普通用户态 screen recorder 对 Viewer 输出的捕获行为。
是否被排除、变黑或仍然可见，必须在具体 OS / GPU / 驱动 / 捕获工具组合上验证。

Windows 提供的捕获限制与真正的安全边界不同。WDA 和 DISPLAY_ONLY 不能作为 DRM 或对抗完全控制机器的攻击者的保证。
明确不防御：

- 管理员完全控制、进程内存读取或篡改。
- Kernel malware、恶意显示驱动、GPU / driver level attacker。
- Hypervisor attacker。
- 手机或其他物理摄像机拍摄屏幕。

不使用 DLL 注入、API Hook、SSDT Hook、kernel patch、未公开 DWM Hook，也不修改其他进程的窗口或 affinity。
唯一的 `SetWindowDisplayAffinity` 调用位于 DisplayProtection，先确认目标是本进程的顶级 HWND。
设计原则是受控输出通路。`CaptureTest` 只执行普通 BitBlt 和 PNG 编码，不提供规避保护的路径。

## Build

需要 Windows 10/11 x64、Visual Studio 2022 或 Build Tools 2022、MSVC v143、Windows SDK 10.0.19041+ 和 CMake 3.24+。
VS Installer 中安装“使用 C++ 的桌面开发”，包含 x64 编译工具和 SDK。
无需 Qt、Electron、.NET 应用框架、Rust、内核驱动或第三方 GUI 框架。

在 x64 Native Tools Command Prompt for VS 2022 或已配置 CMake 的终端运行：

```powershell
cd C:\VeilDesk
cmake -S . -B build
cmake --build build --config Release
```

首次配置若需要明确选择 VS 2022 / x64：

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

生成器必须为 x64。切换已缓存的生成器时使用另一个构建目录。
默认 VS 多配置生成器的产物在 `build\Release`；Ninja 等单配置生成器需要 `-DCMAKE_BUILD_TYPE=Release`，产物目录可能不同。
测试脚本的 `-BuildDir` 参数可指定实际产物目录。

本工作机原有 VS Installer 状态为未完成，但 MSVC 文件已存在；已补装官方 CMake 和 Windows SDK。
此类环境若自动发现 VS 失败，可修复 VS 安装，或针对已有工具链显式指定实例（下面仅对应本机）：

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 "-DCMAKE_GENERATOR_INSTANCE=C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools,version=17.14.36915.13"
cmake --build build --config Release
```

CMake 配置成功后，常规的两条命令即可重配和重建该目录。CMake 新安装后可能需要重新打开终端刷新 PATH。
两个程序均嵌入 asInvoker、Windows 10 compatibility 和 Per-Monitor V2 manifest；不会请求管理员权限。

## Run

先启动 Viewer，然后在另一个终端启动测试程序：

```powershell
.\build\Release\SecureViewer.exe
.\build\Release\CaptureTest.exe
```

CaptureTest 列出当前桌面上类名为 `VeilDesk.SecureViewer` 的窗口，输入编号选择。
让 Viewer 保持可见、未最小化、无遮挡且客户区完整位于显示器内。
测试程序截取所选窗口客户区对应的**桌面像素矩形**，不调用 PrintWindow，也不绕过保护。
`capture.png`、`SecureViewer.log`、`CaptureTest.log` 默认写入启动时的工作目录。
日志同时输出到 OutputDebugString；CaptureTest 有终端输出，GUI Viewer 的日志文件是主要诊断入口。

F11 切换本进程窗口的无边框全屏；Esc 从全屏返回。保持 DXGI windowed swap chain，不使用独占全屏或 Alt+Enter。
支持 WM_SIZE、最大化、最小化恢复、WM_DPICHANGED 建议矩形及多显示器移动的基本处理。

可按 PID 选择窗口及指定输出路径：

```powershell
.\build\Release\CaptureTest.exe --list
.\build\Release\CaptureTest.exe --pid 12345 --output capture.png
```

### Diagnostic options

| Viewer 选项 | 行为 |
| --- | --- |
| `--smoke-test` | 5 秒自动执行 resize、最大化、最小化恢复、全屏切换，然后退出；若有第二显示器，也移动到该显示器并返回 |
| `--run-seconds 30` | 持续渲染指定秒数（1..3600），正常退出，不自动 resize |
| `--capture-ready` | 测试专用：将自己的 Viewer 置顶并定位到 1000×700；退出后消失 |
| `--no-protection` | 明确关闭 WDA 和 DISPLAY_ONLY，显示 UNPROTECTED，用于无保护基线 |
| `--disable-display-only` | 仍请求 WDA，禁用 DISPLAY_ONLY，测试 WDA 单独效果 |
| `--simulate-display-only-failure` | 注入 DXGI_ERROR_UNSUPPORTED，测试自动回退；不表示实际驱动不支持 |
| `--help` | 记录选项后退出 |

常规启动默认请求 WDA 和 DISPLAY_ONLY。无保护基线建议单独运行：

```powershell
.\build\Release\SecureViewer.exe --no-protection --capture-ready
```

CaptureTest 的 `--expect-visible` / `--expect-excluded` 是测试用的图像标记断言。
退出码 0 表示普通捕获与编码完成、选定的标记断言未失败；1 表示参数/API/窗口状态/编码失败；2 表示断言不匹配。
**退出码 0 不等于安全认证**。每次捕获都会写日志区分 API 成功与图像内容。

### Interpreting captures

- `Capture succeeded: Viewer diagnostic pattern detected`：图像在预期位置出现 Viewer 标记，表明此路径取得了测试画面的至少一部分。
- `Capture blocked / blank / excluded (candidate...)`：标记未出现，可能是排除、黑屏、遮挡或其他无效条件；单凭缺失不能证明保护。
- BitBlt 返回成功但得到底层桌面，是可能的正常排除结果；它不一定返回错误。
- 工具拒绝隐藏、最小化、空客户区、超大或部分超出虚拟屏幕的窗口；这些失败标为不确定，不能计作保护通过。

必须先在相同位置取得无遮挡的无保护基线，再开启保护重复测试，并人工检查 PNG。
彩色标记只是诊断启发式；它不能检测所有部分泄漏、其他画面或不同工具的捕获行为。
截屏可能包含 Viewer 后面的应用内容，请自行决定是否保留/分享 PNG 和日志。

## Display protection and compatibility

`SetWindowDisplayAffinity(hwnd, WDA_EXCLUDEFROMCAPTURE)` 在 Viewer 显示前调用；检查返回值、立即读取 GetLastError、用 FormatMessage 输出错误，随后读取 affinity 并确认 DWM composition。
成功读取 `0x11` 只确认 API 状态。旧版本 Windows 可能按 WDA_MONITOR 处理该值；不会用 OS build hardcode 决定能力。
启动日志中的版本来自兼容 manifest 下的 GetVersionEx，仅供诊断（Windows 11 也使用 10.0 版本号）。

DISPLAY_ONLY 使用 `IDXGIFactory2::CreateSwapChainForHwnd`、BGRA、两个 buffer、flip sequential、windowed、当前进程拥有的 HWND。
不使用 GDI-compatible swap chain，也不限制到某一输出显示器；MakeWindowAssociation 禁用 Alt+Enter，F11 仅改变窗口样式。
按实际创建结果检测支持情况。失败时记录 HRESULT 和可读消息，再以相同参数、Flags=0 创建 swap chain，Viewer 仍继续运行。
DISPLAY_ONLY 下的 resize / Present 若发生兼容错误，也尝试通过 ResizeBuffers 去掉 flag；WDA 状态保持。
Resize 前释放 D2D target、render target view 并清除绑定，避免持有 back buffer 引用。

Device removed/reset 明确记录原因并正常以错误退出，Phase 1 没有设备重建；这类错误不被掩饰为兼容回退。
硬件 D3D11 创建失败会记录 HRESULT 并尝试 WARP；WARP 不能代表硬件路径的兼容性结果。
初始化、Present 或 resize 的致命错误会停止渲染并正常释放资源。
WDA 失败会明确记录原因、窗口显示未确认保护状态，便于诊断；DISPLAY_ONLY 创建成功也不代表 WDA 成功。

目标兼容矩阵包含 Windows 10 22H2、Windows 11 23H2、24H2；必须分别实测。
本机已执行的编译/自动测试与限制记录见 `tests/VERIFICATION.md`，不把该机器结果推广到上述系统。

微软官方资料：

- [SetWindowDisplayAffinity](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-setwindowdisplayaffinity)
- [DXGI_SWAP_CHAIN_FLAG / DISPLAY_ONLY](https://learn.microsoft.com/en-us/windows/win32/api/dxgi/ne-dxgi-dxgi_swap_chain_flag)
- [CreateSwapChainForHwnd](https://learn.microsoft.com/en-us/windows/win32/api/dxgi1_2/nf-dxgi1_2-idxgifactory2-createswapchainforhwnd)
- [ResizeBuffers](https://learn.microsoft.com/en-us/windows/win32/api/dxgi/nf-dxgi-idxgiswapchain-resizebuffers)
- [BitBlt](https://learn.microsoft.com/en-us/windows/win32/api/wingdi/nf-wingdi-bitblt)
- [WIC encoding](https://learn.microsoft.com/en-us/windows/win32/wic/-wic-creating-encoder)

## Manual Testing

每项先运行无遮挡无保护基线，确认可以捕获动态背景、文字、时间和标记；再退出基线 Viewer，以默认保护启动，使用同一捕获方式复测。
避免同时打开两个 Viewer。分别测试窗口/全屏、resize、最大化、最小化恢复、跨显示器、不同 DPI。
OBS 请记录版本、实际 capture method、窗口选择与设置；OBS 的不同后端不能互相替代。

| Test | Expected Result（待具体环境验证） | Actual Result（用户填写） |
| --- | --- | --- |
| Windows Snipping Tool | 保护 Viewer 内容被排除或变黑；背景可能可见 | |
| PrintScreen / Clipboard | 不包含受保护 Viewer 的测试画面 | |
| OBS Display Capture | 标准桌面捕获路径排除或遮蔽 Viewer；按后端实测 | |
| OBS Window Capture | 按所选捕获后端实测；可能排除、黑屏或存在差异 | |
| CaptureTest（GDI BitBlt） | API 可成功，但 PNG 中不出现 Viewer 内容；先确认无保护基线 | |
| 第二显示器及不同 DPI 移动 | 保持渲染，resize 与 DPI 正常，重新验证捕获限制 | |

| System | GPU / driver | Capture tool/version/method | WDA log | DISPLAY_ONLY log | Actual Result / evidence path |
| --- | --- | --- | --- | --- | --- |
| Windows 10 22H2 | | | | | |
| Windows 11 23H2 | | | | | |
| Windows 11 24H2 | | | | | |

## Automated Testing

统一验证入口（文档检查 → 检查器回归 → configure → Release build → CTest）：

```powershell
powershell -NoProfile -File scripts/verify.ps1
```

纯文档运行 `powershell -NoProfile -File scripts/governance-docs-check.ps1`。
无交互桌面的 CI 可给统一入口传 `-SkipInteractive`；跳过窗口/捕获测试，不构成捕获能力证据。

需要有可用的交互式 Windows 桌面。测试会短暂显示/resize Viewer，并将捕获用 Viewer 临时置顶。
运行期间避免移动、遮挡窗口、切换显示器或锁屏。

```powershell
ctest --test-dir build -C Release --output-on-failure
```

或直接执行：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tests\window-smoke.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File tests\capture-smoke.ps1
```

WindowSmoke 检查自动窗口序列、持续提交帧及错误日志。
CaptureSmoke 依次执行无保护基线、默认保护、WDA 单独启用、模拟 DISPLAY_ONLY 失败回退，保存 PNG 和两端日志。
基线标记必须出现，保护测试标记必须缺失；仍需人工检查图像。
CTest 产物位于 `build\test-results`；直接运行脚本默认写入 `tests\results`。
实际 DISPLAY_ONLY 不支持、WDA 失败、device removal、真实多显示器和所有外部工具，均需要相应环境独立验证。

## Project Layout

```text
CMakeLists.txt
README.md
src/
  main.cpp
  app.manifest
  app.rc
  window/SecureWindow.h, .cpp
  graphics/D3D11Renderer.h, .cpp
  security/DisplayProtection.h, .cpp
  common/Logger.h, .cpp
  common/SystemInfo.h, .cpp
  common/CapturePattern.h
  capture/main.cpp
  capture/CaptureEngine.h, .cpp
tests/
  window-smoke.ps1
  capture-smoke.ps1
  VERIFICATION.md
```

SecureWindow 管理 HWND、消息循环、窗口模式、resize 和 DPI。
D3D11Renderer 管理 device/context、swap chain、GPU 测试源、render target、文本和 Present。
DisplayProtection 只设置和检查 Windows display affinity，不渲染。
CaptureEngine 管理普通 GDI 捕获及 WIC PNG 编码，COM 用 ComPtr，DC / bitmap / 选中对象使用 RAII。

## Next Phases

后续顺序、Gate 与工作包分别以 [ROADMAP](docs/ROADMAP.md)、[DEVELOPMENT_BLUEPRINT](docs/DEVELOPMENT_BLUEPRINT.md) 为准。
记录规划不启动实现；当前能力状态见 Context 与具名验证记录。
