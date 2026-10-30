# 构建说明

## 1. 环境要求

| 组件 | 版本 | 说明 |
| --- | --- | --- |
| Windows | 10 1903+ / 11 x64 | 仅支持 x64 |
| MSVC | Visual Studio 2022/2026 Build Tools，MSVC v143+ | 需要“使用 C++ 的桌面开发”工作负载 |
| Windows SDK | 10.0.19041 以上 | `uiautomationclient.h`、`shellscalingapi.h`、`gdiplus.h` |
| CMake | ≥ 3.21 | 需要 `MSVC_RUNTIME_LIBRARY` 支持 |
| Ninja | 任意较新版本 | 可选，也可用 Visual Studio 生成器 |

不需要任何第三方库：GDI+、UI Automation、Shell API 均为系统组件。

## 2. 快速构建

在 **Developer Command Prompt for VS** 中：

```bat
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

或使用预设：

```bat
cmake --preset windows-x64
cmake --build --preset windows-x64
ctest --preset windows-x64
```

Visual Studio 2022 生成器：

```bat
cmake -S . -B build-vs -G "Visual Studio 17 2022" -A x64
cmake --build build-vs --config Release
```

## 3. 产物

| 目标 | 说明 |
| --- | --- |
| `IMETip.exe` | 主程序，WIN32 子系统，静态 CRT，单文件可分发 |
| `imetip_tests.exe` | 单元测试（状态机 + 定位算法） |
| `imetip-icon.exe` | 重新生成 `res/IMETip.ico` |
| `imetip-preview.exe` | 生成文档用样式预览图 |
| `imetip-smoke-host.exe` | 手动验证宿主（带多行编辑框的窗口） |

Release 配置下 `IMETip.exe` 约 **262 KB**。链接参数 `/OPT:REF /OPT:ICF` 与 `/MT` 静态运行库确保不依赖 VC++ Redistributable。

## 4. 资源与工具

### 重新生成图标

```bat
cmake --build build --target icons
```

等价于 `imetip-icon res/IMETip.ico`。图标由 `src/ui/AppIcon.cpp` 的绘制代码生成，与运行时托盘图标共用同一套绘制逻辑，保证视觉一致。图标包含 16/20/24/32/48/64 的 DIB 条目与 128/256 的 PNG 条目。

### 生成样式预览

```bat
build\imetip-preview.exe docs\preview.png
```

### 手动验证

```bat
start build\IMETip.exe
build\imetip-smoke-host.exe
```

宿主窗口会弹出一个已聚焦的多行编辑框，8 秒后自动关闭；观察光标右侧、与光标同一水平线是否出现提示。

## 5. 构建细节

- **清单嵌入**：`res/IMETip.rc` 以 `1 RT_MANIFEST` 嵌入 `res/IMETip.manifest`，因此 CMake 中显式添加 `/MANIFEST:NO`，关闭链接器自动生成的默认清单，否则会出现 `CVT1100 资源重复`。
- **字符集**：全局 `UNICODE/_UNICODE`，RC 文件使用 `#pragma code_page(65001)` + UTF-8 保存，版本信息中的中文可正常解析。
- **GDI+ 头文件顺序**：`WIN32_LEAN_AND_MEAN` 下需先包含 `<objidl.h>`，见 `src/ui/GdiplusHeaders.h`。
- **UIA 头文件**：`UIAutomationClient.h` 依赖 `interface` 宏，需先包含 `<objbase.h>`。
- **运行时库**：`MSVC_RUNTIME_LIBRARY=MultiThreaded`，Debug 配置同样使用静态 CRT，便于单文件调试分发。

## 6. 持续集成与发布

`.github/workflows/build.yml` 在 `windows-latest` 上跑（Ninja + MSVC x64 Release）：

| 触发 | 行为 |
| --- | --- |
| 推送到 `master` / `main`，或提交 PR | 构建 → `ctest` 单元测试 → 上传 `IMETip-<版本>-x64.exe` 与其 SHA256 |
| 打 `v*` 标签 | 以上全部 + 校验 exe 版本号与标签一致 + 用 `CHANGELOG.md` 对应章节做说明 + 创建 GitHub Release |
| 手动触发（Actions 页面 Run workflow） | 同分支构建 |

发布一个新版本：

```bash
# 1. 改 src/Version.h、res/IMETip.rc、res/IMETip.manifest、CMakeLists.txt、CHANGELOG.md
git add -A && git commit -m "release: 0.1.11"
git tag v0.1.11
git push origin master --tags
```

推送标签后 Actions 会自动产出 Release，附件包含：

- `IMETip-0.1.11-x64.exe`（单文件，静态 CRT，无需运行库）
- `IMETip-0.1.11-x64.exe.sha256`

如果 `src/Version.h` 里的版本号和标签不一致，工作流会直接失败并提示，避免发布错版本。

## 7. 常见问题

| 现象 | 处理 |
| --- | --- |
| `CVT1100: 资源重复。类型: MANIFEST` | 确认 `/MANIFEST:NO` 生效（见 `CMakeLists.txt`） |
| `无法打开包括文件: gdiplus.h` 或大量语法错误 | 检查是否先包含了 `<objidl.h>` |
| `C2039: put_TransactionTimeout 不是 IUIAutomation 的成员` | 该接口属于 `IUIAutomation2`，需 `QueryInterface`（代码中已用 `ComPtr::As`） |
| 链接报 `__imp_GetDpiForMonitor` 未解析 | 需要链接 `shcore.lib` |
| 托盘图标不出现 | 检查是否被系统收纳到溢出区；`--log` 查看“托盘图标创建失败” |
