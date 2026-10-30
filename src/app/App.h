#pragma once

#include <windows.h>

#include <string>

#include "app/Config.h"
#include "core/EventMonitor.h"
#include "core/InputProbe.h"
#include "core/SessionLogic.h"
#include "ui/IndicatorWindow.h"
#include "ui/TrayIcon.h"

namespace imetip {

// 应用主体：装配事件监听、输入态探测、指示窗、托盘，并在隐藏消息窗口上串起全部逻辑。
class App {
public:
    static int Run(HINSTANCE instance);

private:
    bool Initialize();
    int RunLoop();
    void Shutdown();
    void RequestExit();

    LRESULT HandleMessage(HWND window, UINT message, WPARAM wParam, LPARAM lParam);
    static LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam);

    void ParseCommandLine(bool& startPaused, bool& silent, bool& logEnabled, bool& showVersion, bool& showHelp);
    void OnTrigger(InputTrigger trigger, LPARAM payload = 0);
    void ScheduleCheck();
    void PerformCheck(bool slowUia = false);
    void ShowIndicator(const InputSnapshot& snapshot, const IndicatorState& state);
    bool CanUseStickyAnchor(const InputSnapshot& snapshot) const;
    void SetPaused(bool paused);
    void HandleTrayMessage(UINT event, int x, int y);
    void ShowTrayMenu(POINT point);
    void HandleTrayCommand(UINT command);
    void UpdateTrayVisuals();
    void ShowAbout();
    void OnExplorerRestarted();

    HINSTANCE instance_ = nullptr;
    HWND window_ = nullptr;
    ULONG_PTR gdiplusToken_ = 0;
    bool comInitialized_ = false;
    HANDLE instanceMutex_ = nullptr;

    Config config_;
    EventMonitor events_;
    InputProbe probe_;
    SessionLogic session_;
    IndicatorWindow indicator_;
    TrayIcon tray_;

    HICON iconActive_ = nullptr;
    HICON iconPaused_ = nullptr;

    bool paused_ = false;
    bool silent_ = false;
    bool exiting_ = false;
    bool initialized_ = false; // 初始化完成（用于退出时安全落盘配置）
    int retryCount_ = 0;       // 本次点击/焦点已做过的一次性复查次数
    RECT lastCaret_{};         // 最近一次检测到的光标矩形（用于判断“点击远离输入区”）
    bool lastCaretValid_ = false;
    HWND lastCaretWindow_ = nullptr; // 最近一次光标所在窗口
    ULONGLONG lastCaretTick_ = 0;    // 最近一次检测到光标的时间
    HWND stickyWindow_ = nullptr;    // 当前正沿用缓存锚点的窗口
    bool farClick_ = false;          // 本次点击是否远离光标
    UINT taskbarCreatedMessage_ = 0;
};

} // namespace imetip
