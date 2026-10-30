#include "app/App.h"

#include "ui/GdiplusHeaders.h"
#include <shellapi.h>
#include <windowsx.h>

#include <string>

#include "Version.h"
#include "platform/Autostart.h"
#include "platform/Dpi.h"
#include "ui/AppIcon.h"
#include "util/Log.h"

namespace imetip {
namespace {

constexpr wchar_t kWindowClass[] = L"IMETip.MessageWindow";
constexpr wchar_t kMutexName[] = L"Local\\IMETip.SingleInstance.9A1C42E7";

constexpr UINT kMsgInputCheck = WM_APP + 17;
constexpr UINT kMsgTray = WM_APP + 18;

constexpr UINT_PTR kTimerDebounce = 1;
constexpr UINT_PTR kTimerHide = 2;

constexpr UINT kCmdEnable = 1001;
constexpr UINT kCmdPause = 1002;
constexpr UINT kCmdDurationBase = 1100; // + 秒数
constexpr UINT kCmdAutostart = 1201;
constexpr UINT kCmdAbout = 1301;
constexpr UINT kCmdExit = 1401;

} // namespace

int App::Run(HINSTANCE instance) {
    App app;
    app.instance_ = instance;
    if (!app.Initialize()) {
        app.Shutdown();
        return 0;
    }
    const int exitCode = app.RunLoop();
    app.Shutdown();
    return exitCode;
}

bool App::Initialize() {
    dpi::EnablePerMonitorAwareness();

    instanceMutex_ = ::CreateMutexW(nullptr, TRUE, kMutexName);
    if (instanceMutex_ && ::GetLastError() == ERROR_ALREADY_EXISTS) {
        ::CloseHandle(instanceMutex_);
        instanceMutex_ = nullptr;
        return false; // 已有实例在运行，静默退出
    }

    comInitialized_ = SUCCEEDED(::CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED));
    config_ = Config::Load();
    // 首次运行写出一份默认配置，方便用户发现可调项
    if (::GetFileAttributesW(Config::FilePath().c_str()) == INVALID_FILE_ATTRIBUTES) {
        config_.Save();
    }

    bool startPaused = config_.startPaused;
    bool logEnabled = config_.logEnabled;
    bool showVersion = false;
    bool showHelp = false;
    ParseCommandLine(startPaused, silent_, logEnabled, showVersion, showHelp);

    if (logEnabled) {
        log::SetLogFile(Config::Directory() + L"\\imetip.log");
        log::SetEnabled(true);
    }

    if (showVersion) {
        std::wstring text = L"IMETip ";
        text += IMETIP_VERSION_STRING;
        text += L"\n\nWindows 输入法状态提示工具\n";
        text += IMETIP_COPYRIGHT;
        ::MessageBoxW(nullptr, text.c_str(), L"IMETip", MB_OK | MB_ICONINFORMATION | MB_TOPMOST);
        return false;
    }
    if (showHelp) {
        const std::wstring text =
            L"用法：IMETip.exe [选项]\n\n"
            L"  --paused, --start-paused   启动后暂停提示\n"
            L"  --silent                   静默启动（用于开机自启）\n"
            L"  --log                      写入诊断日志\n"
            L"  --version, -v              显示版本\n"
            L"  --help, -h                 显示帮助\n\n"
            L"配置：%LOCALAPPDATA%\\IMETip\\config.ini";
        ::MessageBoxW(nullptr, text.c_str(), L"IMETip 帮助", MB_OK | MB_ICONINFORMATION | MB_TOPMOST);
        return false;
    }

    log::Write(L"IMETip %s 启动", IMETIP_VERSION_STRING);

    Gdiplus::GdiplusStartupInput gdiplusInput;
    if (Gdiplus::GdiplusStartup(&gdiplusToken_, &gdiplusInput, nullptr) != Gdiplus::Ok) {
        log::Write(L"GDI+ 初始化失败");
        return false;
    }

    WNDCLASSEXW windowClass{};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.lpfnWndProc = &App::WindowProc;
    windowClass.hInstance = instance_;
    windowClass.hCursor = ::LoadCursorW(nullptr, IDC_ARROW);
    windowClass.lpszClassName = kWindowClass;
    if (!::RegisterClassExW(&windowClass) && ::GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        log::Write(L"注册窗口类失败：%lu", ::GetLastError());
        return false;
    }

    window_ = ::CreateWindowExW(WS_EX_TOOLWINDOW, kWindowClass, L"IMETip", WS_POPUP, 0, 0, 0, 0, nullptr, nullptr,
                                instance_, this);
    if (!window_) {
        log::Write(L"创建消息窗口失败：%lu", ::GetLastError());
        return false;
    }

    taskbarCreatedMessage_ = ::RegisterWindowMessageW(L"TaskbarCreated");

    const UINT systemDpi = ::GetDpiForSystem();
    const int smallIconSize = ::MulDiv(16, static_cast<int>(systemDpi > 0 ? systemDpi : 96), 96);
    iconActive_ = icon::CreateAppIcon(smallIconSize > 0 ? smallIconSize : 16, false);
    iconPaused_ = icon::CreateAppIcon(smallIconSize > 0 ? smallIconSize : 16, true);

    if (!indicator_.Create(instance_)) {
        log::Write(L"指示窗创建失败");
    }

    if (!tray_.Add(window_, kMsgTray, iconActive_)) {
        log::Write(L"托盘图标创建失败");
        return false; // 没有托盘图标就无法操作，直接退出
    }

    probe_.Start();
    events_.Start(instance_, window_, kMsgInputCheck);

    if (!events_.WinEventHooksInstalled() || !events_.MouseHookInstalled() || !events_.KeyboardHookInstalled()) {
        log::Write(L"系统事件钩子注册不完整：winevent=%d mouse=%d keyboard=%d",
                   events_.WinEventHooksInstalled() ? 1 : 0, events_.MouseHookInstalled() ? 1 : 0,
                   events_.KeyboardHookInstalled() ? 1 : 0);
        if (!silent_) {
            // PRD FR-01：钩子注册失败必须提示用户，不得静默降级为轮询。
            tray_.ShowBalloon(L"IMETip 运行受限",
                              L"系统事件钩子注册失败，输入状态提示可能无法正常工作。请尝试以相同权限重新启动。",
                              NIIF_WARNING);
        }
    }

    paused_ = startPaused;
    if (paused_) {
        events_.SetEnabled(false);
    }
    UpdateTrayVisuals();
    log::Write(L"初始化完成，状态：%s", paused_ ? L"已暂停" : L"运行中");
    return true;
}

int App::RunLoop() {
    MSG message{};
    while (::GetMessageW(&message, nullptr, 0, 0) > 0) {
        ::TranslateMessage(&message);
        ::DispatchMessageW(&message);
    }
    return static_cast<int>(message.wParam);
}

void App::Shutdown() {
    if (exiting_) return;
    exiting_ = true;

    log::Write(L"IMETip 退出");

    session_.Reset();
    events_.Stop();

    if (window_) {
        ::KillTimer(window_, kTimerDebounce);
        ::KillTimer(window_, kTimerHide);
    }

    indicator_.Hide(false);
    indicator_.Destroy();
    tray_.Remove();
    probe_.Stop();

    if (iconActive_) {
        ::DestroyIcon(iconActive_);
        iconActive_ = nullptr;
    }
    if (iconPaused_) {
        ::DestroyIcon(iconPaused_);
        iconPaused_ = nullptr;
    }

    if (window_) {
        HWND window = window_;
        window_ = nullptr;
        ::DestroyWindow(window);
    }

    if (gdiplusToken_) {
        Gdiplus::GdiplusShutdown(gdiplusToken_);
        gdiplusToken_ = 0;
    }
    if (comInitialized_) {
        ::CoUninitialize();
        comInitialized_ = false;
    }
    if (instanceMutex_) {
        ::CloseHandle(instanceMutex_);
        instanceMutex_ = nullptr;
    }
}

void App::RequestExit() {
    if (exiting_) return;
    Shutdown();
    ::PostQuitMessage(0);
}

void App::ParseCommandLine(bool& startPaused, bool& silent, bool& logEnabled, bool& showVersion, bool& showHelp) {
    int argumentCount = 0;
    LPWSTR* arguments = ::CommandLineToArgvW(::GetCommandLineW(), &argumentCount);
    if (!arguments) return;

    for (int i = 1; i < argumentCount; ++i) {
        const std::wstring argument = arguments[i];
        if (argument == L"--paused" || argument == L"--start-paused") {
            startPaused = true;
        } else if (argument == L"--silent") {
            silent = true;
        } else if (argument == L"--log") {
            logEnabled = true;
        } else if (argument == L"--version" || argument == L"-v") {
            showVersion = true;
        } else if (argument == L"--help" || argument == L"-h" || argument == L"/?") {
            showHelp = true;
        }
    }
    ::LocalFree(arguments);
}

LRESULT CALLBACK App::WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    App* self = nullptr;
    if (message == WM_NCCREATE) {
        auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
        self = static_cast<App*>(create->lpCreateParams);
        self->window_ = window;
        ::SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    } else {
        self = reinterpret_cast<App*>(::GetWindowLongPtrW(window, GWLP_USERDATA));
    }

    if (self) {
        return self->HandleMessage(window, message, wParam, lParam);
    }
    return ::DefWindowProcW(window, message, wParam, lParam);
}

LRESULT App::HandleMessage(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    if (taskbarCreatedMessage_ != 0 && message == taskbarCreatedMessage_) {
        OnExplorerRestarted();
        return 0;
    }

    switch (message) {
    case kMsgInputCheck:
        OnTrigger(static_cast<InputTrigger>(wParam));
        return 0;

    case kMsgTray:
        HandleTrayMessage(LOWORD(lParam), GET_X_LPARAM(wParam), GET_Y_LPARAM(wParam));
        return 0;

    case WM_TIMER:
        if (wParam == kTimerDebounce) {
            ::KillTimer(window, kTimerDebounce);
            PerformCheck();
        } else if (wParam == kTimerHide) {
            ::KillTimer(window, kTimerHide);
            if (session_.OnHideTimeout().action == IndicatorAction::Hide) {
                indicator_.Hide(config_.fadeOut);
            }
        }
        return 0;

    case WM_QUERYENDSESSION:
        return TRUE;

    case WM_ENDSESSION:
        if (wParam) {
            tray_.Remove(); // 系统注销/关机时先移除托盘图标，避免残留
        }
        return 0;

    case WM_DESTROY:
        window_ = nullptr;
        ::PostQuitMessage(0);
        return 0;

    default:
        break;
    }
    return ::DefWindowProcW(window, message, wParam, lParam);
}

void App::OnTrigger(InputTrigger trigger) {
    if (paused_ || exiting_) return;
    // 按键事件只用于输入态会话内的状态复查，非输入态不因按键而检查。
    if (trigger == InputTrigger::Key && !session_.SessionActive()) return;
    ScheduleCheck();
}

void App::ScheduleCheck() {
    if (!window_) return;
    // 合并去抖：连续事件只在最后一次之后 50ms 触发一次探测。
    ::KillTimer(window_, kTimerDebounce);
    ::SetTimer(window_, kTimerDebounce, static_cast<UINT>(config_.debounceMs), nullptr);
}

void App::PerformCheck() {
    if (paused_ || exiting_) return;

    const InputSnapshot snapshot = probe_.Probe(window_);
    const IndicatorState state{snapshot.chinese, snapshot.capsLock};

    if (!snapshot.inputActive) {
        session_.EndSession();
        ::KillTimer(window_, kTimerHide);
        if (indicator_.IsVisible()) {
            indicator_.Hide(config_.fadeOut);
        }
        return;
    }

    const SessionDecision decision = session_.OnProbe(true, state, snapshot.sessionKey);
    switch (decision.action) {
    case IndicatorAction::Show:
    case IndicatorAction::Refresh:
        ShowIndicator(snapshot, state);
        ::SetTimer(window_, kTimerHide, static_cast<UINT>(config_.hideDelayMs), nullptr);
        log::Write(L"显示指示窗：%s%s%s", state.chinese ? L"中" : L"英", state.capsLock ? L" + A" : L"",
                   snapshot.viaUia ? L"（UIA）" : L"");
        break;

    case IndicatorAction::Hide:
        ::KillTimer(window_, kTimerHide);
        indicator_.Hide(config_.fadeOut);
        break;

    case IndicatorAction::None:
    default:
        break;
    }
}

void App::ShowIndicator(const InputSnapshot& snapshot, const IndicatorState& state) {
    HMONITOR monitor = ::MonitorFromRect(&snapshot.caret, MONITOR_DEFAULTTONEAREST);
    MONITORINFO info{};
    info.cbSize = sizeof(info);

    RECT workArea{};
    if (monitor && ::GetMonitorInfoW(monitor, &info)) {
        workArea = info.rcWork;
    } else {
        workArea = RECT{0, 0, ::GetSystemMetrics(SM_CXSCREEN), ::GetSystemMetrics(SM_CYSCREEN)};
    }

    const UINT dpiValue = dpi::ForMonitor(monitor);
    indicator_.Present(IndicatorModel{state.chinese, state.capsLock}, snapshot.caret, workArea, config_.offsetX,
                       config_.offsetY, dpiValue);
}

void App::SetPaused(bool paused) {
    if (paused_ == paused) {
        UpdateTrayVisuals();
        return;
    }
    paused_ = paused;

    if (paused_) {
        if (window_) {
            ::KillTimer(window_, kTimerHide);
            ::KillTimer(window_, kTimerDebounce);
        }
        session_.Reset();
        indicator_.Hide(false);
        events_.SetEnabled(false);
    } else {
        session_.Reset();
        events_.SetEnabled(true);
    }
    UpdateTrayVisuals();
    log::Write(paused_ ? L"提示已暂停" : L"提示已启用");
}

void App::HandleTrayMessage(UINT event, int x, int y) {
    switch (event) {
    case WM_CONTEXTMENU: {
        POINT point{x, y};
        ShowTrayMenu(point);
        break;
    }
    case NIN_SELECT:
    case NIN_KEYSELECT:
        // 左键单击托盘图标：切换启用/暂停（PRD FR-09 默认行为）
        SetPaused(!paused_);
        break;
    default:
        break;
    }
}

void App::ShowTrayMenu(POINT point) {
    HMENU menu = ::CreatePopupMenu();
    if (!menu) return;

    std::wstring header = L"IMETip ";
    header += IMETIP_VERSION_STRING;
    header += paused_ ? L"（已暂停）" : L"（运行中）";
    ::AppendMenuW(menu, MF_STRING | MF_DISABLED, 0, header.c_str());
    ::AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    ::AppendMenuW(menu, MF_STRING | (paused_ ? 0 : MF_CHECKED), kCmdEnable, L"启用提示");
    ::AppendMenuW(menu, MF_STRING | (paused_ ? MF_CHECKED : 0), kCmdPause, L"暂停提示");
    ::AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);

    HMENU durations = ::CreatePopupMenu();
    const int seconds[] = {3, 5, 8, 10};
    for (int value : seconds) {
        wchar_t label[32]{};
        ::swprintf_s(label, L"%d 秒", value);
        ::AppendMenuW(durations, MF_STRING | (config_.hideDelayMs == value * 1000 ? MF_CHECKED : 0),
                      kCmdDurationBase + static_cast<UINT>(value), label);
    }
    ::AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(durations), L"显示时长");

    ::AppendMenuW(menu, MF_STRING | (autostart::IsEnabled() ? MF_CHECKED : 0), kCmdAutostart, L"开机自启");
    ::AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    ::AppendMenuW(menu, MF_STRING, kCmdAbout, L"关于 IMETip");
    ::AppendMenuW(menu, MF_STRING, kCmdExit, L"退出");

    // 托盘菜单需要在窗口前台时才能被正确关闭。
    ::SetForegroundWindow(window_);
    const UINT command = ::TrackPopupMenuEx(menu, TPM_RIGHTBUTTON | TPM_RETURNCMD | TPM_NONOTIFY, point.x, point.y,
                                            window_, nullptr);
    ::DestroyMenu(menu);
    ::PostMessageW(window_, WM_NULL, 0, 0);

    if (command != 0) {
        HandleTrayCommand(command);
    }
}

void App::HandleTrayCommand(UINT command) {
    if (command == kCmdEnable) {
        SetPaused(false);
        return;
    }
    if (command == kCmdPause) {
        SetPaused(true);
        return;
    }
    if (command == kCmdAutostart) {
        autostart::SetEnabled(!autostart::IsEnabled());
        return;
    }
    if (command == kCmdAbout) {
        ShowAbout();
        return;
    }
    if (command == kCmdExit) {
        RequestExit();
        return;
    }
    if (command > kCmdDurationBase && command <= kCmdDurationBase + 60) {
        config_.hideDelayMs = static_cast<int>(command - kCmdDurationBase) * 1000;
        config_.Save();
        log::Write(L"显示时长调整为 %d 秒", config_.hideDelayMs / 1000);
    }
}

void App::UpdateTrayVisuals() {
    tray_.SetIcon(paused_ ? iconPaused_ : iconActive_);

    std::wstring tooltip = L"IMETip ";
    tooltip += IMETIP_VERSION_STRING;
    tooltip += paused_ ? L" · 提示已暂停" : L" · 提示已启用";
    tray_.SetTooltip(tooltip);
}

void App::ShowAbout() {
    std::wstring text = L"IMETip ";
    text += IMETIP_VERSION_STRING;
    text += L"\nWindows 输入法状态提示工具\n\n";
    text += L"仅在可编辑输入框获得焦点时，于光标右上方短暂显示当前输入法与大小写状态。\n\n";
    text += L"· 不联网、不记录任何输入内容\n";
    text += L"· 事件驱动，无定时轮询\n";
    text += L"· 配置目录：";
    text += Config::Directory();
    text += L"\n\n";
    text += IMETIP_COPYRIGHT;
    text += L"\nMIT License";

    ::MessageBoxW(window_, text.c_str(), L"关于 IMETip", MB_OK | MB_ICONINFORMATION | MB_TOPMOST);
}

void App::OnExplorerRestarted() {
    // 资源管理器重启后托盘图标会丢失，需要重新添加。
    log::Write(L"检测到 TaskbarCreated，重建托盘图标");
    tray_.Add(window_, kMsgTray, iconActive_);
    UpdateTrayVisuals();
}

} // namespace imetip
