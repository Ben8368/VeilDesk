# Windows 原生开发指南

构建/运行具体命令见 [README](../README.md)；本文件维护工程约束，不复制完整命令矩阵。

## 工具与布局

- Windows 10/11 x64、VS 2022 MSVC v143、C++20、CMake 3.24+、Windows SDK 10.0.19041+。
- 产品依赖限于 Win32、D3D11、DXGI、Direct2D/DirectWrite、WIC/DWM 等 Windows SDK 能力。
- UNICODE / _UNICODE、/W4、/permissive-、UTF-8 源码；文本统一 LF。
- HWND 生命周期在 window，device/swap chain 在 graphics，WDA 在 security，普通捕获在 capture，共享诊断在 common。
- future Broker / service 不提前用空源码、空目标或假成功函数占位。

## 资源与错误

- COM 使用 Microsoft::WRL::ComPtr，不裸调用 AddRef/Release 管理生命周期。
- HANDLE、DC、bitmap、选中对象、命令行分配均明确 owner，使用 RAII；禁止散落 new/delete。
- HRESULT 必须检查；Win32 BOOL/句柄失败时立即读取 GetLastError，再 FormatMessage。
- 初始化错误给出具体位置；可回退失败说明原 HRESULT、回退是否成功及剩余保护状态。
- resize 先解绑/释放 back buffer 引用，再 ResizeBuffers，并保留有效 swap chain flags。
- DPI/窗口模式只修改自有 HWND；最大化、最小化和跨显示器保持可解释行为。

## 验证接入

治理脚本使用系统 PowerShell，无 Node/pnpm 或产品运行时依赖。
统一验证脚本先检查文档，再 configure/build，最后在交互桌面执行 CTest。
GitHub-hosted Windows CI 只验证治理和 Release 构建；窗口/捕获必须使用具名交互桌面。
新增测试验证真实行为/边界，不写仅复述实现的断言；新增检查入口需正负路径验证。

## 未来语言与部署

Rust 只在 Phase 3 授权后接入服务；Phase 2 Broker 语言的长期选择若变化需 ADR。
Hyper-V、WDAC/VBS/HVCI 不作为本地构建先决条件。新阶段安装/系统变更与源码实现分别说明授权范围。
