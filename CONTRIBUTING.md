# 贡献指南

先读 [AGENTS](AGENTS.md)、[CONTEXT](CONTEXT.md)；阶段与 Gate 见 [ROADMAP](docs/ROADMAP.md)。
工作目标应是一个可审查、可验证的切片。Phase 2–4 的规划不自动授权实现。

## 开发与验证

- 按 [DEVELOPMENT_GUIDE](docs/DEVELOPMENT_GUIDE.md) 使用 Windows x64 / MSVC 工具链。
- 纯治理修改：`powershell -NoProfile -File scripts/governance-docs-check.ps1`。
- 源码或构建入口修改：`powershell -NoProfile -File scripts/verify.ps1`；交互桌面限制见 [QUALITY](docs/QUALITY.md)。
- 捕获、IPC、授权、服务权限或虚拟化的长期变更先形成 ADR 和负向验证方案。
- 文档、实现、人工验收和性能证据分别报告；未运行项写明原因。

## Git 与提交

- 从 `main` 创建短分支，按范围使用 `docs/`、`fix/`、`feat/` 或 `spike/` 前缀。
- 默认不自动提交、推送或发布；每种动作在用户授权范围内执行，无需重复确认。
- 中文 Conventional Commit 及 AI 工具 trailer 以 [AI_RULES](docs/AI_RULES.md) 为准。
- 首次根提交允许一起建立既有 Phase 1 源码与治理基线；以后按独立工作包拆分。
- 不提交构建物、原始桌面 PNG、日志、`.env`、证书私钥、凭据或真实客户数据。
- 不修改用户无关改动；提交前检查 staged diff、忽略规则和验证结果。

## 评审材料

使用 [PR 模板](.github/PULL_REQUEST_TEMPLATE.md) 说明问题、阶段、变化、验证和限制。
事实回填遵守 [GOVERNANCE](docs/GOVERNANCE.md)，责任域见 [MAINTAINERS](docs/MAINTAINERS.md)。
远程仓库与当前授权状态见 [CONTEXT](CONTEXT.md)；本地 commit 不等于 push、公开分发或发布。
