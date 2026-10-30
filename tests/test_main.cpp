#include "test_util.h"

void RunSessionLogicTests();
void RunIndicatorLayoutTests();

int main() {
    std::printf("IMETip 单元测试\n");

    std::printf("[1/2] 会话状态机\n");
    RunSessionLogicTests();

    std::printf("[2/2] 指示窗定位\n");
    RunIndicatorLayoutTests();

    if (imetip::test::Failures() == 0) {
        std::printf("全部通过\n");
        return 0;
    }
    std::printf("失败 %d 项\n", imetip::test::Failures());
    return 1;
}
