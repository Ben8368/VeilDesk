# 当前状态

> **最后确认：** 2026-10-10
> **阶段：** Phase 2 / G2-D 故障矩阵进行中；G2-C Authenticode 实现已通过平台签名样本验证，但 VeilDesk 发布者 enrollment 未完成；G2-A 已限定接受关闭；G2/G3/G4 未通过。
> **当前范围：** 自有内容源、受控消费者与后续阶段基础设施；另授权 macOS 扩展基础，主环境仍为 Windows。

## 当前事实

- SecureViewer 与 CaptureTest 已实现；C++20 / Win32 / D3D11，普通 GDI 捕获和 WIC PNG 编码。
- Phase 1 本机限定验证完成；保护 API 成功不代表阻止全部捕获，更不代表保护第三方应用窗口。
- 维护者授权 G2-A→B→C→D；G2-A 限定关闭、G2-B 验证完成。G2-C pin 实现已做但缺项目证书；按要求已启动 G2-D。G2 未通过前不进 G3；Phase 4 和系统变更仍未授权。
- 独立 producer / Broker / Viewer / PNG 消费者原型已实现；限定结果见 [Broker 证据](docs/evidence/p2-broker/README.md)。
- Rust 服务入口、默认拒绝 IPC、策略内核和运维预览已实现；见 [服务证据](docs/evidence/p3-service/README.md)。
- Workspace 探针和 VM 生命周期脚本已实现；Guest 显示/输入尚未实现，见 [Workspace 证据](docs/evidence/p4-workspace/README.md)。
- GitHub public 仓库（本轮 `gh api` 实查）：[Ben8368/VeilDesk](https://github.com/Ben8368/VeilDesk)；Phase 2–4 原型与历史署名修正已推送到远端 `main`。
- 采用单一文档事实源；AI 提交的 `🚦 Audit Report` 与工具级 trailer 见 [AI_RULES](docs/AI_RULES.md)。

## 近期优先级

1. 继续 G2-D：验证在途撤销、Broker crash、device removed/reset。
2. 获取 VeilDesk 测试证书，签署自有映像并验证 pin；项目 enrollment 未测。
3. 调查授权路径吞吐（约 31 FPS），再做 P2-05；G2 未通过前不转阶段。

## 当前风险与限制

- OS / GPU / 捕获工具矩阵尚不完整；细节只在 [RISK-001](docs/RISK_REGISTER.md) 和 [Phase 1 证据](tests/VERIFICATION.md)。
- G2/G3/G4 均未通过；console Service 已约束 Broker，SCM 未验收；VMConnect 不提供 Guest 显示通路。
- 本机未提权安装服务、启用 Hyper-V、创建 VM 或修改 WDAC/VBS/HVCI；维护者确认暂时没有测试机/Guest 镜像。
- 实现妥协独立记录在 [TECH_DEBT](docs/TECH_DEBT.md)，不以实现数量关闭风险。

## 最近验证

- G2-A：完整 1800.059 秒资源/CPU 长测已完成，电功耗与 timer 回退性能未测；维护者限定接受并关闭 G2-A，相关承诺限制见 [长测记录](docs/evidence/p2-broker/resource-stability.md)。
- G2-B：Service 绑定真实 pipe 客户端身份；Broker 每帧先原子消费 Service envelope，再交付纹理。端到端批准及伪造/过期/重放/预算/降级/撤销拒绝覆盖。新增有效授权路径 50 秒性能复验约 31.1 FPS，未达 60 FPS 目标；详见[G2-C/D 与性能证据](docs/evidence/p2-broker/g2c-g2d.md)。
- G2-C/D：WinVerifyTrust + 构建 pin 的 Authenticode 校验、签名负向和平台签名样本通过；项目测试证书缺失。G2-D producer/consumer 退出及 revoke-after-2 有有限证据，Viewer 场景 occluded，Broker crash/device-loss 未测；G2 仍未通过。
- 最新增量：Windows FFI/身份读取修复；macOS C++ fixture 1/1、Rust 策略 8/8及平台拒绝 1/1、两平台 Clippy 通过，见[核心证据](docs/evidence/portable-core/README.md)。Windows 链接/运行未测。
- TD-003：Release/Debug 与共享 GPU 回归通过，Viewer readbacks/uploads=0/0；1080p 约 59FPS，见[GPU 证据](docs/evidence/p2-broker/zero-copy.md)。长测与 G2-A 限定接受见[资源记录](docs/evidence/p2-broker/resource-stability.md)。
- Phase 2–4 既有验证：C++ Release、CTest 5/5、Rust 策略 4/4、console IPC 与只读/预览检查；详见 [Broker](docs/evidence/p2-broker/README.md)、[服务](docs/evidence/p3-service/README.md)、[Workspace](docs/evidence/p4-workspace/README.md)。Phase 1 基线与历史记录仍保留，不以新构建替代真实捕获/隔离验收。
- GitHub 首轮 CI/Dependabot 与历史 Rust lint/test、headless 通过，见[CI 证据](docs/evidence/ci/README.md)。

## 按需入口

- 产品与计划：[PRODUCT](docs/PRODUCT.md)、[ROADMAP](docs/ROADMAP.md)、[DEVELOPMENT_BLUEPRINT](docs/DEVELOPMENT_BLUEPRINT.md)。
- 架构与安全：[ARCHITECTURE](docs/ARCHITECTURE.md)、[SECURITY](SECURITY.md)、[CAPTURE_CONTRACT](docs/CAPTURE_CONTRACT.md)。
- 治理与决策：[GOVERNANCE](docs/GOVERNANCE.md)、[ADR](docs/ADR/README.md)。
