# ADR 0002：受控显示 PoC 与安全表述

- 状态：已接受
- 日期：2026-10-10

## 背景

维护者要求测试常见用户态捕获路径，禁止注入、Hook、内核方案和更改其他进程 HWND；当前不开发完整产品。

## 决策

Phase 1 采用 C++20/Win32/D3D11/DXGI，自有窗口设置 WDA；DISPLAY_ONLY 直接探测、失败回退。
捕获客户端只执行正常 GDI 测试。DisplayProtection 与 Renderer 分离，错误与剩余状态可见。
官方捕获限制不作为管理员/Kernel/GPU/Hypervisor 控制者或物理拍摄的安全边界。
接受依据为维护者的 Phase 1 明确技术/安全要求；真实实现与证据已存在。

## 后果

当前可验证单个受控窗口，不能直接保护所有第三方应用原始窗口，也不能按程序临时放开 WDA。
不同系统/驱动/工具需独立验证；底层窗口可能出现在排除后的截图中。

## 替代方案

Hook/注入/修改其他进程/未公开系统接口违反已授权设计，拒绝。完整 Workspace 隔离留给后续阶段。

## 关联文档

[SECURITY](../../SECURITY.md)、[ARCHITECTURE](../ARCHITECTURE.md)、[验证记录](../../tests/VERIFICATION.md)。
