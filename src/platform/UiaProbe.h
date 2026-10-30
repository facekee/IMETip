#pragma once

#include <windows.h>

#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <thread>

namespace imetip {

struct UiaCaretInfo {
    bool ok = false;
    bool editable = false;
    bool readOnly = true;
    RECT caret{};
    long controlType = 0;
};

// UI Automation 光标探测。
// 所有 UIA 调用都在独立工作线程执行，UI 线程只做带超时的等待，
// 避免目标应用卡顿导致提示工具自身无响应（PRD FR-08：UIA 查询超时 <= 50ms）。
class UiaProbe {
public:
    UiaProbe() = default;
    ~UiaProbe();

    UiaProbe(const UiaProbe&) = delete;
    UiaProbe& operator=(const UiaProbe&) = delete;

    bool Start();
    void Stop();

    // 同步等待一次查询结果；超时返回 ok=false 的空结果。
    UiaCaretInfo Query(DWORD timeoutMs);

private:
    void ThreadMain();

    std::thread thread_;
    std::mutex mutex_;
    std::condition_variable cv_;
    bool initialized_ = false;
    bool running_ = false;
    bool quit_ = false;
    bool hasRequest_ = false;
    std::uint64_t requestId_ = 0;
    std::uint64_t doneId_ = 0;
    UiaCaretInfo result_{};
};

} // namespace imetip
