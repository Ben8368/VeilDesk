# VeilDesk AI 协作入口

所有任务先读 [CONTEXT.md](CONTEXT.md) 和 [LESSONS.md](LESSONS.md)，再按实际任务选择下表命中项。
不要递归读取全部链接；工具摘要只能指向本入口，不复制规则或阶段状态。

| 实际任务 | 按需读取 |
| --- | --- |
| 产品、阶段、规划 | [PRODUCT](docs/PRODUCT.md)、[ROADMAP](docs/ROADMAP.md)、[DEVELOPMENT_BLUEPRINT](docs/DEVELOPMENT_BLUEPRINT.md) |
| 任意产品源码 | [AI_RULES](docs/AI_RULES.md)、[ARCHITECTURE](docs/ARCHITECTURE.md)、[DEVELOPMENT_GUIDE](docs/DEVELOPMENT_GUIDE.md)、[QUALITY](docs/QUALITY.md) |
| 显示保护、捕获、IPC、权限、服务或虚拟化 | [SECURITY](SECURITY.md)、[CAPTURE_CONTRACT](docs/CAPTURE_CONTRACT.md)、相关 [ADR](docs/ADR/README.md) |
| 能力或兼容性结论 | [PRODUCT_CAPABILITY_MATRIX](docs/PRODUCT_CAPABILITY_MATRIX.md)、[QUALITY](docs/QUALITY.md)、[Phase 1 证据](tests/VERIFICATION.md) |
| 风险、技术债、Gate 推进 | [RISK_REGISTER](docs/RISK_REGISTER.md)、[TECH_DEBT](docs/TECH_DEBT.md)、[ROADMAP](docs/ROADMAP.md) |
| 治理、Git、文档 | [GOVERNANCE](docs/GOVERNANCE.md)、[AI_RULES](docs/AI_RULES.md)、[CONTRIBUTING](CONTRIBUTING.md) |
| 长期决策与责任域 | [MAINTAINERS](docs/MAINTAINERS.md)、[ADR](docs/ADR/README.md) |

## 硬边界

- 当前能力和阶段以 Context / 验证记录为准；计划、文档或模拟故障不等于实现或真机支持。
- Phase 1 使用 Windows x64、VS 2022、C++20、CMake、Win32、D3D11、DXGI 和 Windows SDK；无第三方 GUI 框架。
- WDA / DISPLAY_ONLY 属于官方捕获限制，不是真正对抗完全控制机器攻击者的安全边界。
- 只对本进程的顶级 HWND 设置 affinity；禁止修改其他进程 HWND、DLL 注入、API Hook、内核 patch 和未公开 DWM Hook。
- Phase 2–4 按 Roadmap 独立启动；记录未来计划不授权开发、安装服务、启用 Hyper-V 或改变系统安全策略。
- 用户的新授权优先于本入口的阶段限制；确认范围后更新 Context，不重复询问已授权的工作。
- Win32 采用 Unicode；COM 用 ComPtr，其他资源用 RAII；关键 API / HRESULT 检查结果，输出具体失败原因。
- 原始截图可能含桌面隐私；不得把日志、截图、构建物、密钥或机器私有配置放进 Git。

## 工作闭环

1. 确认范围、事实源和需验证的结论；源码或治理改动后按 [AI_RULES](docs/AI_RULES.md) 输出 `🚦 Audit Report`，再验证。
2. 文档修改运行 `powershell -NoProfile -File scripts/governance-docs-check.ps1`；源码修改使用 `scripts/verify.ps1`。
3. 不编造验证结果；未执行项说明原因。构建成功不能替代捕获、隔离、兼容性或安全证据。
4. 事实变化只回填对应权威文档；当前状态放 Context，计划放 Roadmap，工作包放 Blueprint，经验放 Lessons。
5. 提交、推送、发布分别按用户授权执行；本轮已授权的动作不再索要确认。Git 格式与 AI trailer 见 AI_RULES。
6. 默认用中文汇报；保留 API、命令和标识符原文。文本保存 UTF-8（无 BOM）、LF。

治理预算、链接、ADR 和状态检查由 [GOVERNANCE](docs/GOVERNANCE.md) 定义，不通过抬高预算规避文档维护。
