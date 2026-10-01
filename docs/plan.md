# HuxerUI 通用 Windows 安装器项目计划（v2）

## 进度（2026-10-01）

- [x] 引擎提取：`src/engine/installer_engine.cpp`（vendor 自 SDK windows_installer.cpp），**补齐了 Display.None 静默路径**（不建窗口、推荐值自动应答、退出码 0/3010/1602、`/norestart` 语义修正）；Embedded 显示模式交由父 bundle 驱动。
- [x] `branding.json` 运行时品牌注入（名称/发布者/logo/主题色/许可文本）。
- [x] 默认通用界面 `src/ui/default_app.cpp`：品牌面板→许可页（可选）→目录+快捷方式→进度→完成/失败；字符串走资源（10 语言，`{0}` 占位符运行时注入产品名）。
- [x] 参数化 `wix/Package.wxs.in` / `wix/Bundle.wxs.in`（`@@TOKEN@@` 占位符）。
- [x] `cmake/HuxerUIInstaller.cmake`：`huxerui_installer_add()`（源码接入，含 WiX 恢复与 installer.json 计划输出）。
- [x] `scripts/Restore-Wix.ps1` + `scripts/make-setup.ps1`（预编译接入一键打包；升级码按 AppId 用 RFC 4122 SHA1 派生，与 CMake `string(UUID ... TYPE SHA1)` 一致）。
- [x] `.github/workflows/release.yml`（发预编译 zip）与 `reusable-package.yml`（app 仓库 workflow_call）。
- [x] `README.md` + `examples/source-mode` + `examples/prebuilt-mode`。
- [ ] `mcpp.toml`：暂缓 —— BA 需要链接 MSVC 的 balutil.lib/dutil.lib 且引用 xim:wix payload 布局，需先在 Windows CI 验证 CMake 路径后再补。
- [ ] 阶段 3 试点（niki 预编译模式 / acgu 源码模式）与 Windows CI 冒烟：GUI 安装、`/quiet`、升级覆盖、卸载。
- [ ] 阶段 4 收敛与 winget 示例。

## 定位

专门服务 HuxerUI app 的 Windows GUI 安装器项目：`huxerui-installer/`（独立仓库，置于 `/home/farna/dev/cpp/mcpp/` 下）。

核心架构（沿用并泛化 HuxerUI 现有模式）：
- **成熟内核**：WiX v5 Burn bundle（检测/提权/回滚/卸载/静默全归 Burn）。
- **套壳**：HuxerUI 写的 Bootstrapper Application（BA），Burn 管引擎，BA 只管 UI。
- **winget 无 GUI 支持**：不单独做产物，Burn 原生 `/quiet /norestart` + `InstallFolder=` 变量；BA 处理 `Display.None` 静默模式（不建窗口、默认接受、返回标准退出码 0/3010/1602）。CI 发版时用 `wingetcreate` 生成 manifest。

## 两种接入方式（项目的核心设计点）

### 方式一：源码接入（深度定制）
app 把本项目作为源码依赖（git submodule / mcpp 包 / FetchContent）引入：
- 调 CMake 函数 `huxerui_installer_add(<target> [UI_SOURCES ...] [CONFIG branding.json])`
- 不传 `UI_SOURCES` 就用默认通用界面；传了就编译自己的界面（等价于现在各 app 的 `package/src/app.cpp`，如 Clash-Flux 的 logo 面板）
- 产出：编译进 app 构建树的 `<app>-Installer.exe`

### 方式二：CI 预编译接入（零构建）
本项目 CI 发布预编译包 `huxerui-installer-<ver>-windows-x86_64.zip`，含：
- 通用 BA exe（默认界面）+ `mbanative.dll` 等依赖
- 参数化 `Package.wxs.in` / `Bundle.wxs.in`
- `Restore-Wix.ps1`（搬自 `HuxerUI/cmake/HuxerUIWindowsInstaller.cmake:3-114` 的 nupkg+SHA256 恢复）
- `make-setup.ps1`（一键：staging → wix build MSI → 生成 payload 片段 → wix build bundle → setup.exe）

app 侧只需：下载 zip → 提供 `branding.json` + 图标 + 版本 → 跑 `make-setup.ps1` → 得到 `<app>-<ver>-windows-x86_64-setup.exe`。**不需要编译任何 C++**。

**关键技术点 —— 预编译模式的品牌注入**：BA exe 是通用的，per-app 品牌（名称/logo/主题色/字符串/许可文本）在运行时从 payload 目录的 `branding.json` + 图片文件读取（bundle 把这些作为 payload 带入，BA 启动时从自己所在目录加载）。app 身份（升级码、安装目录、快捷方式）本来就在 wxs 里，不经 BA exe——现有架构天然支持。

## 项目结构

```
huxerui-installer/
├── CMakeLists.txt / mcpp.toml
├── README.md                        # 两种接入方式文档 + winget 发布指引
├── src/
│   ├── engine/                      # BA 引擎：从 HuxerUI/platform/windows/windows_installer.cpp 提取
│   │   └── installer_engine.cpp     #   (WindowsInstallerSession 状态机 + Display.None 静默处理)
│   ├── ui/
│   │   ├── default_app.cpp          # 默认通用界面：欢迎→目录→进度→完成，读 branding.json
│   │   └── branding.cpp/.h          # branding.json 解析（名称/logo/颜色/字符串/许可）
│   └── main.cpp                     # RunInstallerApplication() 入口
├── wix/
│   ├── Package.wxs.in               # 参数化 MSI 模板（身份/升级码/快捷方式/注册表）
│   └── Bundle.wxs.in                # 参数化 bundle 模板（InstallFolder/CreateDesktopShortcut 变量契约）
├── scripts/
│   ├── Restore-Wix.ps1              # WiX 5.0.2 nupkg 恢复（固定 SHA256）
│   └── make-setup.ps1               # 一键打包（预编译接入的主入口）
├── cmake/
│   └── HuxerUIInstaller.cmake       # huxerui_installer_add()（源码接入的主入口）
├── .github/workflows/
│   ├── release.yml                  # 构建并发布预编译 zip
│   └── reusable-package.yml         # 供 app 仓库 workflow_call 的打包 job
└── examples/
    ├── source-mode/                 # 源码接入示例
    └── prebuilt-mode/               # 预编译接入示例（含示例 branding.json）
```

## 实施步骤

**阶段 1：引擎提取与品牌注入**
1. 建仓库骨架（CMake + mcpp.toml，依赖 HuxerUI SDK）。
2. 提取 BA 引擎：从 `HuxerUI/platform/windows/windows_installer.cpp` 与 `HuxerUI/include/huxerui/windows/installer.h` 提取引擎层；确认 `Display.None` 静默路径完整（不建 UI、默认接受、退出码）。
3. 写 `branding.json` 机制 + 默认通用界面（参考现有 app 的 `package/src/app.cpp` 取最大公约数：品牌面板、目录选择、桌面快捷方式勾选、进度、完成/失败页；字符串走资源，默认英文+简中）。
4. 参数化 `Package.wxs.in`/`Bundle.wxs.in`（基于 `HuxerUI/tools/huxerui_cli/templates/platform/windows/app/package/`，占位符化 name/id/version/icon/upgrade-code）。

**阶段 2：两种接入入口**
5. `cmake/HuxerUIInstaller.cmake`：`huxerui_installer_add()`（源码接入），含 WiX 恢复（搬 `_huxerui_restore_wix_package` 逻辑）。
6. `scripts/Restore-Wix.ps1` + `scripts/make-setup.ps1`（预编译接入）。
7. 本项目 `release.yml`：Windows runner 构建 → 产 `huxerui-installer-<ver>-windows-x86_64.zip` → GitHub Release（命名遵守 skill 规范）。

**阶段 3：试点接入**
8. **niki（预编译模式试点）**：niki 目前没有安装器、无历史包袱。CI 下载预编译包 + branding.json → 出 `niki-<ver>-windows-x86_64-setup.exe`。
9. **acgu（源码模式试点）**：把 `acgu/platform/windows/package/` 的模板拷贝换成 `huxerui_installer_add()`，验证与现有 `huxerui package windows` 流程的衔接（或改由 make-setup.ps1 接管）。
10. 验证：Windows CI 跑通构建→打包→命名校验；手工/CI 冒烟：双击 GUI 安装、`/quiet` 静默安装、升级覆盖、卸载。

**阶段 4：收敛与 winget**
11. Clash-Flux / llm-switch / apitab 收敛到统一接入；Clash-Flux 的 logo/OpenSSL 扩展走源码模式自定义；修 Clash-Flux `huxerui.cmake:140` 残留的 `@PROJECT_NAME@`。
12. 加一个 app 的 winget 发布示例：release workflow 里 `wingetcreate` 生成 manifest（URL + SHA256 + `/quiet` switches）。

## 注意点
- 本机 Linux 无法验证 WiX 构建，验证全靠 Windows CI runner。
- 引擎从 HuxerUI 提取后，HuxerUI 仓库里的 `windows_installer.cpp` 保留不动（SDK 模板继续工作）；后续可考虑让 SDK 反过来依赖本项目，但不在本期。
- 代码签名不在本期（需证书）；README 留 signtool 挂接点说明。
