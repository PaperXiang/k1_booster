#pragma once

#include <cmath>

// 追逐拦截解算 (pursuer–target interception).
//
// 机器人在 P, 以名义速度 s 直线全速追; 球当前在 B, 速度 V (均 field 坐标).
// 入参用相对量: r = B - P, 即 (rx, ry); 球速 (vx, vy); 机器人名义速度 s; 最大提前时间 maxLead.
//
// 求最小 t>=0 使 |r + V t| = s t, 展开为:
//   (|V|^2 - s^2) t^2 + 2 (r·V) t + |r|^2 = 0   // a t^2 + b t + c = 0
// 取最小正根, 夹到 [0, maxLead].
//
// ok=true  表示解到有限正根 (可拦截);
// ok=false 表示无正根 (追不上 / 球在远离) -> 返回 maxLead 作为回退建议, 仍朝球前方迎截而非尾随.
// 返回值始终是夹紧到 [0, maxLead] 的提前时间.

namespace k1_ball_intercept {

inline double solveInterceptTime(double rx, double ry, double vx, double vy,
                                 double s, double maxLead, bool &ok)
{
    ok = false;
    if (maxLead <= 0.0) {
        return 0.0;
    }

    const double a = vx * vx + vy * vy - s * s; // |V|^2 - s^2
    const double b = 2.0 * (rx * vx + ry * vy); // 2 r·V
    const double c = rx * rx + ry * ry;         // |r|^2
    const double eps = 1e-6;

    double t = -1.0;
    if (std::fabs(a) < eps) {
        // 球速 ≈ 机器人速度: 退化为线性 2(r·V) t + |r|^2 = 0
        if (b < -eps) {
            t = -c / b; // 仅 b<0 (在接近) 才有正解
        }
    } else {
        const double disc = b * b - 4.0 * a * c;
        if (disc >= 0.0) {
            const double sq = std::sqrt(disc);
            const double t1 = (-b - sq) / (2.0 * a);
            const double t2 = (-b + sq) / (2.0 * a);
            double best = -1.0;
            if (t1 > eps && (best < 0.0 || t1 < best)) best = t1; // 取最小正根
            if (t2 > eps && (best < 0.0 || t2 < best)) best = t2;
            t = best;
        }
    }

    if (t > 0.0 && std::isfinite(t)) {
        ok = true;
        return t > maxLead ? maxLead : t;
    }
    // 无解 (追不上或球远离): 回退到最大提前量
    return maxLead;
}

} // namespace k1_ball_intercept
