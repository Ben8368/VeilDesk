# 责任域与评审

项目维护者为本项目发起人；Git author 使用其现有配置。本文不伪造团队、账号或 CODEOWNERS。

| 责任域 | 评审重点 |
| --- | --- |
| 产品 / 治理 | 范围、授权阶段、事实源、能力文案和 Gate |
| Win32 / Graphics | HWND/DPI、D3D11/DXGI、错误、同步、GPU/COM 资源 |
| Security / Capture | 威胁模型、身份、授权、IPC/共享资源、撤销和隐私 |
| SecureService | 最小权限、会话、策略、服务生命周期和安装恢复 |
| Workspace / Virtualization | Guest/Host 边界、VM 数据、跨边界通路、应用与硬件矩阵 |
| Quality / Supply chain | 真实环境、自动/人工证据、依赖来源、更新和可重复构建 |

阶段、长期权限、身份/授权、IPC、技术栈、上传、虚拟化和安全策略的实质变更需维护者评审并形成 ADR 或明确的限定接受。
执行者先完成可审查材料，不把既有授权重复变成确认问题；没有授权的新高影响操作仍需澄清。
只有计划文档或 AI 自评不能代表维护者接受 Gate。
