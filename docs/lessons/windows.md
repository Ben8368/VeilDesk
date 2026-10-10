# Windows 开发经验

- VS 编译器文件存在不等于安装器实例完整，CMake 的发现失败先查注册状态与 SDK，不先改产品源码。
- 自定义 manifest 和链接器自动 manifest 可能冲突；保证唯一有效嵌入，实际检查 PMv2 / compatibility。
- 首次 ShowWindow 可能服从 STARTUPINFO 的隐藏命令；测试可在本进程第二次显式显示，隐藏/遮挡不能计作保护。
- `near` 等 Windows SDK 宏可能和局部名字冲突；显式包含关键 COM 声明，用不冲突的标识符。
- PowerShell 中文源文件统一 UTF-8；验证脚本用严格 UTF-8 读取，Git 文本保持 LF。
- 提交正文/Trailer 不用字面 `\n`，使用多个 `-m`，提交后解析 trailer。
