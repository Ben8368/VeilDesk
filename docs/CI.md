# GitHub CI 与依赖维护

配置入口：[workflow](../.github/workflows/ci.yml)、[Dependabot](../.github/dependabot.yml)。证据状态独立记录在 Context；这里维护配置和操作语义。

## 检查与限制

| 检查 | 范围 |
| --- | --- |
| Governance and workflow syntax | 文档预算/链接/UTF-8/LF、治理与 Git 署名 hook 正负回归、固定 actionlint 1.7.12 |
| C++ x64 / Release、Debug | windows-2022、VS 2022、x64、两配置编译；跳过桌面/GPU CTest |
| Rust / 1.85.1、stable | 有 service/Cargo.toml 时运行；fmt、clippy -D warnings、锁定依赖测试/Release、真实 console IPC 和运维预览 |
| Portable core / macOS | macos-14、C++ envelope fixture、Rust 策略与非 Windows 服务拒绝；不验证 macOS 平台后端 |
| CI required | 汇总 Windows 与 macOS 核心结果；只有没有服务 manifest 时接受 Windows Rust skipped，其他失败/取消不通过 |

push main、pull_request main、merge_group、手动和周一 10:17（Asia/Shanghai）触发。PR 不设路径过滤，避免 required check 被省略；同一 PR 的新运行取消旧运行。
所有外部 Actions 固定完整 commit SHA；checkout 不保留凭据，GITHUB_TOKEN 只读。不使用 pull_request_target、secrets、提权服务安装、VM 创建或安全策略修改。
缓存只保存 crates.io registry 下载，不缓存可执行构建产物；CTest 与 console IPC 日志最多保存 7 天，不上传桌面 PNG、私有配置或产品发布包。
建议分支规则要求稳定检查名 `CI required`；规则涉及合并权限，另行由维护者确定，不在工作流中自动改写。
GitHub-hosted CI 不能替代本机 `scripts/verify.ps1` 的交互桌面/GPU 检查、SCM/ACL 安装验收或 Guest 隔离证据，见 [QUALITY](QUALITY.md)。

## Dependabot

默认分支的配置启用 GitHub Actions 版本更新，周一 10:00（Asia/Shanghai）；minor/patch 分组，major 独立 PR，每生态最多 5 个版本 PR。
Dependabot PR 使用普通 pull_request 和只读 CI，不跳过检查、不授予 secrets，也不自动批准或合并。告警和安全修复功能通过仓库设置启用，独立于版本更新时间表。
当前远端没有 Cargo manifest 时不配置 `/service`，避免产生无法找到依赖文件的更新任务。服务源码和锁文件进入默认分支后，向 updates 添加：

```yaml
  - package-ecosystem: cargo
    directory: /service
    schedule:
      interval: weekly
      day: monday
      time: '10:00'
      timezone: Asia/Shanghai
    open-pull-requests-limit: 5
    commit-message:
      prefix: chore
      include: scope
    groups:
      cargo-minor-patch:
        patterns: ['*']
        update-types: [minor, patch]
```

精确版本的 Rust 依赖更新需同时审查 Cargo.toml/Cargo.lock、MSRV、ABI、许可证和两工具链 CI；不能只更新 lock 后宣称兼容。
官方依据：[Dependabot 启用](https://docs.github.com/en/code-security/how-tos/secure-your-supply-chain/secure-your-dependencies/configure-version-updates)、[配置选项](https://docs.github.com/en/code-security/reference/supply-chain-security/dependabot-options-reference)、[Dependabot 的只读 Actions](https://docs.github.com/en/code-security/reference/supply-chain-security/dependabot-on-actions)。
