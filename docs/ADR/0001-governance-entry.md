# ADR 0001：单一事实源与证据治理

- 状态：已接受
- 日期：2026-10-10

## 背景

维护者要求参考 uNAS 移植治理文档体系并创建首次 Git 基线；多工具、多阶段协作需要防止事实、规则与计划漂移。

## 决策

AGENTS 是唯一 AI 入口；Context / Lessons 保持短小，按任务读取专题。
产品、架构、捕获契约、阶段、工作包、风险、技术债和真实证据分开维护。
治理检查改用无产品依赖的 PowerShell，适配 Windows/CMake；原始桌面产物不进入 Git。
接受依据为本轮明确用户指令；迁移出处与差异见 [记录](../archive/reviews/2026-10-10-governance-bootstrap.md)。

## 后果

减少重复状态和假成功，增加链接/预算维护成本；自动检查不能代替真实系统与人工审查。

## 替代方案

多个工具各自维护规则、所有材料塞进 README、逐字复制 MV3/pnpm 文档均不适合本项目。

## 关联文档

[AGENTS](../../AGENTS.md)、[GOVERNANCE](../GOVERNANCE.md)、[AI_RULES](../AI_RULES.md)。
