#include "test_util.h"

void RunSessionLogicTests();
void RunIndicatorLayoutTests();
void RunWindowAnchorTests();
void RunUiaGeometryTests();

int main() {
    std::printf("IMETip 单元测试\n");

    std::printf("[1/4] 会话状态机\n");
    RunSessionLogicTests();

    std::printf("[2/4] 指示窗定位\n");
    RunIndicatorLayoutTests();

    std::printf("[3/4] 输入框位置记忆\n");
    RunWindowAnchorTests();

    std::printf("[4/4] UIA 光标几何\n");
    RunUiaGeometryTests();

    if (imetip::test::Failures() == 0) {
        std::printf("全部通过\n");
        return 0;
    }
    std::printf("失败 %d 项\n", imetip::test::Failures());
    return 1;
}
