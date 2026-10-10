# VeilDesk 产品边界

VeilDesk 的长期目标是 Windows Secure Workspace：敏感内容通过受控显示通路输出，并通过独立授权通路向可信录制者交付。
目标用户是需要限制常见截屏/录屏路径、同时保留可解释可信协作出口的 Windows 工作区使用者。
当前事实只在 [CONTEXT](../CONTEXT.md)，实现证据只在对应验证记录。

## 当前 PoC 目标

- 本进程原生顶级窗口，GPU 动态测试源、帧计数/时间、resize/DPI 与窗口模式。
- Windows 官方 WDA 与可回退 DISPLAY_ONLY 的状态和实际捕获行为。
- 普通 GDI 捕获客户端和 PNG，先取无保护基线，再比较保护模式。
- 明确暴露失败、降级和未验证范围，避免把 API 成功当作安全保证。

## 长期目标与边界

- 经受控内容源实现可信录制、Capture Broker 和临时授权；规划见 [ROADMAP](ROADMAP.md)。
- Rust SecureService 管理策略/身份/生命周期；不能单靠提升权限保证授权安全。
- Hyper-V 或独立 Workspace 中接入 Chrome / Edge、微信、QQ、飞书、Office 和其他 Windows 应用；每类逐项验证。
- WDAC / VBS / HVCI 为候选增强，必须分别确认环境、运维影响和实测价值。

## 非目标

- 不宣称防御所有恶意软件、管理员完全控制、kernel/driver/hypervisor 控制者或物理相机。
- 不 Hook 截图 API，不注入 DLL，不更改其他进程窗口 affinity，不做内核驱动或未公开 DWM Hook。
- 当前不以 Viewer PoC 代表整个桌面或第三方应用已受保护。
- 当前不提供服务安装器、远程上传、云录制、策略部署或自动虚拟机创建。

## 能力表述

外部材料、README、截图、日志与 PR 使用相同证据语义。
“已验证”必须带环境和路径；某一个 API / GPU / 捕获工具通过不能覆盖整个 Windows 生态。
能力记录格式与索引见 [PRODUCT_CAPABILITY_MATRIX](PRODUCT_CAPABILITY_MATRIX.md)。
