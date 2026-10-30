# IMETip

**Windows 输入法状态提示工具** —— 当光标落在可编辑输入框时，在光标右侧、与光标同一水平线短暂显示当前是中文还是英文（以及 Caps Lock 是否开启），5 秒后自动消失。不抢焦点、可点击穿透、完全事件驱动。

![指示窗样式](docs/preview.png)

## 特性

- **只在输入态出现**：仅当焦点位于可编辑文本控件且存在光标时显示，点击按钮、菜单、只读文本都不会触发。
- **事件驱动，零轮询**：通过前台切换、焦点变化、鼠标点击、按键等系统事件触发检测，没有任何定时轮询探测。
- **中 / 英 + 条件大写**：中文红底「中」、英文蓝底「英」；**仅当 Caps Lock 开启时**才追加「A」，小写时不渲染。
- **5 秒自动隐藏**：状态变化立即刷新并重新计时；焦点离开立即隐藏；**窗口滚动（滚轮 / 翻页 / 拖动滚动条）立即消失**。
- **清晰的多屏 / 高 DPI 表现**：Per-Monitor V2 感知，100%–300% 缩放下尺寸与位置正确，越界时自动翻转到左侧 / 下方。
- **系统托盘常驻**：启用 / 暂停、显示时长、开机自启、关于、退出；资源管理器重启后自动恢复图标。
- **极致轻量**：C++17 + 原生 Win32 + GDI+ 自绘，静态链接 CRT，单文件 exe；实测私有内存约 4 MB，空闲 CPU ≈ 0。
- **隐私优先**：不联网、不记录任何输入内容、不注入目标进程、不修改输入法、不追踪 Shift 临时大写。

## 快速开始

1. 下载 `IMETip.exe`（或从源码构建，见下文）。
2. 双击运行，托盘出现红色「中」图标，无主窗口、不出现在任务栏。
3. 点击任意输入框，光标右侧、与光标同一水平线出现提示，5 秒后自动消失。
4. 左键单击托盘图标切换启用 / 暂停，右键打开菜单。

## 托盘菜单

| 菜单项 | 说明 |
| --- | --- |
| 启用提示 / 暂停提示 | 互斥勾选；暂停时停止检测并隐藏指示窗 |
| 显示时长 | 1 / 2 / 3 / 5 / 8 / 10 秒，默认 5 秒 |
| 开机自启 | 写入 `HKCU\...\Run`，无需管理员权限 |
| 校准提示位置（点击输入框） | 对不暴露光标的自绘程序（如微信），点一下它的输入框即可指定提示位置 |
| 关于 IMETip | 版本与配置目录 |
| 退出 | 注销钩子、移除托盘图标后退出 |

## 命令行

```text
IMETip.exe [选项]

  --paused, --start-paused   启动后暂停提示
  --silent                   静默启动（开机自启用）
  --log                      写入诊断日志
  --version, -v              显示版本
  --help, -h                 显示帮助
```

## 配置

配置文件位于 `%LOCALAPPDATA%\IMETip\config.ini`，首次运行自动生成：

```ini
[indicator]
hideDelayMs=5000   ; 显示时长（1000–30000）
offsetX=32         ; 相对光标右侧的水平间距（96 DPI 逻辑像素）
debounceMs=50      ; 事件合并去抖窗口（20–200）
fadeOut=0          ; 隐藏时是否 200ms 淡出
imeChinese=1       ; 上次退出时的中英文状态（推断用）
anchorMode=1       ; 无可定位光标时使用记住的输入框位置；0 = 严格按 PRD 不显示

[startup]
startPaused=0

[diagnostics]
log=0

[anchors]
; 按窗口类记住的输入框相对位置：dx,dy,窗口宽,窗口高
Qt51514QWindowIcon=597,1071,1863,1333
```

`[anchors]` 由程序自动维护，不需要手工编辑；校准一次后该程序的提示位置就固定下来，窗口尺寸变化超过 15% 时需要重新校准。

## 构建

要求：**Visual Studio 2022/2026 Build Tools（MSVC v143+）** + **Windows SDK 10** + CMake ≥ 3.21。不依赖第三方库。

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

产物：`build/IMETip.exe`（单个文件，约 260 KB）。详见 [docs/build.md](docs/build.md)。

## 目录结构

```text
.
├─ src/
│  ├─ app/          应用装配、配置、隐藏消息窗口、托盘菜单
│  ├─ core/         事件监听、输入态探测、IME 状态、会话状态机
│  ├─ platform/     DPI、开机自启、UI Automation 工作线程
│  ├─ ui/           指示窗、渲染器、托盘图标、图标绘制
│  └─ util/         日志
├─ res/             .rc / 清单 / 多尺寸图标
├─ tests/           会话状态机与定位算法单元测试
├─ tools/           图标生成、样式预览、手动验证宿主
├─ docs/            架构、构建、兼容性、测试计划
└─ .github/         GitHub Actions 构建流水线
```

## 架构与实现要点

| 关注点 | 方案 |
| --- | --- |
| 输入态检测 | `GetGUIThreadInfo` 标准光标优先；Chrome / Windows Terminal 等回退 UI Automation 文本光标 |
| 无光标程序 | 按窗口类记住输入框相对位置（微信等），托盘可校准；点击输入框附近或会话列表才算重新进入 |
| 中英文判断 | `ImmGetConversionStatus` + `IME_CMODE_NATIVE` 语言掩码，不硬编码具体输入法返回值 |
| 大小写 | 仅 `GetKeyState(VK_CAPITAL)`，不处理 Shift 临时大写 |
| 事件源 | `SetWinEventHook`（前台 / 焦点）+ 低级鼠标 / 键盘钩子，全部合并去抖 50 ms |
| UIA 超时 | 独立工作线程 + `IUIAutomation2` 事务超时 50 ms，UI 线程最多等待 70 ms |
| 指示窗 | `WS_EX_LAYERED｜TRANSPARENT｜TOPMOST｜TOOLWINDOW｜NOACTIVATE` + `UpdateLayeredWindow` 逐像素 alpha |
| 渲染 | GDI+ 圆角渐变 + 字体回退，随 DPI 缩放 |
| 托盘 | `Shell_NotifyIcon` + `NOTIFYICON_VERSION_4`，退出时 `NIM_DELETE` |

更多细节见 [docs/architecture.md](docs/architecture.md)。

## 兼容性

- Windows 10 1903+ / Windows 11，x64。
- 记事本、Chrome、Edge、Windows Terminal、Office 等标准控件优先走标准光标，失败时回退 UIA。
- 微信等自绘输入框：首次双击输入框（或托盘菜单校准）后记住位置，之后切换聊天即可正常提示。
- 微软拼音、搜狗拼音、QQ 拼音等以语言掩码判断，不依赖私有返回值。
- 高 DPI / 多显示器 / 深色任务栏。

已知限制与降级行为见 [docs/compatibility.md](docs/compatibility.md)。

## 隐私

- 无任何网络请求。
- 不记录、不缓存、不传输输入文本或按键内容（低级钩子只判断按键类型，不保存键值序列）。
- 以普通用户权限运行，不注入目标进程、不修改输入法。

## 许可证

[MIT](LICENSE)
