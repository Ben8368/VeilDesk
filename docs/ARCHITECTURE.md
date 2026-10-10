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
Renderer 的 `RenderFrame` 保留 Phase 1 测试源；共享模式通过 `SetSourceTexture` 输入已锁定且校验的 GPU 纹理。
Direct2D / DirectWrite 是 Windows SDK 中同一 GPU 输出面的文字绘制工具，不是新增 GUI 框架。
CaptureTest 是验证程序，不是可信录制实现，也不成为保护措施的旁路。

## Phase 2 原型数据流

```text
CaptureBroker --producer (独立进程 / D3D11)
      → 私有继承管道 + unnamed NT texture handle
      → CaptureBroker (保留准确进程对象 / lease / ACK)
          ├→ SecureViewer --shared-source (WDA 保持 / 帧校验)
          └→ --consumer (GPU readback / 有界 PNG 序列)
```

每帧创建新资源；旧资源不再写入未来像素。每通道只允许一帧在途，先 ACK 再下一帧。
NT 纹理句柄不继承，仅 DuplicateHandle 至已启动且保留的子进程；不提供按 PID 附加或公开资源名称。
子进程挂起启动，真实 PID/creation time/SID/session/image 与自有角色匹配后恢复；交付前重核 retained 对象，publisher/signature NotVerified；增量见 [enrollment 证据](evidence/p2-broker/enrollment.md)。
Viewer 在 Renderer 同一 device 上 OpenSharedResource1，校验 LUID/descriptor，锁内 GPU copy 至本地 snapshot，再 copy 到保护 swap chain；无 CPU 帧接口。尺寸按原生像素裁切，resize 不 readback。
Producer GPU shader 生成测试图案；Recorder/test 的 readback 在独立库，SecureViewer 不链接该库；独立 producer 不采用第三方应用画面。
Rust 服务是独立 SCM/诊断 IPC 原型，尚未对 Broker 策略强制执行；Workspace 尚无 Guest 显示通路。
契约见 [CAPTURE_CONTRACT](CAPTURE_CONTRACT.md)，证据与限制见 [Context](../CONTEXT.md)。

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
