# ADR 0007：同 adapter 的 GPU Viewer 输入

- 状态：已接受
- 日期：2026-10-10

## 背景

维护者明确要求优先偿还 TD-003，正常 Viewer 禁止 CPU 帧 readback/upload，保持现有架构与官方显示限制。

## 决策

接受依据为本轮明确的 TD-003 技术要求；不等于 G2 通过。
SharedFrameProducer 生成 GPU 图案、发布匿名只读 NT handle；SharedFrameConsumer 在调用者 device 上打开资源，校验协议、完整 descriptor 和 adapter LUID。协议 v2 增加 LUID，拒绝旧版本。
SharedTexture RAII 只在 AcquireSync(1) 精确返回 S_OK 时交付纹理，最多等待 1000ms；Renderer 在锁内 CopyResource 到同 device 的本地 GPU snapshot，ReleaseSync(1) 后允许重复显示和 resize。输出 CopySubresourceRegion 原生裁切，不承诺 scaling。
每帧独立不可变资源，producer 只做 0→1；多个串行读者做 1→1，不重新发布写 key 0。跨 adapter、device loss、abandoned 或超时拒绝/退出，禁止 CPU fallback。
FrameReadback 是 Recorder/test 独立库，不链接 SecureViewer；CPU readbacks/uploads、opens/copies 通过 Debug/Release 计数与源码结构检查验证。

## 后果

显示 zero-copy 指零 CPU 帧往返，仍有 GPU copy；WDA/DISPLAY_ONLY 仍非 DRM 或抵抗系统控制者的边界。
旧句柄仍可读旧像素，但 producer 从不向其写未来帧；撤销无法召回已交付像素。
不可变纹理保留 TD-005 创建成本，先测量再决定资源池；身份/lease/service 仍按 G2/G3 单独验收。

## 替代方案

CPU readback/reupload 违反目标；重用已交付资源会让旧 handle 看到未来帧；预先引入纹理池缺少性能依据。

## 关联文档

[ADR 0004](0004-private-broker-prototype.md)、[Architecture](../ARCHITECTURE.md)、[Contract](../CAPTURE_CONTRACT.md)、[Risk](../RISK_REGISTER.md)。
官方依据：[OpenSharedResource1](https://learn.microsoft.com/en-us/windows/win32/api/d3d11_1/nf-d3d11_1-id3d11device1-opensharedresource1)、[ReleaseSync](https://learn.microsoft.com/en-us/windows/win32/api/dxgi/nf-dxgi-idxgikeyedmutex-releasesync)、[CopySubresourceRegion](https://learn.microsoft.com/en-us/windows/win32/api/d3d11/nf-d3d11-id3d11devicecontext-copysubresourceregion)。
