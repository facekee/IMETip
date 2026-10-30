#pragma once

#include <windows.h>

#include <cstdint>
#include <string>
#include <vector>

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
    void OnTrigger(InputTrigger trigger);
    void ScheduleCheck();
    void PerformCheck(bool slowUia = false);
    void ShowIndicator(const InputSnapshot& snapshot, const IndicatorState& state);

    // 无可定位光标的窗口（微信等）：复用记住的输入框位置。
    bool ApplyAnchor(InputSnapshot& snapshot);
    void LearnAnchor(const InputSnapshot& snapshot);
    void RememberCaretlessWindow();
    bool IsCaretlessWindow(HWND window) const;
    void StartCalibration();
    void CancelCalibration();
    void CaptureCalibrationPoint();

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

    // 位置记忆相关的运行时状态
    bool clickTriggered_ = false;        // 本次探测由鼠标点击触发
    bool calibrating_ = false;           // 正在等待用户点击输入框
    std::uintptr_t clickSerial_ = 0;     // 点击序号（用于把每次点击视为新的输入会话）
    std::uintptr_t anchorSessionKey_ = 0;   // 位置记忆模式下的会话标识
    std::uintptr_t anchorSessionWindow_ = 0;
    POINT anchorPoint_{};                   // 记忆位置（屏幕坐标）
    HWND anchorWindow_ = nullptr;           // 记忆位置所属窗口
    POINT clickPoint_{};                    // 本次点击位置
    bool clickPointValid_ = false;

    // 曾经出现过「没有光标也没有 UIA 文本」的窗口类；只有这些窗口才需要记忆位置。
    std::vector<std::wstring> caretlessClasses_;

    UINT taskbarCreatedMessage_ = 0;
};

} // namespace imetip
