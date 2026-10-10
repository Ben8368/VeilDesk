# Phase 4 Workspace 探针与未运行记录

日期：2026-10-10（Asia/Shanghai）；P4-01 只读探针、P4-02 生命周期源码候选；G4：未通过。
源码版本：基于 `b699715` 的本轮未提交工作树；维护者明确答复暂时没有隔离测试机/已许可 Guest 镜像。

## 实际命令与结果

```powershell
powershell -NoProfile -File scripts/workspace-probe.ps1
powershell -NoProfile -File scripts/workspace.ps1 -Action Plan
powershell -NoProfile -File tests/workspace-smoke.ps1
```

| 探针 / 检查 | 实际结果 |
| --- | --- |
| OS | Windows 11 Pro、10.0.26200、SKU 48 |
| HypervisorPresent | True；不等于 Hyper-V 管理功能已可用 |
| CPU 三个虚拟化标志 | CIM 返回 False；已有 hypervisor 可屏蔽这些标志，不能推断硬件不支持 |
| Microsoft-Hyper-V-All | Unknown：当前非管理员；实际一次查询报需提升，探针随后明确跳过提权查询 |
| vmms | NotFound |
| Get-VM | 管理 cmdlet 不可用 |
| ReadyForVmOperations | False，缺少可确认管理前提；不启用 feature、不重启 |
| JSON 预览 | Gen2、2 vCPU、4 GiB、64 GiB、无连接 switch、保留数据、未实现 GuestTransport |
| 安全负向检查 | 名称/路径越界与盘根拒绝；Create 无 Apply 预览没有产生目录；服务 Install 无 Apply 未部署 |

生命周期源码提供 Create/Start/Stop/Checkpoint，按受 ACL 保护的 manifest 中 VM GUID 操作；不接管已有目录/同名 VM，不提供强制关机或删除。
New-VM/Set-VM 的参数依据官方文档，见 [ADR 0006](../../ADR/0006-workspace-lifecycle-preview.md)；未运行真实 VM 命令。

## 未实现 / 未运行

G3 未通过；本机管理工具不可用且无测试机/Guest 镜像。没有创建、启动、停止或检查点的真机证据，恢复、升级和数据删除也未实现。
Guest/Host 受控显示、输入、授权录制、文件、剪贴板、网络、音频和外设协议尚未实现；VMConnect 不构成受保护显示出口。
Chrome/Edge、微信、QQ、飞书、Office 全部未验证；不能从 Host PoC 推导 Guest 应用兼容。
WDAC/VBS/HVCI 未实施、未开启、未实测；各自仍需独立候选方案与操作授权。
以上属于阻断 G4 的实际缺口，不用规划、脚本预览或构建替代 Guest 证据。

关联：[Risk RISK-007/008](../../RISK_REGISTER.md)、[TD-007](../../TECH_DEBT.md)、[Roadmap G4](../../ROADMAP.md)、[Blueprint](../../DEVELOPMENT_BLUEPRINT.md)。
