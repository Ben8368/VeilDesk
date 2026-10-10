# 能力与证据索引

本文件维护记录格式和指向证据的索引，不复制各环境详细状态。
当前阶段摘要在 [CONTEXT](../CONTEXT.md)，状态词以 [GOVERNANCE](GOVERNANCE.md) 为准。

| 能力域 | 权威证据 / 规划位置 |
| --- | --- |
| Viewer / D3D11 / WDA / DISPLAY_ONLY / GDI PNG | [Phase 1 验证记录](../tests/VERIFICATION.md) |
| 外部工具和目标 OS 细分实际结果 | [README 手工测试表](../README.md#manual-testing)，P1 工作包启动后回填证据 |
| Broker / 共享纹理 / 可信消费者 | [Broker 原型证据](evidence/p2-broker/README.md) |
| Rust SecureService / 策略 | [服务原型证据](evidence/p3-service/README.md) |
| Hyper-V / 第三方应用 / WDAC、VBS、HVCI | [Workspace 探针与未运行项](evidence/p4-workspace/README.md) |

## 新能力记录格式

能力 ID、阶段/工作包、状态、源码版本、OS build/SKU、GPU/驱动、显示器/DPI、API/工具/后端、步骤、输入、Expected/Actual、失败/清理、结论限制、证据位置、关联 Risk/Gate。
性能增加采样方法、固定规模、峰值/时延和统计方式；不能用主观“流畅”替代数据。
授权增加主体/会话、lease/generation、拒绝/到期/撤销及已交付内容限制。
第三方应用按应用版本和 Guest/Host 条件独立记录，禁止从一个应用外推为全部兼容。

文档中的设计数据流与未来接口不产生支持状态；模拟不支持不产生真实驱动不支持结论。
