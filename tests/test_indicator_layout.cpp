#include "test_util.h"

#include "ui/IndicatorLayout.h"

void RunIndicatorLayoutTests() {
    using namespace imetip;

    IndicatorMetrics metrics; // 32px 色块、5px 间距、8px 间距、5px 阴影
    const RECT work{0, 0, 1920, 1040};

    {
        // 常规场景：光标右侧、与光标同一水平线；含阴影后窗口为 42x42
        const RECT caret{100, 100, 102, 116};
        const IndicatorPlacement placement = ComputePlacement(caret, work, metrics);
        CHECK(!placement.flippedX);
        CHECK(!placement.clampedY);
        CHECK(placement.window.left == 105); // 102 + 8 - 5
        CHECK(placement.window.top == 87);   // 100 + (16-32)/2 - 5
        CHECK(placement.window.right - placement.window.left == 42);
        CHECK(placement.window.bottom - placement.window.top == 42);

        // 垂直居中对齐：色块中心 == 光标中心
        const int blockCenter = placement.window.top + metrics.shadow + metrics.block / 2;
        const int caretCenter = caret.top + (caret.bottom - caret.top) / 2;
        CHECK(blockCenter == caretCenter);
    }

    {
        // 顶部空间不足：贴住工作区上边，不再翻到光标下方
        const RECT caret{100, 4, 102, 20};
        const IndicatorPlacement placement = ComputePlacement(caret, work, metrics);
        CHECK(placement.clampedY);
        CHECK(placement.window.top == work.top);
        CHECK(placement.window.bottom <= work.bottom);
    }

    {
        // 底部空间不足：贴住工作区下边
        const RECT caret{100, 1020, 102, 1036};
        const IndicatorPlacement placement = ComputePlacement(caret, work, metrics);
        CHECK(placement.clampedY);
        CHECK(placement.window.bottom == work.bottom);
    }

    {
        // 右侧空间不足：翻转到光标左侧，垂直仍然对齐光标中线
        const RECT caret{1900, 500, 1902, 516};
        const IndicatorPlacement placement = ComputePlacement(caret, work, metrics);
        CHECK(placement.flippedX);
        CHECK(placement.window.right <= work.right);
        CHECK(placement.window.left >= work.left);
        CHECK(placement.window.top == 487); // 500 + (16-32)/2 - 5
    }

    {
        // 双色块（中英文 + A）宽度自适应，垂直对齐不变
        metrics.count = 2;
        const RECT caret{100, 100, 102, 116};
        const IndicatorPlacement placement = ComputePlacement(caret, work, metrics);
        CHECK(placement.window.right - placement.window.left == 32 * 2 + 5 + 10);
        CHECK(placement.window.top == 87);
        metrics.count = 1;
    }

    {
        // 副屏（负坐标）工作区
        const RECT secondary{-1920, 0, 0, 1080};
        const RECT caret{-1800, 100, -1798, 116};
        const IndicatorPlacement placement = ComputePlacement(caret, secondary, metrics);
        CHECK(!placement.flippedX);
        CHECK(placement.window.left >= secondary.left);
        CHECK(placement.window.right <= secondary.right);
        CHECK(placement.window.top >= secondary.top);
        CHECK(placement.window.bottom <= secondary.bottom);
    }

    {
        // 极小工作区：翻转 + 收敛后仍完整可见
        const RECT tiny{0, 0, 120, 120};
        const RECT caret{60, 60, 62, 76};
        metrics.count = 2;
        const IndicatorPlacement placement = ComputePlacement(caret, tiny, metrics);
        CHECK(placement.window.left >= tiny.left);
        CHECK(placement.window.right <= tiny.right);
        CHECK(placement.window.top >= tiny.top);
        CHECK(placement.window.bottom <= tiny.bottom);
        metrics.count = 1;
    }

    {
        // 光标高度为 0 时按 1px 处理，不产生负尺寸，且垂直居中对齐
        const RECT caret{500, 500, 501, 500};
        const IndicatorPlacement placement = ComputePlacement(caret, work, metrics);
        CHECK(placement.window.right > placement.window.left);
        CHECK(placement.window.bottom > placement.window.top);
        const int blockCenter = placement.window.top + metrics.shadow + metrics.block / 2;
        // 光标高度为奇数时允许 1px 的取整误差
        const int diff = blockCenter > caret.top ? blockCenter - caret.top : caret.top - blockCenter;
        CHECK(diff <= 1);
    }
}
