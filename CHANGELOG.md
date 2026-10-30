# 更新日志

本项目遵循 [语义化版本](https://semver.org/lang/zh-CN/)。

## [0.1.0] - 2026-10-30

首个可运行版本，覆盖 PRD v1.0 的全部 P0 需求。

### 新增

- 后台常驻 + 系统托盘图标（启用/暂停、显示时长、开机自启、关于、退出）。
- 输入态检测：`GetGUIThreadInfo` 标准光标优先，Chrome / Windows Terminal 等回退 UI Automation 文本光标。
- 事件驱动链路：`SetWinEventHook`（前台切换、焦点）+ 低级鼠标 / 键盘钩子，50 ms 合并去抖，无定时轮询。
- 状态采集：IME 转换模式 + 语言掩码判断中英文；Caps Lock 判断大小写。
- 指示窗：GDI+ 自绘圆角渐变、逐像素 alpha 分层窗口、置顶、点击穿透、不抢焦点、不出现在任务栏 / Alt+Tab。
- 显示规则：进入输入态显示一次；状态变化刷新并重置 5 秒；隐藏后状态变化重新弹出；同一会话无变化不重复弹出。
- 大写条件显示：仅 Caps Lock 开启时追加「A」。
- 多显示器与 100%–300% DPI：Per-Monitor V2，越界自动翻转并在工作区内收敛。
- 可选 200 ms 淡出、命令行参数（`--paused` / `--silent` / `--log` / `--version`）、本地 INI 配置。
- 资源管理器重启后自动恢复托盘图标；注销 / 关机时移除托盘图标。
- 单元测试：会话状态机、指示窗定位算法。
- 构建工具：`imetip-icon`（生成多尺寸 ICO）、`imetip-preview`（样式预览图）、`imetip-smoke-host`（手动验证宿主）。

### 已知限制

- 仅区分中文与非中文，日文 / 韩文输入法按英文显示。
- 浏览器可编辑区域依赖 UIA 的 `IsReadOnly` 属性，个别网页编辑器可能误判。
- 高权限窗口（管理员程序）在普通权限下可能无法读取光标信息。

[0.1.0]: https://github.com/imetip/imetip/releases/tag/v0.1.0
