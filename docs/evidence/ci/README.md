# GitHub CI / Dependabot 验证记录

日期：2026-10-10（Asia/Shanghai）。远端版本 `88d1f3b65cd4c4b1385090b3963bb16e03b7bca4`；当时本地产品验证使用该版本加上一轮未入库的 Phase 2–4 工作树。
本轮提交范围仅 workflow、dependabot.yml、CI 配置说明、治理 requiredFiles；不包含上一轮产品源码、截图、日志或构建物。
后续说明：Phase 2–4 源码已进入本地 Git 历史，仍未 push 到远端；本记录描述的 CI 轮次范围不变。

## 实际验证

| 范围 | 命令/实际结果 |
| --- | --- |
| workflow 语法 | `go run github.com/rhysd/actionlint/cmd/actionlint@v1.7.12 -color .github/workflows/ci.yml`，退出码 0 |
| PowerShell 步骤 | 解析全部 workflow run block；从实际汇总步骤执行 2 个成功与 4 个失败/取消/错误 skip 场景，全部符合预期 |
| 配置解析与 SHA | Dependabot YAML 解析通过；远端 `.github/dependabot.yml` blob SHA 与本地 git hash-object 相同；Actions 完整 SHA 对照官方 release/tag API |
| 治理与无桌面入口 | `scripts/verify.ps1 -SkipInteractive` 退出码 0；文档正负检查、Release、BrokerProtocol、Rust 4/4、console IPC、服务/VM 预览通过；桌面/GPU 项明确未运行 |
| C++ Debug | Debug 编译成功；排除 Interactive/四个桌面 GPU 用例后的 BrokerProtocol 1/1 通过 |
| Rust 最低版本 / stable | 1.85.1 与 stable（实际 1.99.0）：fmt --check、clippy --locked --all-targets -- -D warnings 通过；两版本策略 4/4、Release 与真实 console IPC 通过 |
| GitHub hosted CI | [首轮运行](https://github.com/Ben8368/VeilDesk/actions/runs/38030964691) success；治理/语法、C++ Debug/Release、CI required 成功；远端无 service manifest，Rust 明确 skipped |
| Dependabot 实际更新任务 | [首轮运行](https://github.com/Ben8368/VeilDesk/actions/runs/38030972403) success；版本配置已在默认分支激活，没有待处理更新 PR |
| Dependabot alerts | PUT 后 GET vulnerability-alerts 返回 204，确认启用 |
| Dependabot security updates | PUT 后 GET automated-security-fixes 返回 enabled=true、paused=false |

## 限制与跟进

远端仍为 Phase 1 产品源码；其 CTest 只有桌面用例，被明确排除，不把空测试集写成测试通过。Rust、Broker 的本地结果不能充当远端运行证据。
GitHub Actions 更新已启用，每周一 10:00（Asia/Shanghai），minor/patch 分组、major 独立；源码/锁文件进入默认分支后按 [CI 说明](../../CI.md) 添加 Cargo 更新配置。
没有启用 auto-merge、自动批准、扩大 workflow token 权限、修改分支合并规则或执行服务/虚拟机/安全策略部署。
真实 GPU/捕获、SCM/LocalService ACL、Guest 边界仍分别依照 [QUALITY](../../QUALITY.md) 验收；本记录不推进 G2–G4。

初次 governance smoke 因新文档链接的 Dependabot 配置未进入夹具复制清单而失败；将配置与说明加入 requiredFiles 后正负回归通过，未抬高文档预算。
本轮为既有 Rust toolchain 补充 rustfmt/clippy，并执行格式化；Phase 2–4 源码仍未随 CI 提交发布。
