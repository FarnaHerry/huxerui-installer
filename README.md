# huxerui-installer

**简体中文** | [English](README.en.md)

[HuxerUI](https://github.com/HuxerUI/HuxerUI) 应用的通用 Windows 安装器。

- **成熟内核**：WiX v5 Burn bundle 全权负责检测、提权、回滚、卸载与静默安装。
- **HuxerUI 套壳**：引导程序（BA）是一个 HuxerUI 程序——Burn 管引擎，BA 只管界面。
- **静默与 winget 就绪**：`setup.exe /quiet /norestart`，可选 `InstallFolder=`、
  `CreateDesktopShortcut=` 变量；静默时不创建窗口，提示自动取推荐答案，进程退出码为 `0`、
  `3010`（需要重启）或 `1602`（用户取消）。
- **多语言**：安装界面跟随 Windows 显示语言。简体中文为默认回退；`resources/strings/` 内置
  English、繁体中文（香港/台湾）、德语、西班牙语、法语、日语、韩语、巴西葡萄牙语目录。

## 两种接入方式

### 1. 源码接入（深度定制）

把本仓库作为源码依赖（git submodule、FetchContent、mcpp 包）引入，调用
`cmake/HuxerUIInstaller.cmake` 提供的 `huxerui_installer_add()`。不传 `UI_SOURCES` 即使用默认通用
界面；传了就编译自己的界面——等价于现在各 app 的 `package/src/app.cpp`（例如自定义 logo 面板）。
参见 [examples/source-mode](examples/source-mode)。

### 2. 预编译接入（零构建）

从 Release 下载 `huxerui-installer-<version>-windows-x86_64.zip`，提供 `branding.json`、图标、版本号
和 staged 应用载荷，运行包根目录的 `make-setup.exe` 即得 `<app>-<version>-windows-x86_64-setup.exe`，
**不需要编译任何 C++，也不需要 PowerShell**——WiX 工具集已随包内置（`wix.exe` 运行需要 .NET 6+
运行时）。CI 里也可继续用等价的 `scripts/make-setup.ps1`。参见
[examples/prebuilt-mode](examples/prebuilt-mode) 与可复用工作流
[.github/workflows/reusable-package.yml](.github/workflows/reusable-package.yml)。

每个 app 的身份信息（产品名、logo、主题色、许可文本）在运行时注入：bundle 把 `branding.json` 及其
引用的文件作为 payload 带入，通用 BA 启动时从自己所在目录读取。升级码、安装目录、快捷方式都写在
WiX 模板里，不经过 BA 二进制。

## 仓库结构

```
src/engine/installer_engine.cpp  # Burn BA 引擎（vendor 自 HuxerUI SDK）+ 静默模式驱动
src/ui/branding.{h,cpp}          # branding.json 运行时加载
src/ui/default_app.cpp           # 默认通用界面
src/main.cpp                     # wWinMain 入口
src/shared/flat_json.h           # BA 与 make-setup 共用的扁平 JSON 解析器
src/pack/make_setup.cpp          # make-setup.exe——原生一键打包器（无需 PowerShell）
resources/strings/               # 界面字符串目录：default.properties = 简体中文，另有 en 及多语言
wix/Package.wxs.in               # 参数化 MSI 模板
wix/Bundle.wxs.in                # 参数化 bundle 模板（InstallFolder/CreateDesktopShortcut 变量契约）
cmake/HuxerUIInstaller.cmake     # huxerui_installer_add()——源码接入入口
scripts/Restore-Wix.ps1          # 固定 WiX 5.0.2 nupkg 恢复（SHA256 校验）
scripts/make-setup.ps1           # PowerShell 版打包入口（行为与 make-setup.exe 一致）
.github/workflows/release.yml    # v*.*.* tag 触发，构建并发布预编译 zip
.github/workflows/reusable-package.yml  # 供 app 仓库 workflow_call 的打包 job
```

## 本地构建预编译包

需要 Windows + MSVC C++ x64 工具链、HuxerUI 源码（或已安装的 SDK）、以及运行 `wix.exe` 所需的
.NET 6+ 运行时：

```powershell
git clone --depth 1 https://github.com/HuxerUI/HuxerUI.git huxerui-src
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DHUXERUI_SOURCE_DIR="$PWD/huxerui-src" `
    -DCMAKE_CXX_COMPILER=cl
cmake --build build --config Release
cmake --install build --config Release --component HuxerUIInstaller_huxerui_installer_ba --prefix out/ba
cmake --install build --config Release --component HuxerUIInstaller_make_setup --prefix out
```

`-DHUXERUI_SOURCE_DIR` 以源码方式构建 HuxerUI（发版 CI 就是这么做的）；改用已安装的 SDK 则去掉它，
改为 `-DCMAKE_PREFIX_PATH <huxerui-sdk-prefix>`。

WiX 5.0.2 会自动恢复到 `HUXERUI_WIX_ROOT`（默认 `<build>/.wix`）并做 SHA256 校验；版本与哈希由
`cmake/HuxerUIInstaller.cmake` 和 `scripts/Restore-Wix.ps1` 共享。

## 发版命名

遵循 HuxerUI 发版约定：tag 为 `v<MAJOR.MINOR.PATCH>`，Release 标题为 `huxerui-installer v<version>`，
产物名为 `<product>-<version>-windows-x86_64.zip`（不带前导 `v`）。

## 说明

- 代码签名暂不在范围内；可在打包（`make-setup.exe` 或 `make-setup.ps1`）之后用 `signtool` 给 `*-setup.exe` 签名（要完整签名，
  需在打包前先签 BA 可执行文件）。
- 验证依赖 Windows CI runner；WiX 构建无法在 Linux/macOS 上运行。
- HuxerUI SDK 自带的 `windows_installer.cpp` 保持不动，继续服务 SDK 模板项目；本仓库的引擎在此之上
  增加了静默模式驱动，是后续安装器工作的收敛点。

## 许可证

MIT，见 [LICENSE](LICENSE)。vendor 的引擎源自 HuxerUI SDK（Apache-2.0），并链接 WiX 工具集的
bootstrapper API（MS-RL）。
