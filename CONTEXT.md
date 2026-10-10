# 当前状态

> **最后确认：** 2026-10-10
> **阶段：** Phase 2 / G2-A 长时资源稳定性；G2/G3/G4 未通过。
> **当前范围：** 自有内容源、受控消费者与后续阶段基础设施，尚非完整 Secure Workspace。

## 当前事实

- SecureViewer 与 CaptureTest 已实现；C++20 / Win32 / D3D11，普通 GDI 捕获和 WIC PNG 编码。
- Phase 1 本机限定验证完成；保护 API 成功不代表阻止全部捕获，更不代表保护第三方应用窗口。
- 维护者本轮授权按 G2-A→B→C→D 收敛安全边界；G2 PASS 后才进入 G3，禁止继续 Phase 4，系统变更仍独立授权。
- 独立 producer / Broker / Viewer / PNG 消费者原型已实现；限定结果见 [Broker 证据](docs/evidence/p2-broker/README.md)。
- Rust 服务入口、默认拒绝 IPC、策略内核和运维预览已实现；见 [服务证据](docs/evidence/p3-service/README.md)。
- Workspace 探针和 VM 生命周期脚本已实现；Guest 显示/输入尚未实现，见 [Workspace 证据](docs/evidence/p4-workspace/README.md)。
- GitHub public 仓库（本轮 `gh api` 实查）：[Ben8368/VeilDesk](https://github.com/Ben8368/VeilDesk)；Phase 2–4 原型与历史署名修正已推送到远端 `main`。
- 采用单一文档事实源；AI 提交需强制 `🚦 Audit Report` 与工具级 trailer，见 [AI_RULES](docs/AI_RULES.md)；迁移出处见 [治理迁移记录](docs/archive/reviews/2026-10-10-governance-bootstrap.md)。

## 近期优先级

1. G2-A：真实 1080p/60 FPS target/1800 秒资源测试进行中；[长测方法与记录](docs/evidence/p2-broker/resource-stability.md)，未引入 pool、未宣称资源 Gate 通过。
2. 按 [Blueprint G2 剩余收敛](docs/DEVELOPMENT_BLUEPRINT.md) G2-A→D：长时资源、统一 Authorization Envelope、Authenticode enrollment、在途撤销/故障矩阵；[自有工具身份](docs/evidence/p2-broker/enrollment.md)已增量实现。
3. G2/G3 验收后再推进 Guest/Host 通路；准备隔离测试机和许可 Guest 镜像。

## 当前风险与限制

- OS / GPU / 捕获工具矩阵尚不完整；细节只在 [RISK-001](docs/RISK_REGISTER.md) 和 [Phase 1 证据](tests/VERIFICATION.md)。
- G2/G3/G4 均未通过；服务尚未约束 Broker，VMConnect 不构成受控 Guest 显示通路。
- 本机未提权安装服务、启用 Hyper-V、创建 VM 或修改 WDAC/VBS/HVCI；维护者确认暂时没有测试机/Guest 镜像。
- 实现妥协独立记录在 [TECH_DEBT](docs/TECH_DEBT.md)，不以实现数量关闭风险。

## 最近验证

- TD-003：Release/Debug 构建与共享 GPU 回归通过；Viewer readbacks/uploads=0/0、完整 descriptor/真实跨 adapter 拒绝有证据；1080p/60 秒由约 18FPS 提升到约 59FPS（3556–3566/3600），长时资源 Gate 未通过，见 [GPU 证据](docs/evidence/p2-broker/zero-copy.md)。

- Phase 2–4 既有验证：C++ Release、CTest 5/5、Rust 策略 4/4、console IPC 与只读/预览检查；结果见 [Broker](docs/evidence/p2-broker/README.md)、[服务](docs/evidence/p3-service/README.md)、[Workspace](docs/evidence/p4-workspace/README.md)。
- Phase 1 基线与历史治理记录保留原有证据，不将新构建替代真实捕获或隔离验收。
- GitHub 首轮 CI 与 Dependabot 更新任务通过；告警/安全修复已启用，Cargo 更新待服务源码推送；本地两套 Rust lint/test 与 headless 验证通过，见 [CI 证据](docs/evidence/ci/README.md)。

## 按需入口

- 产品与计划：[PRODUCT](docs/PRODUCT.md)、[ROADMAP](docs/ROADMAP.md)、[DEVELOPMENT_BLUEPRINT](docs/DEVELOPMENT_BLUEPRINT.md)。
- 架构与安全：[ARCHITECTURE](docs/ARCHITECTURE.md)、[SECURITY](SECURITY.md)、[CAPTURE_CONTRACT](docs/CAPTURE_CONTRACT.md)。
- 治理与决策：[GOVERNANCE](docs/GOVERNANCE.md)、[ADR](docs/ADR/README.md)。
