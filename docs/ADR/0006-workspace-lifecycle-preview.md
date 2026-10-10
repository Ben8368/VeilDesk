# ADR 0006：Workspace 只读探针与受控生命周期入口

- 状态：提议
- 日期：2026-10-10

## 背景

Phase 4 源码推进已获授权；维护者确认暂时没有隔离测试机和 Guest 镜像。G3 未通过，当前不具备部署验收条件。

## 决策

先提供只读 OS/SKU、hypervisor、CPU、feature、vmms 与 cmdlet 探针。缺权限记 Unknown，不把 hypervisor 存在时被屏蔽的 CPU 标志误判为硬件不支持。
生命周期候选脚本默认 JSON 预览，只有显式 Apply、提权、有效 ISO、空目标目录及未使用 VM 名称才进入创建。
候选 VM 为 Generation 2、Secure Boot、2 vCPU、4 GiB RAM、64 GiB 新盘、无连接虚拟交换机；不自动启动，不更改 Hyper-V feature 或 Host 安全策略。
操作绑定保存在受 ACL 保护目录的 VM GUID，拒绝仅凭名称接管其他 VM；关闭采用 graceful shutdown，失败保留磁盘。检查点不是备份或已验证恢复方案。
Guest/Host 显示输入协议仍未实现；VMConnect 窗口和普通远程桌面不构成受控 Viewer 通路，不对其他进程 HWND 设置 affinity。

## 后果

可以准备具体部署计划和未满足条件，不产生 Guest 隔离/应用兼容结论。创建/关闭/检查点路径尚无真机证据；升级、恢复、删除、剪贴板、文件、网络、音频和外设待后续实现与验收。
WDAC/VBS/HVCI 不进入本轮 Apply，保持独立操作授权与证据要求。

## 替代方案

自动启用 Hyper-V/重启日常机器会越过源码范围；把 VMConnect 当受保护工作区或把模拟结果当真实 Guest 证据会扩大能力承诺。

## 关联文档

[Workspace 证据](../evidence/p4-workspace/README.md)、[Blueprint](../DEVELOPMENT_BLUEPRINT.md)、[Security](../../SECURITY.md)、[Gate](../ROADMAP.md)。
官方依据：[Hyper-V 安装](https://learn.microsoft.com/en-us/windows-server/virtualization/hyper-v/get-started/Install-Hyper-V)、[New-VM](https://learn.microsoft.com/en-us/powershell/module/hyper-v/new-vm)、[Set-VM](https://learn.microsoft.com/en-us/powershell/module/hyper-v/set-vm)。
