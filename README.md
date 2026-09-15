# PathHelper - Windows 文件对话框增强工具

PathHelper 是一个 Windows 文件对话框增强工具，通过注入 DLL 到文件对话框进程中，为标准的打开/保存对话框添加快速导航面板，支持历史路径记录、收藏夹和实时资源管理器路径监控；并可在文件对话框内联动 [Everything](https://www.voidtools.com/) 做全盘搜索，或给资源管理器注入一个只保留收藏的侧边面板。

![alt text](assets/README/image.png)

## 功能特性

### 🚀 核心功能
- **历史路径记录**：自动记录文件对话框中访问过的路径，支持快速回溯
- **收藏夹管理**：添加常用路径到收藏夹，一键导航
- **实时资源管理器监控**：显示当前打开的资源管理器窗口路径，直接跳转
- **自动导航**：打开文件对话框时自动跳转到最近使用的路径
- **Everything 全盘搜索**：监听文件对话框自带的搜索框，把关键字交给本机已安装的 Everything，并在搜索框正下方弹出结果面板；单击/`Ctrl`+单击多选，双击在当前对话框内跳转，右键可「在新窗口打开」，也可直接拖拽到资源管理器或其它程序
- **资源管理器收藏面板**：在设置里开启后，向 explorer.exe 注入一个只保留收藏功能的侧边面板，可以收藏当前文件夹/任意路径并管理它们（备注、排序、删除）

### 🎨 界面特性
- **现代化 UI**：使用 Direct2D 和 DirectWrite 渲染，支持圆角和阴影效果
- **主题支持**：内置浅色/深色主题，可自定义切换
- **可调节布局**：支持调整面板宽度、边距、字体大小等
- **路径智能显示**：自动隐藏公共路径前缀，显示关键路径信息

### ⚙️ 配置选项
- **注入进程管理**：选择要注入的目标进程（如 notepad、explorer 等）
- **快捷键支持**：自定义时间戳粘贴快捷键
- **自动启动**：支持开机自启并最小化到系统托盘
- **数据存储**：所有设置和历史记录存储在 `%USERPROFILE%\.PathHelper` 目录

## 系统要求

- **操作系统**：Windows 10/11（64 位）
- **开发环境**：Visual Studio 2022/2026，或 MinGW-w64（GCC 11 及以上，含 g++ / windres）
- **运行时**：VS 构建需要 Visual C++ Redistributable 2022；MinGW 构建为静态链接，不依赖额外运行库
- **权限**：需要管理员权限进行 DLL 注入

## 安装说明

### 方式一：下载发布版本
1. 从 [Releases](../../releases) 页面下载最新版本
2. 解压到任意目录
3. 运行 `PathHelper.exe`

### 方式二：从源码构建
1. 克隆仓库：
   ```bash
   git clone https://github.com/yourusername/pathhelper.git
   ```
2. 使用 Visual Studio 2022/2026 打开 `PathHelper.sln`
3. 选择 Release 配置（x64 或 Win32）
4. 构建解决方案
5. 输出文件位于 `Release/` 目录

### 方式三：使用 MinGW-w64（g++）构建
没有安装 Visual Studio 时，可以直接用 MinGW-w64 的 g++ 编译（仅 64 位）：

1. 安装 MinGW-w64（建议 UCRT 版），确认 `g++`、`windres` 在 PATH 中
2. 在仓库根目录执行：
   ```powershell
   powershell -ExecutionPolicy Bypass -File .\build_mingw.ps1 -Clean
   ```
3. 构建产物位于 `Release/`：`PathHelper.exe` 与 `Dll1.dll`（两者必须放在同一目录）

脚本会同时编译主程序和注入 DLL；产物为静态链接，不依赖 `libstdc++-6.dll` 等运行时库。
如果 `g++` 用于 x86 目标，请改用对应的 32 位工具链并自行调整。

## 使用方法

### 基本使用
1. 运行 `PathHelper.exe`
2. 在“注入管理”选项卡中添加要注入的进程（如 `notepad`）
3. 打开记事本的文件打开对话框
4. 在对话框右侧会出现 PathHelper 面板，显示历史路径和收藏夹

### 面板操作
- **历史路径**：点击路径项可直接导航到该目录
- **收藏夹**：点击星标图标添加/移除收藏
- **资源管理器路径**：显示当前打开的资源管理器窗口路径
- **右键菜单**：支持复制路径、在资源管理器中打开等操作

### Everything 全局搜索
1. 先在电脑上安装并运行 [Everything](https://www.voidtools.com/)（PathHelper 使用它的 IPC 接口，无需额外安装 SDK）
2. 在任意文件对话框右上角的**搜索框**里输入关键字
3. 搜索框下方会弹出一个结果面板，列出全盘匹配的文件/文件夹
4. **单击**结果 = 打开：**文件夹**在当前对话框里导航过去，**文件**交给系统默认程序打开
5. **Ctrl + 单击** = 加选 / 取消选中；**Shift + 单击** = 范围选择（多选，表头会显示"已选 N 项"）
6. **右键**结果 → 在当前窗口打开 / 在新窗口打开 / 复制完整路径（多选时"复制完整路径"会把所有选中项按行复制）
7. 按住结果**拖动**可以把文件（`CF_HDROP`）拖到资源管理器、聊天窗口、以及对话框本身；多选时一次拖出所有选中的文件

**结果面板本身**
- 标题栏左侧是「搜索中… / N 个结果」，右侧是 **✕ 关闭**按钮；关掉只是本次收起，下次新的搜索或新的对话框还会自动出现
- 拖**标题栏**可以把面板挪走（只影响这一次，不写配置）；拖**四条边**可以缩放，尺寸会记住
- 不想用这个功能的话，可以在主程序 **设置 → Everything 搜索面板** 里整体关掉

> 面板只在检测到 Everything 时才出现；如果 Everything 已安装但没运行，PathHelper 会尝试把它拉起来。

### 资源管理器收藏面板
1. 打开主程序 → **设置** → 勾选 **“资源管理器收藏面板”** → 保存设置
2. 程序会把 `Dll2.dll` 注入到 `explorer.exe`，每个资源管理器窗口右侧会出现一个只包含收藏的侧边面板
3. 面板上（右上角两个图标按钮，鼠标悬停会显示说明）：
   - **文件夹图标**：手动输入/粘贴任意路径进行收藏
   - **定位图标**：收藏当前资源管理器窗口所在的文件夹（可填备注）
   - **单击**收藏项 → 在**当前资源管理器窗口内**导航过去（不会新开窗口）
   - **拖动**收藏项 → 拖出文件夹，或拖动排序
   - **右键** → 在当前窗口打开 / 在新窗口打开 / 修改备注 / 复制路径 / 从收藏中移除
4. 取消勾选并保存后，Dll2.dll 会在 2 秒内自动撤掉所有收藏面板（无需重启资源管理器）

> 收藏数据和文件对话框面板共用 `%USERPROFILE%\.PathHelper\Favorites.jsonl`。

### 快捷键
- **时间戳粘贴**：在设置中配置自定义快捷键，快速插入当前时间
- **面板切换**：可通过系统托盘图标控制面板显示

## 配置说明

所有配置存储在 `%USERPROFILE%\.PathHelper\Setting.ini` 文件中：

```ini
[Settings]
; 自动导航到最近使用的路径
AutoToLatest=true

; 界面主题 (light/dark)
theme=light

; 历史面板宽度 (像素)
HistoryPanelWidth=200

; 面板边距 (像素)
PanelMargin=5

; 历史记录字体大小
HistoryPanelFontSize=10

; 是否隐藏公共路径前缀
StripCommonPrefix=true

; 最大历史记录显示数量
HistoryDisplayMax=5

; 时间格式
TimeFormat=%Y-%m-%d %H:%M:%S

; 时间戳快捷键
TimeHotkeyVK=0
TimeHotkeyMod=0

; 是否为资源管理器注入收藏面板（Dll2.dll）
ExplorerFavorites=false

; 是否在文件对话框的搜索框下显示 Everything 结果面板
EverythingPanel=true

; Everything 结果面板的尺寸（由面板自己拖动缩放后写入，0/缺省 = 自动）
EverythingPanelWidth=460
EverythingPanelHeight=0
```

> 改完设置**不用重启被注入的程序**：所有被注入的 DLL 都会盯着这个文件，
> 配置一变就自己重新读取并套用（字体、面板宽度、主题、开关等）。

## 项目结构

```
PathHelper/
├── Dll1/                    # 核心 DLL 模块（注入文件对话框进程）
│   ├── dllmain.cpp         # DLL 入口点和主逻辑
│   ├── panel.cpp           # 伴侣面板 UI 实现
│   ├── history.cpp         # 历史记录管理
│   ├── hooking.cpp         # API 钩子实现
│   ├── settings.cpp        # 设置管理
│   ├── everything.cpp      # Everything 检测 + IPC 查询
│   ├── dialogsearch.cpp    # UI Automation 监听对话框搜索框
│   ├── searchpanel.cpp     # Everything 结果浮层（点击打开 / 拖拽）
│   └── nlohmann/           # JSON 库
├── Dll2/                    # 收藏面板 DLL（注入 explorer.exe）
│   ├── dllmain.cpp         # 入口、资源管理器窗口挂载 / 设置轮询
│   ├── favpanel.cpp        # 只保留收藏的侧边面板
│   └── favstore.cpp        # 收藏数据读写（与 Dll1 共用 Favorites.jsonl）
├── PathHelper/              # 主程序模块
│   ├── PathHelper.cpp      # 主程序入口和 GUI
│   ├── Injector.cpp        # DLL 注入器（Dll1 / Dll2）
│   ├── ProcessManager.cpp  # 进程管理
│   ├── SettingsManager.cpp # 设置管理
│   ├── TrayManager.cpp     # 系统托盘管理
│   ├── ExplorerMonitor.cpp # 资源管理器监控
│   ├── ExplorerFavorites.cpp # 资源管理器收藏面板注入管理
│   └── AutoStartManager.cpp # 自动启动管理
└── .gitignore              # Git 忽略规则
```

## 开发说明

### 依赖项
- **Windows API**：Shell、COM、UI Automation
- **Direct2D/DirectWrite**：UI 渲染
- **nlohmann/json**：JSON 解析
- **Windows 注册表**：自动启动配置
- **Everything IPC**：通过 `WM_COPYDATA` 与 `EVERYTHING_TASKBAR_NOTIFICATION` 窗口通信，不需要 Everything SDK 的 `Everything64.dll`

### 编译注意事项
1. 需要安装 Windows SDK
2. 项目使用 Unicode 字符集
3. 需要链接 `ole32.lib`、`shell32.lib`、`d2d1.lib`、`uiautomationcore.lib` 等库（MinGW 下由脚本传入 `-luiautomationcore`）
4. 管理员权限需要 UAC 清单配置（清单见 `PathHelper/PathHelper.manifest`，MinGW 构建会自动嵌入）
5. 为兼容 GCC，`Dll1` 中原先使用 MSVC `__try/__except` 的防御性代码改成了 `IsValidComObject()` / `IsReadableMemory()` 指针校验（见 `Dll1/common.h`），在 MSVC 与 MinGW 下均可编译
6. `PathHelper.rc` 被 VS 保存为 UTF-16LE，windres 无法直接读取，`build_mingw.ps1` 会先转成 UTF-8 再用 `--codepage=65001` 编译
7. MinGW 的 gcc 默认会链入自带的 `default-manifest.o`，与自定义清单冲突，脚本通过自定义 specs 去掉该注入

### 调试技巧
1. 使用 DebugView 查看调试输出
2. 可附加到目标进程调试 DLL 注入
3. 检查 `%USERPROFILE%\.PathHelper\` 目录下的日志文件

## 常见问题

### Q: 为什么需要管理员权限？
A: DLL 注入需要 `SeDebugPrivilege` 权限来操作其他进程的内存。

### Q: 支持哪些文件对话框？
A: 支持标准的 Windows 文件打开/保存对话框，包括旧版 `GetOpenFileName` 和新版 `IFileDialog`。

### Q: 如何卸载？
A: 直接删除程序目录，并可选择删除 `%USERPROFILE%\.PathHelper` 目录清理数据。

### Q: 会影响系统性能吗？
A: PathHelper 使用轻量级钩子，对系统性能影响极小，仅在文件对话框打开时激活。

### Q: Everything 搜索需要额外配置吗？
A: 不需要。只要本机安装并运行了 Everything（1.4 及以上，默认开启 IPC），PathHelper 就能直接查询。
文件对话框搜索框的文本由 UI Automation 以 250ms 的间隔读取——它不会阻塞界面，也不会改写你输入的内容。

### Q: 资源管理器收藏面板怎么卸载？
A: 在设置里取消勾选“资源管理器收藏面板”并保存，Dll2.dll 会在 2 秒内撤掉所有面板。
DLL 本身会一直留在 explorer.exe 里直到重启资源管理器，这是 Windows 的限制（无法安全卸载已注入的 DLL）。

## 许可证

本项目采用 MIT 许可证。详见 [LICENSE](LICENSE) 文件。

## 贡献指南

欢迎提交 Issue 和 Pull Request！

1. Fork 本仓库
2. 创建功能分支 (`git checkout -b feature/AmazingFeature`)
3. 提交更改 (`git commit -m 'Add some AmazingFeature'`)
4. 推送到分支 (`git push origin feature/AmazingFeature`)
5. 开启 Pull Request

## 更新日志

### v1.0.0 (2024-01-01)
- 初始版本发布
- 基础历史路径记录功能
- 收藏夹管理
- 资源管理器路径监控
- 浅色/深色主题支持
- 系统托盘集成

---

**注意**：本工具仅用于学习和研究目的，请遵守相关软件的使用条款。
