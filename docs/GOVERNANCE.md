# 治理规则

本体系参考 uNAS 的单一事实源与证据治理，按 Windows 原生开发重新编写；出处见 [迁移记录](archive/reviews/2026-10-10-governance-bootstrap.md)。

## 事实归属

| 事实 | 权威位置 |
| --- | --- |
| AI 入口、硬边界、读取路由 | [AGENTS](../AGENTS.md)；工具摘要只链接它 |
| 当前阶段、范围、前三优先级、最近验证摘要 | [CONTEXT](../CONTEXT.md) |
| 可复发经验索引与详情 | [LESSONS](../LESSONS.md)、`docs/lessons/` |
| 用户、产品目标、长期范围、非目标 | [PRODUCT](PRODUCT.md) |
| 长期分层与依赖方向 | [ARCHITECTURE](ARCHITECTURE.md) |
| 捕获结果与未来授权协议的语义 | [CAPTURE_CONTRACT](CAPTURE_CONTRACT.md) |
| 阶段成果、进入/退出门禁 | [ROADMAP](ROADMAP.md) |
| 执行工作包、依赖、验收项 | [DEVELOPMENT_BLUEPRINT](DEVELOPMENT_BLUEPRINT.md) |
| 工程接入、代码规范、构建约束 | [DEVELOPMENT_GUIDE](DEVELOPMENT_GUIDE.md)；具体运行命令在 [README](../README.md) |
| 质量门禁、证据标准、完成定义 | [QUALITY](QUALITY.md) |
| 能力记录格式与证据索引 | [PRODUCT_CAPABILITY_MATRIX](PRODUCT_CAPABILITY_MATRIX.md) |
| 外部未知、未验证假设与其 Gate | [RISK_REGISTER](RISK_REGISTER.md) |
| 已产生的实现妥协与偿还条件 | [TECH_DEBT](TECH_DEBT.md) |
| 长期决定及理由 | [ADR](ADR/README.md) |
| 威胁模型与安全政策 | [SECURITY](../SECURITY.md) |
| AI 审查、Git message / trailer | [AI_RULES](AI_RULES.md) |
| 原始 Phase 1 验证结论与覆盖范围 | [tests/VERIFICATION](../tests/VERIFICATION.md) |
| 历史评审与关闭条目 | [archive](archive/README.md) 与 Git 历史 |

摘要只链接证据，不复制其详细矩阵。证据记录与 Context 有矛盾时先核实带环境/步骤的记录，在同一次变更修正摘要。
README 是构建运行入口，避免再维护完整的未来工作包或当前阶段副本。

## 状态语言

- 设计中：有候选方案，尚未实现。
- 计划：进入路线图，尚未开始执行。
- 已实现未验证：存在实现，但未满足目标环境验收。
- 已验证：在具名环境、路径和证据范围内通过。
- 不支持：明确拒绝或超出约定范围。

Gate 的规则归 Roadmap，当前是否通过归 Context 并链接证据。
文档记录、故障模拟、构建通过或 CI 通过不能替代真实捕获/隔离能力。

## 预算与检查

预算唯一机器来源是 [governance-budgets.json](../scripts/governance-budgets.json)。
频繁读取的 AGENTS、Context、Lessons、Claude/Cursor 摘要保持短小；专题超过预算先去重、拆分或归档，不单纯加预算。
检查脚本无需 Node、Rust 或 GUI 依赖，也无需先初始化 Git。

```powershell
powershell -NoProfile -File scripts/governance-docs-check.ps1
```

检查强制文件、预算、UTF-8/LF、本地文件链接、三项优先级、Phase 2–4 章节、Gate、ADR 元数据和工具摘要路由。
只扫描源码文档根、docs、指定测试证据和 PR 模板；构建目录、临时工具与桌面产物不属于治理输入。
检查不能证明文本事实、安全边界、真实系统行为或远程 URL 有效，需要人工核对。

## 更新与归档

1. 先判断权威位置；其他文档使用链接和稳定摘要。
2. 只有阶段、前三优先级、授权范围、阻断或验证摘要变化才更新 Context。
3. 产品范围改 Product，Gate 改 Roadmap，执行拆分改 Blueprint；规划不授权实施。
4. 风险与技术债分开，引用关联 ID，不复制详情。
5. 权限、身份、IPC、数据流、技术栈或虚拟化长期决定需要 ADR；提议不当作接受或实现。
6. 历史评审、关闭风险与偿债记录按日期归档；当前入口只保留跳转。
7. 新 lesson 只记录会影响下次行动的结论。
8. 提交前检查链接、状态、预算和保密边界；执行顺序与 trailer 遵守 AI_RULES。
