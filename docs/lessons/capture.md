# 捕获与图形经验

- WDA 成功读回 0x11 只确认状态，不确认每条路径都排除内容。
- BitBlt 成功而 PNG 显示后方桌面可符合窗口排除；标记缺失仍须相同位置无遮挡基线对照。
- 基线被其他窗口遮挡时，先修正测试条件，不通过调整结果阈值伪造通过。
- DISPLAY_ONLY 的模拟失败验证回退，不验证真实驱动是否不支持；结果必须区分。
- resize 前先解除所有 back buffer 的 D2D/RTV/绑定引用，尺寸和 flags 随真实 swap chain 状态更新。
- 共享 GPU 纹理与 DISPLAY_ONLY 不是同一安全概念；授权和撤销必须另有访问/生命周期设计。
- IDXGIKeyedMutex::AcquireSync 的 WAIT_TIMEOUT/WAIT_ABANDONED 是正值，必须显式要求 S_OK；不能只按 SUCCEEDED/FAILED 判断同步成功。
- DisconnectNamedPipe 会丢弃未读数据；需要可靠诊断响应时采用有界回执，不用可能无限阻塞的 FlushFileBuffers 替代协议超时。
