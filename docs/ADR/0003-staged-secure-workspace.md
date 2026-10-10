# ADR 0003：分阶段 Workspace 演进

- 状态：已接受
- 日期：2026-10-10

## 背景

维护者要求保存后续 Phase 2–4 规划，同时保留 Phase 1 的停止边界。

## 决策

顺序为 Capture Broker + Shared D3D11 Texture → Rust SecureService → Hyper-V Secure Workspace。
每阶段有独立 Gate、工作包和启动授权；规划不启动源码实现、服务安装、虚拟机或系统策略变更。
WDAC/VBS/HVCI 与第三方应用接入按独立候选工作包和真实环境验证。
接受的是阶段方向与治理，不是尚未验证的共享资源 API、身份算法、IPC、服务账户或 Guest 显示方案。

## 后果

可逐步形成可验证切片；完整桌面产品与各应用兼容仍需后续证据，不能从 PoC 推导已有能力。
未来契约、依赖和特权的选择需要新 ADR。

## 替代方案

一次引入 Broker、Rust、服务与 Hyper-V 会混淆错误和信任边界，违背维护者的分阶段要求。

## 关联文档

[ROADMAP](../ROADMAP.md)、[DEVELOPMENT_BLUEPRINT](../DEVELOPMENT_BLUEPRINT.md)、[CAPTURE_CONTRACT](../CAPTURE_CONTRACT.md)。
