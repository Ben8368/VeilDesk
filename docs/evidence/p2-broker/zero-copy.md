# TD-003 GPU Viewer 验证

日期：2026-10-10（Asia/Shanghai）；G2 未通过。机器为本地开发机（机器名不公开），Windows 11 Pro build 26200.9457，NVIDIA RTX 4060 / 32.0.16.1062，MSVC v143 / SDK 10.0.26100.0。

## 首轮实现验证

基线 `scripts/verify.ps1` 退出 0：Release、CTest 5/5、Rust 策略 4/4、console IPC；本次重构后同命令再次退出 0。Debug build 无警告；Debug 的 BrokerProtocol/BrokerSmoke/SharedTextureProbe 3/3。
命令：`cmake --build build --config Debug`、`ctest --test-dir build -C Debug -R 'Broker|SharedTexture' --output-on-failure`。
实际 approved Viewer 打开 8 帧，ZeroCopyStats 为 CPU readbacks=0、CPU uploads=0、opens=8、GPU copies=40（包含重绘）；WDA 已读回确认，DISPLAY_ONLY 创建接受。
Producer shader 输出经独立 readback 逐像素匹配诊断图案；Recorder PNG 独立解码匹配 frame ID。旧快照不变、非继承 HANDLE、真实 keyed mutex 超时通过。

## 限制

自动 instrumentation 断言、扩展 descriptor/adapter 负向矩阵和 1080p/60FPS/60 秒性能基线仍待补充；本记录不提前关闭 TD-003 或 TD-005。
零拷贝指零 CPU 帧往返，仍有 GPU snapshot/presentation copy。正常显示原生裁切，不承诺 scaling；跨 adapter Unsupported。
OBS/WGC/DD、其他 GPU/OS、device loss、恶意主体/句柄转移和真实 PID 复用 NOT TESTED。G2/G3/G4 未通过；未安装服务或改变虚拟化/安全策略。
历史原型结果保留在 [Broker 证据](README.md)，其 CPU 显示描述仅适用于历史版本。
