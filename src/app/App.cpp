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

#include <cstdlib>

namespace imetip {
namespace {

constexpr wchar_t kWindowClass[] = L"IMETip.MessageWindow";
constexpr wchar_t kMutexName[] = L"Local\\IMETip.SingleInstance.9A1C42E7";

constexpr UINT kMsgInputCheck = WM_APP + 17;
constexpr UINT kMsgTray = WM_APP + 18;

constexpr UINT_PTR kTimerDebounce = 1;
constexpr UINT_PTR kTimerHide = 2;
constexpr UINT_PTR kTimerRetry = 3;
constexpr UINT_PTR kTimerCalibrateArm = 4;     // 托盘菜单关闭后再进入等待点击状态
constexpr UINT_PTR kTimerCalibrateTimeout = 5; // 等待点击超时

constexpr UINT kCmdEnable = 1001;
constexpr UINT kCmdPause = 1002;
constexpr UINT kCmdDurationBase = 1100; // + 秒数
constexpr UINT kCmdAutostart = 1201;
constexpr UINT kCmdCalibrate = 1251;
constexpr UINT kCmdAbout = 1301;
constexpr UINT kCmdExit = 1401;

// 桌面 / 任务栏等外壳窗口即使“没有光标”也不代表需要记忆输入框位置。
bool IsShellWindowClass(const std::wstring& className) {
    if (className.empty()) return true;
    static const wchar_t* kBlocked[] = {
        L"Progman",      L"WorkerW",         L"Shell_TrayWnd",   L"Shell_SecondaryTrayWnd",
        L"NotifyIconOverflowWindow", L"SysListView32", L"SysTreeView32", L"ToolbarWindow32",
        L"TaskListThumbnailWnd",     L"Windows.UI.Core.CoreWindow",
    };
    for (const wchar_t* name : kBlocked) {
        if (_wcsicmp(className.c_str(), name) == 0) return true;
    }
    return false;
}

std::wstring RootClassName(HWND window) {
    HWND root = window ? ::GetAncestor(window, GA_ROOT) : nullptr;
    if (!root) return std::wstring();
    wchar_t className[128]{};
    if (::GetClassNameW(root, className, ARRAYSIZE(className)) == 0) return std::wstring();
    return std::wstring(className);
}

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
        ::KillTimer(window_, kTimerCalibrateArm);
        ::KillTimer(window_, kTimerCalibrateTimeout);
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
        } else if (wParam == kTimerRetry) {
            // 点击/焦点之后的一次性复查（应对目标应用异步创建光标、UIA 提供程序首次加载）
            ::KillTimer(window, kTimerRetry);
            PerformCheck(true);
        } else if (wParam == kTimerCalibrateArm) {
            ::KillTimer(window, kTimerCalibrateArm);
            calibrating_ = true;
            ::SetTimer(window, kTimerCalibrateTimeout, 8000, nullptr);
            tray_.ShowBalloon(L"IMETip 校准提示位置", L"请在 8 秒内点击目标程序的输入框");
        } else if (wParam == kTimerCalibrateTimeout) {
            ::KillTimer(window, kTimerCalibrateTimeout);
            if (calibrating_) {
                calibrating_ = false;
                tray_.ShowBalloon(L"IMETip 校准已取消", L"没有收到点击，位置未改变", NIIF_WARNING);
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

    if (trigger == InputTrigger::Click) {
        POINT point{};
        if (::GetCursorPos(&point)) {
            clickPoint_ = point;
            clickPointValid_ = true;
        }
    }

    if (trigger == InputTrigger::Click && calibrating_) {
        // 校准：把这次点击的位置记为当前程序的输入框位置，随后立即按新位置提示一次。
        CaptureCalibrationPoint();
        clickTriggered_ = true;
        retryCount_ = 0;
        ScheduleCheck();
        return;
    }

    clickTriggered_ = (trigger == InputTrigger::Click);

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

void App::PerformCheck(bool slowUia) {
    if (paused_ || exiting_) return;

    InputSnapshot snapshot = probe_.Probe(window_, slowUia);

    if (snapshot.inputActive) {
        if (!snapshot.viaUia) {
            LearnAnchor(snapshot);
        }
    } else if (!ApplyAnchor(snapshot)) {
        // 既没有光标也没有可复用的位置记录：按 PRD 降级为不显示。
        if (!snapshot.uiaTextAvailable) {
            RememberCaretlessWindow();
        }
        if (log::Enabled()) {
            HWND root = ::GetAncestor(snapshot.focusWindow, GA_ROOT);
            RECT windowRect{};
            ::GetWindowRect(root, &windowRect);
            wchar_t className[128]{};
            ::GetClassNameW(root, className, ARRAYSIZE(className));
            log::Write(L"记忆位置未命中：class=%s size=%dx%d 记录=%zu", className,
                       windowRect.right - windowRect.left, windowRect.bottom - windowRect.top, config_.anchors.size());
        }
    }

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
        clickTriggered_ = false;
        return;
    }

    std::uintptr_t sessionKey = snapshot.sessionKey;
    if (snapshot.viaAnchor) {
        // 位置记忆模式：点击输入框附近或左侧会话列表视为重新进入输入框
        // （切换聊天、切换单元格等），即使状态没变也提示一次；
        // 点消息气泡等区域不算，避免翻阅消息时反复弹出。
        const bool hasSession =
            session_.SessionActive() && anchorSessionKey_ != 0 && anchorSessionWindow_ == snapshot.sessionKey;
        bool activate = !hasSession;
        bool blockedByClick = false;
        if (clickTriggered_ && clickPointValid_ && anchorWindow_) {
            RECT windowRect{};
            if (::GetWindowRect(anchorWindow_, &windowRect)) {
                const int anchorX = anchorPoint_.x - windowRect.left;
                const int anchorY = anchorPoint_.y - windowRect.top;
                const int clickX = clickPoint_.x - windowRect.left;
                const int clickY = clickPoint_.y - windowRect.top;
                if (IsAnchorActivationPoint(anchorX, anchorY, clickX, clickY)) {
                    activate = true; // 点输入框/会话列表：重新提示一次
                } else if (!hasSession) {
                    blockedByClick = true; // 点消息等区域且当前没有会话：不提示
                }
            }
        }
        if (blockedByClick) {
            if (log::Enabled()) {
                log::Write(L"点击位置不算进入输入框：click=(%ld,%ld) anchor=(%ld,%ld)", clickPoint_.x, clickPoint_.y,
                           anchorPoint_.x, anchorPoint_.y);
            }
            clickTriggered_ = false;
            clickPointValid_ = false;
            return;
        }
        if (activate) {
            anchorSessionKey_ = ++clickSerial_;
            anchorSessionWindow_ = snapshot.sessionKey;
        }
        sessionKey = ~anchorSessionKey_;
    } else {
        anchorSessionKey_ = 0;
        anchorSessionWindow_ = 0;
    }
    clickTriggered_ = false;
    clickPointValid_ = false;

    const SessionDecision decision = session_.OnProbe(true, state, sessionKey);
    switch (decision.action) {
    case IndicatorAction::Show:
    case IndicatorAction::Refresh:
        ShowIndicator(snapshot, state);
        ::SetTimer(window_, kTimerHide, static_cast<UINT>(config_.hideDelayMs), nullptr);
        log::Write(L"显示指示窗：%s%s%s%s%s 光标=(%ld,%ld,%ld,%ld)", state.chinese ? L"中" : L"英",
                   state.capsLock ? L" + A" : L"", snapshot.viaUia ? L"（UIA 光标）" : L"",
                   snapshot.viaAnchor ? L"（记忆位置）" : L"", snapshot.imeInferred ? L"（状态推断）" : L"",
                   snapshot.caret.left, snapshot.caret.top, snapshot.caret.right, snapshot.caret.bottom);
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

bool App::ApplyAnchor(InputSnapshot& snapshot) {
    if (!config_.anchorEnabled || snapshot.uiaTextAvailable) return false;

    HWND foreground = snapshot.focusWindow ? snapshot.focusWindow : ::GetForegroundWindow();
    HWND root = ::GetAncestor(foreground, GA_ROOT);
    if (!root || root == window_) return false;

    RECT windowRect{};
    if (!::GetWindowRect(root, &windowRect)) return false;
    const int width = windowRect.right - windowRect.left;
    const int height = windowRect.bottom - windowRect.top;
    if (width <= 0 || height <= 0) return false;

    wchar_t className[128]{};
    if (::GetClassNameW(root, className, ARRAYSIZE(className)) == 0) return false;

    const WindowAnchor* anchor = FindAnchor(config_.anchors, className, width, height);
    if (!anchor) return false;

    const int x = windowRect.left + anchor->offsetX;
    const int y = windowRect.top + anchor->offsetY;
    if (x < windowRect.left || x >= windowRect.right || y < windowRect.top || y >= windowRect.bottom) return false;

    snapshot.inputActive = true;
    snapshot.viaAnchor = true;
    snapshot.caret = RECT{x, y, x + 1, y + 1};
    snapshot.sessionKey = reinterpret_cast<std::uintptr_t>(root);
    anchorPoint_ = POINT{x, y};
    anchorWindow_ = root;
    // 这类程序同样读不到输入法状态，沿用本地维护的值。
    snapshot.chinese = probe_.LastKnownChinese();
    snapshot.imeInferred = true;
    return true;
}

void App::LearnAnchor(const InputSnapshot& snapshot) {
    if (!config_.anchorEnabled || !IsCaretlessWindow(snapshot.focusWindow)) return;

    HWND root = ::GetAncestor(snapshot.focusWindow, GA_ROOT);
    if (!root) return;

    RECT windowRect{};
    if (!::GetWindowRect(root, &windowRect)) return;
    const int width = windowRect.right - windowRect.left;
    const int height = windowRect.bottom - windowRect.top;
    if (width <= 0 || height <= 0) return;

    wchar_t className[128]{};
    if (::GetClassNameW(root, className, ARRAYSIZE(className)) == 0) return;

    const int offsetX = snapshot.caret.left - windowRect.left;
    const int offsetY = snapshot.caret.top - windowRect.top;
    if (offsetX < 0 || offsetY < 0 || offsetX >= width || offsetY >= height) return;

    const WindowAnchor* existing = FindAnchor(config_.anchors, className, width, height);
    if (existing && std::abs(existing->offsetX - offsetX) <= 2 && std::abs(existing->offsetY - offsetY) <= 2) {
        return;
    }

    WindowAnchor anchor;
    anchor.windowClass = className;
    anchor.offsetX = offsetX;
    anchor.offsetY = offsetY;
    anchor.width = width;
    anchor.height = height;
    UpsertAnchor(config_.anchors, anchor);
    config_.Save(); // 立即落盘：位置记忆是低频事件，避免异常退出时丢失
    log::Write(L"记住输入框位置：%s 偏移 (%d,%d) 窗口 %dx%d", className, offsetX, offsetY, width, height);
}

void App::RememberCaretlessWindow() {
    const std::wstring className = RootClassName(::GetForegroundWindow());
    if (IsShellWindowClass(className)) return;

    for (const std::wstring& existing : caretlessClasses_) {
        if (_wcsicmp(existing.c_str(), className.c_str()) == 0) return;
    }
    constexpr size_t kMaxClasses = 8;
    if (caretlessClasses_.size() >= kMaxClasses) {
        caretlessClasses_.erase(caretlessClasses_.begin());
    }
    caretlessClasses_.push_back(className);
}

bool App::IsCaretlessWindow(HWND window) const {
    const std::wstring className = RootClassName(window);
    if (IsShellWindowClass(className)) return false;
    for (const std::wstring& existing : caretlessClasses_) {
        if (_wcsicmp(existing.c_str(), className.c_str()) == 0) return true;
    }
    return false;
}

void App::StartCalibration() {
    calibrating_ = false;
    ::KillTimer(window_, kTimerCalibrateTimeout);
    // 等托盘菜单彻底关闭再开始等待，避免把菜单点击当成校准点击。
    ::SetTimer(window_, kTimerCalibrateArm, 400, nullptr);
}

void App::CancelCalibration() {
    calibrating_ = false;
    ::KillTimer(window_, kTimerCalibrateArm);
    ::KillTimer(window_, kTimerCalibrateTimeout);
}

void App::CaptureCalibrationPoint() {
    calibrating_ = false;
    ::KillTimer(window_, kTimerCalibrateTimeout);

    POINT point{};
    if (!::GetCursorPos(&point)) return;

    HWND target = ::WindowFromPoint(point);
    HWND root = target ? ::GetAncestor(target, GA_ROOT) : nullptr;
    if (!root || root == window_) return;

    RECT windowRect{};
    if (!::GetWindowRect(root, &windowRect)) return;
    const int width = windowRect.right - windowRect.left;
    const int height = windowRect.bottom - windowRect.top;
    if (width <= 0 || height <= 0) return;

    wchar_t className[128]{};
    if (::GetClassNameW(root, className, ARRAYSIZE(className)) == 0) return;

    const int offsetX = point.x - windowRect.left;
    const int offsetY = point.y - windowRect.top;
    if (offsetX < 0 || offsetY < 0 || offsetX >= width || offsetY >= height) return;

    WindowAnchor anchor;
    anchor.windowClass = className;
    anchor.offsetX = offsetX;
    anchor.offsetY = offsetY;
    anchor.width = width;
    anchor.height = height;
    UpsertAnchor(config_.anchors, anchor);
    config_.Save();
    log::Write(L"校准输入框位置：%s 偏移 (%d,%d) 窗口 %dx%d", className, offsetX, offsetY, width, height);
    tray_.ShowBalloon(L"IMETip 校准完成", L"已记住该程序的输入框位置");
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
                       dpiValue);
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
        CancelCalibration();
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
    const int seconds[] = {1, 2, 3, 5, 8, 10};
    for (int value : seconds) {
        wchar_t label[32]{};
        ::swprintf_s(label, L"%d 秒", value);
        ::AppendMenuW(durations, MF_STRING | (config_.hideDelayMs == value * 1000 ? MF_CHECKED : 0),
                      kCmdDurationBase + static_cast<UINT>(value), label);
    }
    ::AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(durations), L"显示时长");

    ::AppendMenuW(menu, MF_STRING | (autostart::IsEnabled() ? MF_CHECKED : 0), kCmdAutostart, L"开机自启");
    ::AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    ::AppendMenuW(menu, MF_STRING, kCmdCalibrate, L"校准提示位置（点击输入框）");
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
    if (command == kCmdCalibrate) {
        StartCalibration();
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
