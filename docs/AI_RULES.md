# AI 执行与审查规则

入口是 [AGENTS](../AGENTS.md)，当前事实是 [CONTEXT](../CONTEXT.md)，文档事实归属见 [GOVERNANCE](GOVERNANCE.md)。

## 执行顺序

读取当前事实 / 命中经验 → 确认范围 → 实现或文档修改 → 🚦 Audit Report → 客观验证 → 更新权威事实 → 汇报。
用户明确要求工作时完成其范围；只读评审保持只读。阶段规划不自动触发实现，但后续明确授权优先。
API 不确定时先查实际 SDK 和 Microsoft Learn；记录依据与实测差异，不复制来源不明代码。

## 红绿灯审查

产品源码发生变化后、运行完整验证前，必须输出：

```markdown
## 🚦 Audit Report

**总体评价：** [ 🔴 阻断 / 🟡 可通行 / 🟢 通过 ]

**1. 🔴 阻断项：**
- 无 / 具体问题

**2. 🟡 跟进项：**
- 无 / 具体问题及权威登记位置

**3. 验证计划：**
- 命令、真实路径与未覆盖项
```

仅治理/文档改动也输出同一模板；验证计划至少包含 `scripts/governance-docs-check.ps1`，不伪造源码或系统验证。

审查至少覆盖（思考层，不逐条机械罗列）：

- **真实性**：API、HRESULT、捕获结果、OS/GPU 兼容与性能结论是否有证据。
- **范围**：是否符合 Product、Roadmap 和本轮用户授权；未授权阶段不得当作已实现。
- **分层**：UI/测试是否越权改其他进程 HWND、做注入/Hook，或让 DisplayProtection 渲染/改目标 affinity。
- **资源安全**：句柄、COM、共享纹理、IPC 缓冲区是否有上限、超时、终态与清理。
- **异常路径**：失败 API、错误码、回退与降级是否可读且不假成功。
- **兼容性**：以实际 API 结果为准，不以 OS 版本/进程名/路径单独证明能力。
- **供应链**：依赖、工具链、许可证与私有产物边界是否可审查。
- **可测试性**：关键路径能否自动化或留下具名证据；交互桌面未运行时不得标为能力通过。

分级：

- 🔴：越过授权/威胁边界、隐私泄漏、假成功、身份或资源访问失控、契约破坏或当前阶段越权；修复后再完成。
- 🟡：不阻断当前目标，但存在已登记的兼容性、性能、维护或主观验收缺口；写入 `RISK_REGISTER` 或 `TECH_DEBT` 的唯一匹配位置。
- 🟢：授权范围完成、相应客观证据通过，没有未登记缺口。

🔴 不得合并或宣称完成。既有检查通过后，仅在新改动、失败或未解决问题需要时扩大/重复测试。

## 实现约束

- Win32 Unicode、明确类型、ComPtr/RAII；不让 DisplayProtection 渲染或让捕获测试改目标 affinity。
- 错误路径记录失败 API、HRESULT 或立即取得的 GetLastError，不能“成功但无数据”地占位。
- 兼容检测优先实际 API 结果；OS 版本、进程名、路径或 PID 不能单独证明能力/可信身份。
- 未来跨进程操作必须有身份、协议版本、大小上限、超时、终态与资源清理。
- 单文件超过 450 行先评估职责拆分；不会为行数预算把同一职责机械分裂。
- 脚本不可清理用户无关数据；测试输出留在受忽略目录，保留隐私风险说明。

## 验证入口

纯文档：`powershell -NoProfile -File scripts/governance-docs-check.ps1`。
源码、构建或统一验证入口：`powershell -NoProfile -File scripts/verify.ps1`。
没有交互式桌面时可用 `-SkipInteractive`，必须报告窗口/捕获测试未运行，不能视为能力通过。
阶段和具体场景的证据要求见 [QUALITY](QUALITY.md)。

## Git 与署名

提交、push、发布是不同动作，分别以用户授权范围为准；不因已授权 commit 直接 push。
保留现有维护者 `user.name` / `user.email`，不改全局身份，不清理无关改动，不使用破坏性 Git 操作。
提交前检查 staged diff 与敏感产物；首个 commit 可同时记录已有 Phase 1 和治理基线。

标题/正文用中文，Conventional Commit 类型保留英文；AI 参与并执行提交时使用工具级 trailer：

| 工具 | Trailer |
| --- | --- |
| Codex | `Co-authored-by: Codex <codex@openai.com>` |
| Claude Code | `Co-authored-by: Claude Code <noreply@anthropic.com>` |
| Cursor | `Co-authored-by: Cursor <cursoragent@cursor.com>` |

使用多个 `-m` 或 UTF-8 message 文件，不在 shell 字符串里用字面 `\n` 伪造换行。
提交后执行 `git show -s --format=%B HEAD | git interpret-trailers --parse` 确认 trailer，并检查仓库状态。
