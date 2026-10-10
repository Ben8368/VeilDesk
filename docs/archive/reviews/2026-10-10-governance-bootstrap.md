# 2026-10-10 治理迁移与 Git 基线

本记录保存本次迁移出处和验收范围；当前阶段在 [CONTEXT](../../../CONTEXT.md)，后续计划在 [ROADMAP](../../ROADMAP.md)。

## 参考来源

- 项目：[Ben8368/uNAS](https://github.com/Ben8368/uNAS)。
- 固定参考提交：[ad38670bbd24678b4064186972f2784dd1853d18](https://github.com/Ben8368/uNAS/tree/ad38670bbd24678b4064186972f2784dd1853d18)。
- 2026-10-10 通过远程 main SHA 与本地 `C:\uNAS` HEAD 一致性确认；只读取参考仓库，不修改它。
- 参考 AGENTS/Context/Lessons、Governance/AI Rules、Roadmap/Blueprint、Quality、ADR、风险/技术债、贡献/维护和治理检查结构。
- 本项目文档与检查脚本针对 Windows 原生 PoC 重新编写，不复制上游产品代码、浏览器技术栈或阶段事实。

## 移植映射与差异

| uNAS 机制 | VeilDesk 适配 |
| --- | --- |
| 单一 AI 入口与工具摘要 | AGENTS + Claude/Cursor 摘要；按实际任务路由 |
| Context / Lessons 强制短入口 | 当前事实与三项优先级；治理/Windows/捕获经验详情 |
| Roadmap / Blueprint / Gate | Phase 2 Broker、Phase 3 Rust service、Phase 4 Hyper-V；独立启动授权 |
| 产品 / 架构 / App、Engine 契约 | Product / Architecture / Capture Contract；未来协议仍设计中 |
| Risk / Tech Debt / ADR / archive | 区分外部未知、已有妥协、长期决策与历史 |
| Node 文档检查 / pnpm verify | 无产品依赖 PowerShell 检查，统一 CMake/Release/CTest 入口 |
| 扩展 CI 与 E2E | Windows build CI；明确跳过真实交互桌面捕获 |
| Git 署名与保密 | 维护者 author + 工具 trailer；本地产物按目录排除 |

不移植 MV3/React/WXT、浏览器权限、工具 App、密码库、媒体引擎和前端视觉规则。
不在治理任务里实现 Phase 2–4、安装服务或改变 Windows 策略。

## 验收与执行记录

- `scripts/governance-docs-check.ps1` 通过：40 个文本文件、3 份 ADR 的编码、预算、链接和结构有效。
- `tests/governance-smoke.ps1` 通过：有效夹具通过，断链、超预算和 CRLF 被拒绝，恢复后再次通过。
- `scripts/verify.ps1` 通过：CMake 配置、Release 构建成功，WindowSmoke / CaptureSmoke 2/2 通过（40.45 秒）。
- CI 命令使用的 `-SkipInteractive -ConfigureArgs '-A', 'x64'` 在本机通过；输出明确标记交互测试跳过。工作流 YAML 解析通过。
- 入库审查通过：61 个源码/测试/治理文件，staged diff 无空白错误；截图、日志、构建物、工具下载和 `.env` 被忽略，凭据模式检查通过。提交身份、工具署名及摘要以 Git 历史为准。
Phase 1 既有原始证据继续位于 [tests/VERIFICATION](../../../tests/VERIFICATION.md)，不重写为新机器矩阵。
本次新增 CI 仅为配置；没有远程执行或发布证据。
首提交包含 Phase 1 源码、测试源、脱敏文本证据与治理体系；不含构建物、桌面 PNG、日志、工具下载或机器密钥。
