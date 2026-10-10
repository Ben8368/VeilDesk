# 当前状态

> **最后确认：** 2026-10-10
> **阶段：** Phase 2 / Broker 原型推进；Phase 3–4 基础实现与探测已启动，Gate 未通过。
> **当前范围：** 自有内容源、受控消费者与后续阶段基础设施，尚非完整 Secure Workspace。

## 当前事实

- SecureViewer 与 CaptureTest 已实现；C++20 / Win32 / D3D11，普通 GDI 捕获和 WIC PNG 编码。
- Phase 1 本机限定验证完成；保护 API 成功不代表阻止全部捕获，更不代表保护第三方应用窗口。
- 维护者本轮明确授权“继续剩余的 Phase 2-4”；覆盖源码与本地验证，不重复请求开发授权。
- 独立 producer / Broker / Viewer / PNG 消费者原型已实现；限定结果见 [Broker 证据](docs/evidence/p2-broker/README.md)。
- Rust 服务入口、默认拒绝 IPC、策略内核和运维预览已实现；见 [服务证据](docs/evidence/p3-service/README.md)。
- Workspace 探针和 VM 生命周期脚本已实现；Guest 显示/输入尚未实现，见 [Workspace 证据](docs/evidence/p4-workspace/README.md)。
- GitHub 私有仓库：[Ben8368/VeilDesk](https://github.com/Ben8368/VeilDesk)；远端 `main` 仍停在 CI/Dependabot `88d1f3b`；Phase 2–4 产品代码仅在本地提交、尚未 push。
- 采用单一文档事实源；AI 提交需强制 `🚦 Audit Report` 与工具级 trailer，见 [AI_RULES](docs/AI_RULES.md)；迁移出处见 [治理迁移记录](docs/archive/reviews/2026-10-10-governance-bootstrap.md)。

## 近期优先级

1. 评审 [ADR 0004](docs/ADR/0004-private-broker-prototype.md) 的原型边界，补 G2 身份、故障、资源和捕获对照证据。
2. 收敛服务与 Broker 的主体注册和策略执行，补 G3 的持久化、账户/ACL 与隔离安装恢复验收。
3. 准备隔离测试机与许可 Guest 镜像后实现 Guest/Host 通路，再按应用逐项验收 G4。

## 当前风险与限制

- OS / GPU / 捕获工具矩阵尚不完整；细节只在 [RISK-001](docs/RISK_REGISTER.md) 和 [Phase 1 证据](tests/VERIFICATION.md)。
- G2/G3/G4 均未通过；服务尚未约束 Broker，VMConnect 不构成受控 Guest 显示通路。
- 本机未提权安装服务、启用 Hyper-V、创建 VM 或修改 WDAC/VBS/HVCI；维护者确认暂时没有测试机/Guest 镜像。
- 实现妥协独立记录在 [TECH_DEBT](docs/TECH_DEBT.md)，不以实现数量关闭风险。

## 最近验证

- 本轮统一 `scripts/verify.ps1` 通过：治理正负回归、C++ Release、CTest 5/5、Rust 策略 4/4、console IPC 与只读/预览检查；结果见 [Broker](docs/evidence/p2-broker/README.md)、[服务](docs/evidence/p3-service/README.md)、[Workspace](docs/evidence/p4-workspace/README.md)。
- 治理文档补齐强制 `🚦 Audit Report` 模板并核对入口漂移后，`scripts/governance-docs-check.ps1` 通过；未重跑完整源码 verify，不替代捕获/隔离验收。
- Phase 1 基线与历史治理记录保留原有证据，不将新构建替代真实捕获或隔离验收。
- GitHub 首轮 CI 与 Dependabot 更新任务通过；告警/安全修复已启用，Cargo 更新待服务源码推送；本地两套 Rust lint/test 与 headless 验证通过，见 [CI 证据](docs/evidence/ci/README.md)。

## 按需入口

- 产品与计划：[PRODUCT](docs/PRODUCT.md)、[ROADMAP](docs/ROADMAP.md)、[DEVELOPMENT_BLUEPRINT](docs/DEVELOPMENT_BLUEPRINT.md)。
- 架构与安全：[ARCHITECTURE](docs/ARCHITECTURE.md)、[SECURITY](SECURITY.md)、[CAPTURE_CONTRACT](docs/CAPTURE_CONTRACT.md)。
- 治理与决策：[GOVERNANCE](docs/GOVERNANCE.md)、[ADR](docs/ADR/README.md)。
