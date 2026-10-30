#include "app/App.h"

#include "ui/GdiplusHeaders.h"
#include <shellapi.h>
#include <windowsx.h>

#include <algorithm>
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
constexpr UINT_PTR kTimerRetry = 3;

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
    probe_.SetInitialChinese(config_.imeChinese);
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
    initialized_ = true;
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

    // 记住退出时的中英文状态，供下次启动初始化推断值（TSF 应用读不到真实状态）
    if (initialized_) {
        config_.imeChinese = probe_.LastKnownChinese();
        config_.Save();
    }

    session_.Reset();
    events_.Stop();

    if (window_) {
        ::KillTimer(window_, kTimerDebounce);
        ::KillTimer(window_, kTimerHide);
        ::KillTimer(window_, kTimerRetry);
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
        OnTrigger(static_cast<InputTrigger>(wParam), lParam);
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
        } else if (wParam == kTimerRetry) {
            // 点击/焦点之后的一次性复查（应对目标应用异步创建光标、UIA 提供程序首次加载）
            ::KillTimer(window, kTimerRetry);
            PerformCheck(true);
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

void App::OnTrigger(InputTrigger trigger, LPARAM payload) {
    if (paused_ || exiting_) return;

    if (trigger == InputTrigger::Scroll) {
        // 窗口滚动时指示窗立即消失；会话保留，后续状态变化仍可重新显示。
        if (session_.OnExternalHide().action == IndicatorAction::Hide) {
            ::KillTimer(window_, kTimerHide);
            ::KillTimer(window_, kTimerRetry);
            indicator_.Hide(false);
        }
        return;
    }

    if (trigger == InputTrigger::ImeToggle) {
        // Shift / Ctrl+Space：TSF 应用读不到输入法状态，按切换动作翻转本地状态后立即刷新。
        if (!session_.SessionActive()) return;
        probe_.ToggleImeState();
        ScheduleCheck();
        return;
    }

    if (trigger == InputTrigger::Click && session_.SessionActive() && lastCaretValid_) {
        // 单窗口应用（如微信）切换会话时焦点 HWND 不变，会话会被判为“同一次输入”。
        // 点击位置远离当前光标（例如在左侧会话列表里切换聊天）时视为开启新的输入会话，
        // 这样切换聊天后仍会重新提示一次；点击光标附近仍遵循“不重复弹出”。
        const int x = GET_X_LPARAM(payload);
        const int y = GET_Y_LPARAM(payload);
        constexpr int kFarClickDistance = 250;
        if (IsClickFarFromCaret(lastCaret_, x, y, kFarClickDistance)) {
            farClick_ = true;
            session_.EndSession(); // 不隐藏窗口，随后探测会按新会话重新显示
        }
    }

    if (trigger != InputTrigger::Key) {
        retryCount_ = 0;
    }
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

bool App::CanUseStickyAnchor(const InputSnapshot& snapshot) const {
    // 仅用于“窗口本身不提供任何光标信息”的应用（如微信 Qt 输入框）：
    // 沿用同窗口上次检测到的光标位置，避免切换会话后完全无法提示。
    if (!config_.stickyAnchor || !lastCaretValid_ || snapshot.uiaTextAvailable) return false;
    if (snapshot.focusWindow != lastCaretWindow_) return false;
    if (stickyWindow_ != snapshot.focusWindow && !farClick_) return false;
    if (::GetTickCount64() - lastCaretTick_ > 5 * 60 * 1000ULL) return false;

    RECT windowRect{};
    if (!::GetWindowRect(lastCaretWindow_, &windowRect)) return false;
    const POINT center{(lastCaret_.left + lastCaret_.right) / 2, (lastCaret_.top + lastCaret_.bottom) / 2};
    return ::PtInRect(&windowRect, center) != FALSE;
}

void App::PerformCheck(bool slowUia) {
    if (paused_ || exiting_) return;

    InputSnapshot snapshot = probe_.Probe(window_, slowUia);

    if (snapshot.inputActive) {
        lastCaret_ = snapshot.caret;
        lastCaretValid_ = true;
        lastCaretWindow_ = snapshot.focusWindow;
        lastCaretTick_ = ::GetTickCount64();
        stickyWindow_ = nullptr;
    } else if (CanUseStickyAnchor(snapshot)) {
        // 该窗口没有可用光标（微信切换会话后 Qt 会销毁光标），沿用上次位置
        snapshot.inputActive = true;
        snapshot.caret = lastCaret_;
        snapshot.sessionKey = reinterpret_cast<std::uintptr_t>(lastCaretWindow_);
        stickyWindow_ = lastCaretWindow_;
        log::Write(L"窗口未提供光标，沿用上次光标位置 (%ld,%ld)", lastCaret_.left, lastCaret_.top);
    }
    farClick_ = false;

    const IndicatorState state{snapshot.chinese, snapshot.capsLock};

    if (!snapshot.inputActive) {
        session_.EndSession();
        ::KillTimer(window_, kTimerHide);
        if (indicator_.IsVisible()) {
            indicator_.Hide(config_.fadeOut);
        }

        // 部分应用（如微信 Qt 输入框）在点击后才异步创建光标，UIA 提供程序首次加载也较慢，
        // 这里追加两次一次性复查：200ms 与 600ms 各一次。
        if (retryCount_ < 2) {
            ++retryCount_;
            ::SetTimer(window_, kTimerRetry, retryCount_ == 1 ? 200 : 600, nullptr);
        }
        return;
    }

    const SessionDecision decision = session_.OnProbe(true, state, snapshot.sessionKey);
    switch (decision.action) {
    case IndicatorAction::Show:
    case IndicatorAction::Refresh:
        ShowIndicator(snapshot, state);
        ::SetTimer(window_, kTimerHide, static_cast<UINT>(config_.hideDelayMs), nullptr);
        log::Write(L"显示指示窗：%s%s%s%s", state.chinese ? L"中" : L"英", state.capsLock ? L" + A" : L"",
                   snapshot.viaUia ? L"（UIA 光标）" : L"", snapshot.imeInferred ? L"（状态推断）" : L"");
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
            ::KillTimer(window_, kTimerRetry);
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
