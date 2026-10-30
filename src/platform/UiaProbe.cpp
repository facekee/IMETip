#include "platform/UiaProbe.h"

// objbase.h 提供 interface/combaseapi 定义，UIA 头文件依赖它（WIN32_LEAN_AND_MEAN 下需显式引入）
#include <objbase.h>
#include <oleauto.h>
#include <UIAutomationClient.h>
#include <wrl/client.h>

#include <chrono>

#include "platform/UiaGeometry.h"

using Microsoft::WRL::ComPtr;

namespace imetip {
namespace {

// 从文本范围读取第一个包围矩形（左、上、宽、高）。
bool RectFromRange(IUIAutomationTextRange* range, RECT& out) {
    if (!range) return false;

    SAFEARRAY* array = nullptr;
    if (FAILED(range->GetBoundingRectangles(&array)) || !array) {
        return false;
    }

    bool found = false;
    double* values = nullptr;
    if (SUCCEEDED(::SafeArrayAccessData(array, reinterpret_cast<void**>(&values))) && values) {
        LONG lower = 0;
        LONG upper = -1;
        if (SUCCEEDED(::SafeArrayGetLBound(array, 1, &lower)) &&
            SUCCEEDED(::SafeArrayGetUBound(array, 1, &upper)) && upper >= lower + 3) {
            const double left = values[0];
            const double top = values[1];
            const double width = values[2];
            const double height = values[3];
            if (width > 0.0 || height > 0.0) {
                const double right = left + (width > 0.0 ? width : 1.0);
                const double bottom = top + (height > 0.0 ? height : 1.0);
                out.left = static_cast<LONG>(left);
                out.top = static_cast<LONG>(top);
                out.right = static_cast<LONG>(right + 0.5);
                out.bottom = static_cast<LONG>(bottom + 0.5);
                found = out.right > out.left && out.bottom > out.top;
            }
        }
        ::SafeArrayUnaccessData(array);
    }
    ::SafeArrayDestroy(array);
    return found;
}

// 光标矩形与元素矩形是否有交集。
// Chromium / Electron 在折叠选区（纯光标）时可能返回完全无关元素的矩形，
// 例如 QQ 的 ProseMirror 编辑器会把工具栏上某个 21x21 的方块当成光标。
bool CaretInsideElement(const RECT& caret, const RECT& element) {
    return IsCaretRectTrustworthy(caret, element);
}

UiaCaretInfo QueryFocusedElement(IUIAutomation* automation) {
    UiaCaretInfo info;
    if (!automation) return info;

    ComPtr<IUIAutomationElement> element;
    if (FAILED(automation->GetFocusedElement(&element)) || !element) {
        return info;
    }

    int processId = 0;
    if (SUCCEEDED(element->get_CurrentProcessId(&processId)) &&
        processId == static_cast<int>(::GetCurrentProcessId())) {
        return info;
    }

    BOOL focusable = FALSE;
    if (FAILED(element->get_CurrentIsKeyboardFocusable(&focusable)) || !focusable) {
        return info;
    }

    BOOL enabled = FALSE;
    if (FAILED(element->get_CurrentIsEnabled(&enabled)) || !enabled) {
        return info;
    }

    CONTROLTYPEID controlType = 0;
    element->get_CurrentControlType(&controlType);
    info.controlType = static_cast<long>(controlType);

    RECT elementRect{};
    element->get_CurrentBoundingRectangle(&elementRect);

    ComPtr<IUIAutomationTextPattern> textPattern;
    if (FAILED(element->GetCurrentPatternAs(UIA_TextPatternId, IID_PPV_ARGS(&textPattern))) || !textPattern) {
        return info;
    }

    ComPtr<IUIAutomationTextRangeArray> selection;
    if (FAILED(textPattern->GetSelection(&selection)) || !selection) {
        return info;
    }
    int count = 0;
    if (FAILED(selection->get_Length(&count)) || count <= 0) {
        return info;
    }

    ComPtr<IUIAutomationTextRange> range;
    if (FAILED(selection->GetElement(0, &range)) || !range) {
        return info;
    }

    // 只读文本（网页正文、日志、列表等）不算输入态。
    VARIANT attribute;
    ::VariantInit(&attribute);
    bool readOnly = false;
    if (SUCCEEDED(range->GetAttributeValue(UIA_IsReadOnlyAttributeId, &attribute)) && attribute.vt == VT_BOOL) {
        readOnly = attribute.boolVal != VARIANT_FALSE;
    }
    ::VariantClear(&attribute);
    info.readOnly = readOnly;
    if (readOnly) {
        return info;
    }

    RECT caret{};
    bool haveCaret = false;
    if (RectFromRange(range.Get(), caret)) {
        haveCaret = CaretInsideElement(caret, elementRect);
    }

    if (!haveCaret) {
        // 折叠选区（纯光标）部分应用不回传矩形，退化为按字符扩展后取首字符矩形。
        ComPtr<IUIAutomationTextRange> expanded;
        if (SUCCEEDED(range->Clone(&expanded)) && expanded &&
            SUCCEEDED(expanded->ExpandToEnclosingUnit(TextUnit_Character))) {
            RECT expandedRect{};
            if (RectFromRange(expanded.Get(), expandedRect) && CaretInsideElement(expandedRect, elementRect)) {
                caret = expandedRect;
                haveCaret = true;
            }
        }
    }

    if (!haveCaret) {
        // 仍然拿不到可信矩形：若焦点元素本身就是一个输入框，退化为它的左上角，
        // 至少保证提示窗出现在输入框里，而不是跑到别处。
        HWND foreground = ::GetForegroundWindow();
        DWORD foregroundPid = 0;
        if (foreground) {
            ::GetWindowThreadProcessId(foreground, &foregroundPid);
        }
        RECT windowRect{};
        if (foregroundPid == static_cast<DWORD>(processId) && foreground &&
            ::GetWindowRect(::GetAncestor(foreground, GA_ROOT), &windowRect) &&
            IsEditorLikeRect(elementRect, windowRect.right - windowRect.left, windowRect.bottom - windowRect.top)) {
            const int x = elementRect.left + 8;
            const int y = elementRect.top + (elementRect.bottom - elementRect.top) / 5;
            caret = RECT{x, y, x + 1, y + 2};
            haveCaret = true;
        }
    }

    if (!haveCaret) {
        return info;
    }

    info.ok = true;
    info.editable = true;
    info.caret = caret;
    return info;
}

} // namespace

UiaProbe::~UiaProbe() {
    Stop();
}

bool UiaProbe::Start() {
    std::unique_lock<std::mutex> lock(mutex_);
    if (thread_.joinable()) return true;

    quit_ = false;
    thread_ = std::thread([this] { ThreadMain(); });
    cv_.wait_for(lock, std::chrono::milliseconds(800), [this] { return initialized_; });
    return initialized_;
}

void UiaProbe::Stop() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        quit_ = true;
    }
    cv_.notify_all();
    if (thread_.joinable()) {
        thread_.join();
    }
}

UiaCaretInfo UiaProbe::Query(DWORD timeoutMs) {
    std::unique_lock<std::mutex> lock(mutex_);
    if (!initialized_ || !running_ || quit_) {
        return {};
    }

    ++requestId_;
    const std::uint64_t id = requestId_;
    hasRequest_ = true;
    cv_.notify_all();

    const bool completed = cv_.wait_for(lock, std::chrono::milliseconds(timeoutMs),
                                        [this, id] { return doneId_ >= id || quit_ || !running_; });
    if (!completed || doneId_ < id) {
        return {};
    }
    return result_;
}

void UiaProbe::ThreadMain() {
    ::CoInitializeEx(nullptr, COINIT_MULTITHREADED);

    ComPtr<IUIAutomation> automation;
    ::CoCreateInstance(__uuidof(CUIAutomation), nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&automation));
    if (automation) {
        // IUIAutomation2 提供事务/连接超时设置。
        // 事务超时放宽到 200ms：首次访问某个应用的 UIA 提供程序需要加载，50ms 会直接失败。
        // UI 线程的等待时间仍由 Query(timeoutMs) 控制（常规 70ms，复查 260ms）。
        ComPtr<IUIAutomation2> advanced;
        if (SUCCEEDED(automation.As(&advanced)) && advanced) {
            advanced->put_ConnectionTimeout(200);
            advanced->put_TransactionTimeout(200);
        }
    }

    {
        std::lock_guard<std::mutex> lock(mutex_);
        initialized_ = true;
        running_ = true;
    }
    cv_.notify_all();

    for (;;) {
        std::uint64_t requestId = 0;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            cv_.wait(lock, [this] { return quit_ || hasRequest_; });
            if (quit_) break;
            hasRequest_ = false;
            requestId = requestId_;
        }

        const UiaCaretInfo result = QueryFocusedElement(automation.Get());

        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (requestId == requestId_) {
                result_ = result;
                doneId_ = requestId;
            }
        }
        cv_.notify_all();
    }

    automation.Reset();
    {
        std::lock_guard<std::mutex> lock(mutex_);
        running_ = false;
        initialized_ = true;
    }
    cv_.notify_all();
    ::CoUninitialize();
}

} // namespace imetip
