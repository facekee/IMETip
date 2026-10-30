#include "test_util.h"

#include "platform/UiaGeometry.h"

void RunUiaGeometryTests() {
    using imetip::IsCaretRectTrustworthy;
    using imetip::IsEditorLikeRect;

    {
        // QQ（Electron / ProseMirror）：折叠光标返回的是工具栏上的 21x21 方块，
        // 落在编辑框之外，必须判为不可信。
        const RECT editor{891, 1236, 2083, 1384};
        const RECT bogus{2007, 1190, 2044, 1227};
        CHECK(!IsCaretRectTrustworthy(bogus, editor));

        const RECT real{905, 1250, 907, 1266};
        CHECK(IsCaretRectTrustworthy(real, editor));

        // 压在边界上仍算可信（部分应用的光标会略微超出元素矩形）
        const RECT onEdge{891, 1236, 893, 1252};
        CHECK(IsCaretRectTrustworthy(onEdge, editor));
    }

    {
        // 退化情况：元素或光标矩形为空
        const RECT empty{0, 0, 0, 0};
        const RECT editor{891, 1236, 2083, 1384};
        CHECK(!IsCaretRectTrustworthy(empty, editor));
        CHECK(!IsCaretRectTrustworthy(editor, empty));
        CHECK(!IsCaretRectTrustworthy(empty, empty));
    }

    {
        // 输入框矩形判定：正常输入框可用，整窗文档不可用
        const RECT window{403, 193, 2087, 1489}; // 1684 x 1296
        const RECT editor{891, 1236, 2083, 1384}; // 1192 x 148，约占 8%
        CHECK(IsEditorLikeRect(editor, window.right - window.left, window.bottom - window.top));

        const RECT wholeWindow{403, 193, 2087, 1489};
        CHECK(!IsEditorLikeRect(wholeWindow, window.right - window.left, window.bottom - window.top));

        const RECT tooSmall{100, 100, 120, 110};
        CHECK(!IsEditorLikeRect(tooSmall, 1684, 1296));
        CHECK(!IsEditorLikeRect(editor, 0, 0));
    }
}
