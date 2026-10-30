#include "test_util.h"

#include "core/WindowAnchor.h"

void RunWindowAnchorTests() {
    using imetip::FindAnchor;
    using imetip::IsAnchorSizeCompatible;
    using imetip::UpsertAnchor;
    using imetip::WindowAnchor;

    {
        // 尺寸一致性：15% 内视为同一布局，超出则不复用
        WindowAnchor anchor;
        anchor.windowClass = L"Qt51514QWindowIcon";
        anchor.width = 1863;
        anchor.height = 1333;
        CHECK(IsAnchorSizeCompatible(anchor, 1863, 1333));
        CHECK(IsAnchorSizeCompatible(anchor, 1830, 1310));
        CHECK(!IsAnchorSizeCompatible(anchor, 1200, 1333));
        CHECK(!IsAnchorSizeCompatible(anchor, 1863, 800));
        CHECK(!IsAnchorSizeCompatible(anchor, 0, 0));
    }

    {
        // 按类名查找：命中相近尺寸，忽略大小写
        std::vector<WindowAnchor> anchors;
        UpsertAnchor(anchors, WindowAnchor{L"Qt51514QWindowIcon", 585, 1071, 1863, 1333});
        UpsertAnchor(anchors, WindowAnchor{L"Notepad", 40, 40, 900, 700});

        const WindowAnchor* hit = FindAnchor(anchors, L"qt51514qwindowicon", 1860, 1330);
        CHECK(hit != nullptr);
        CHECK(hit->offsetX == 585);
        CHECK(hit->offsetY == 1071);

        CHECK(FindAnchor(anchors, L"qt51514qwindowicon", 1200, 800) == nullptr);
        CHECK(FindAnchor(anchors, L"Unknown", 1863, 1333) == nullptr);
        CHECK(FindAnchor(anchors, L"", 1863, 1333) == nullptr);
    }

    {
        // 同一个窗口类只保留一条记录，更新而不是追加
        std::vector<WindowAnchor> anchors;
        UpsertAnchor(anchors, WindowAnchor{L"Demo", 10, 10, 800, 600});
        UpsertAnchor(anchors, WindowAnchor{L"Demo", 20, 30, 800, 600});
        CHECK(anchors.size() == 1);
        CHECK(anchors[0].offsetX == 20);
        CHECK(anchors[0].offsetY == 30);
    }

    {
        // 条数上限：超出后丢弃最早的记录
        std::vector<WindowAnchor> anchors;
        for (int i = 0; i < 20; ++i) {
            UpsertAnchor(anchors, WindowAnchor{L"Class" + std::to_wstring(i), i, i, 800, 600});
        }
        CHECK(anchors.size() == 12);
        CHECK(anchors.back().windowClass == L"Class19");
        CHECK(FindAnchor(anchors, L"Class0", 800, 600) == nullptr);
    }

    {
        // 点击是否算“进入输入框”：微信 1863x1333 窗口，输入框文字起点 (597,1071)
        using imetip::IsAnchorActivationPoint;
        const int anchorX = 597;
        const int anchorY = 1071;

        CHECK(IsAnchorActivationPoint(anchorX, anchorY, 600, 1080));  // 输入框内
        CHECK(IsAnchorActivationPoint(anchorX, anchorY, 1200, 1150)); // 输入框右侧
        CHECK(IsAnchorActivationPoint(anchorX, anchorY, 250, 300));   // 左侧会话列表
        CHECK(!IsAnchorActivationPoint(anchorX, anchorY, 1200, 400)); // 消息气泡
        CHECK(!IsAnchorActivationPoint(anchorX, anchorY, 900, 200));  // 聊天标题栏
    }
}
