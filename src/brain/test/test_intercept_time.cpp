// 独立单元测试: solveInterceptTime (追逐拦截解算), 无 ROS / Eigen 依赖.
//
// 手动编译运行 (不接入 colcon, 避免构建风险):
//   g++ -std=c++17 src/brain/test/test_intercept_time.cpp -o /tmp/test_intercept && /tmp/test_intercept
// 预期输出: "ALL INTERCEPT TESTS PASSED" 且退出码 0.

#include <cassert>
#include <cmath>
#include <cstdio>

#include "../include/utils/intercept.h"

static bool approx(double a, double b, double tol = 1e-2) {
    return std::fabs(a - b) <= tol;
}

int main() {
    bool ok = false;
    double t = 0.0;

    // 1) 正面来球: 球在前方 2m, 以 1 m/s 朝机器人来, 机器人 1.2 m/s -> 约 0.91s 相遇.
    t = solveInterceptTime(2.0, 0.0, -1.0, 0.0, 1.2, 2.0, ok);
    assert(ok && approx(t, 0.909));

    // 2) 横穿球: 球在前方 1m, 以 1 m/s 横向 (+y) 走, 机器人 1.5 m/s -> 约 0.894s.
    t = solveInterceptTime(1.0, 0.0, 0.0, 1.0, 1.5, 3.0, ok);
    assert(ok && approx(t, 0.894));

    // 3) 球远离且比机器人快: 追不上 -> ok=false, 回退到 maxLead.
    t = solveInterceptTime(1.0, 0.0, 2.0, 0.0, 1.2, 1.5, ok);
    assert(!ok && approx(t, 1.5));

    // 4) 球速≈机器人速度且在接近 (线性分支): 球前方 2m 以 1.2 朝来 -> dist/s 的几何, 约 0.833s.
    t = solveInterceptTime(2.0, 0.0, -1.2, 0.0, 1.2, 5.0, ok);
    assert(ok && approx(t, 0.833));

    // 5) 解超过 maxLead 时应被夹紧, 且 ok=true (可拦截只是较远).
    t = solveInterceptTime(1.0, 0.0, 0.0, 1.0, 1.5, 0.5, ok);
    assert(ok && approx(t, 0.5));

    // 6) maxLead<=0 -> 返回 0.
    t = solveInterceptTime(1.0, 0.0, 0.0, 1.0, 1.5, 0.0, ok);
    assert(approx(t, 0.0));

    std::printf("ALL INTERCEPT TESTS PASSED\n");
    return 0;
}
