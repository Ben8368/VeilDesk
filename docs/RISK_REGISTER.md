# 风险台账

风险记录未验证假设与外部不确定性；已产生的实现妥协在 [TECH_DEBT](TECH_DEBT.md)。
当前条目均开放；责任域按 [MAINTAINERS](MAINTAINERS.md)，Gate 评审由项目维护者接受。

| ID | 风险 / 未验证假设 | Gate / 影响 | 处理与关闭证据 |
| --- | --- | --- | --- |
| RISK-001 | Windows 10/11 目标版本、GPU/驱动、OBS/截图工具、第二显示器与 DPI 矩阵未覆盖 | 通用兼容承诺；G1 仅限定环境 | P1-01/P1-02 按 QUALITY 真机对照；细节见 Phase 1 验证记录 |
| RISK-002 | WDA / DISPLAY_ONLY 不是对系统控制者的边界；排除 Viewer 可暴露底层第三方窗口 | 所有阶段的安全/产品文案 | 持续限制威胁模型；不可用普通 smoke 关闭，范围改变需 ADR |
| RISK-003 | 同用户进程身份、PID 复用、句柄继承/转移及共享纹理内容泄漏 | 阻断 G2 授权能力 | [原型证据](evidence/p2-broker/README.md)采用保留进程对象/私有继承通道；挂起启动/真实 SID/session/creation/image/角色已实现，publisher/envelope 未完成；恶意同用户窃取与句柄转移仍开放 |
| RISK-004 | 授权撤销不撤回已交付像素；旧资源和在途帧可继续被访问 | 阻断 G2 撤销承诺 | [原型证据](evidence/p2-broker/README.md)验证旧快照不变及帧间撤销；在途撤销、恶意留存和完整故障矩阵仍开放 |
| RISK-005 | 跨 adapter、同步死锁、device loss、带宽和长期资源使用未确定 | G2 兼容与性能 | [GPU 证据](evidence/p2-broker/zero-copy.md)已覆盖本机跨 adapter 拒绝/超时；1080p 吞吐约 18FPS，private bytes 热身后增长、长期稳定性仍未验收 |
| RISK-006 | 服务账户、IPC、策略持久化与安装恢复可能扩大权限/数据风险 | 阻断 G3 | [服务证据](evidence/p3-service/README.md)仅覆盖 console IPC/策略内核；SCM、LocalService、ACL 拒绝、Broker 强制执行和持久化未验收 |
| RISK-007 | Hyper-V 硬件/SKU、显示/GPU路径及应用在 Guest 中行为尚未知 | 阻断 G4 | [只读探针](evidence/p4-workspace/README.md)显示管理工具不可用；缺测试机/镜像，P4-02/P4-04 未运行 |
| RISK-008 | Guest/Host 剪贴板、文件、网络、音频、外设及策略恢复可能破坏隔离/可用性 | 阻断 G4 相关通路 | P4-03/P4-05 独立默认策略、负向与恢复测试 |
| RISK-009 | 诊断截图、日志、VM 数据可能包含隐私 | 所有阶段的提交/分享/分发 | 忽略本地产物，提交/分享前脱敏检查；新增收集/上传需独立授权 |

## 条目更新

新条目记录 ID、假设、影响、阻断 Gate、责任域、减缓策略和关闭证据。
只有真实证据或维护者记录的限定接受能改变结论；延期/接受不等于解决。
关闭后移入按日期的 archive，保留原 ID 与链接；Context 只链接当前风险摘要。
