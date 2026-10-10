# 架构与依赖边界

## Phase 1 已有结构

```text
main
 ├─ SecureWindow：HWND / 消息 / resize / DPI / 窗口模式
 ├─ DisplayProtection：本进程顶级 HWND 的 WDA 请求与读回
 └─ D3D11Renderer：测试内容 → GPU 表面 → DXGI Present

CaptureTest → 枚举/选择 Viewer → 桌面 BitBlt → WIC PNG
Logger / SystemInfo：诊断，不能承担保护或授权决策
```

Window 通过回调通知 resize；图形与保护模块分别拥有自己的资源和状态。
Renderer 的 `RenderFrame` 是测试源入口，未来可替换为纹理输入；现阶段没有跨进程纹理接口。
Direct2D / DirectWrite 是 Windows SDK 中同一 GPU 输出面的文字绘制工具，不是新增 GUI 框架。
CaptureTest 是验证程序，不是可信录制实现，也不成为保护措施的旁路。

## 后续目标数据流（设计）

```text
Protected Workspace / trusted producer
                │
                ▼
        ID3D11Texture2D source
           ├─────────► SecureViewer display output
           └─────────► Capture Broker ──grant──► trusted consumer
                            ▲
                     SecureService policy
```

Windows 显示限制负责 Viewer 输出；Broker 授权负责内容源向消费者交付。
共享资源一旦交付就可能被复制，不能把 DISPLAY_ONLY 等同于共享纹理机密性。
未来源从自有 producer 演进到隔离 Workspace，不能依靠拦截所有截图 API 接入第三方应用。

## 演进约束

- Phase 2 的 producer / Broker / Viewer 接口先收敛生命周期、同步、协议和权限，再扩大客户端。
- Phase 3 的 Rust 服务不接管 D3D11 渲染；负责策略和已审计 IPC，默认权限必须有理由。
- Phase 4 的 Guest / Host 信任边界单独验证，显示传输、剪贴板、文件、音频、外设逐项评估。
- API 和资源 owner 单一；窗口、设备、服务、虚拟机故障应有明确终态，不把失败伪装为受保护。
- 当前技术栈已接受；未来实现细节和依赖尚未锁定，接受新方案需相关 ADR。

威胁边界见 [SECURITY](../SECURITY.md)，授权语义见 [CAPTURE_CONTRACT](CAPTURE_CONTRACT.md)，接受依据见 [ADR](ADR/README.md)。
