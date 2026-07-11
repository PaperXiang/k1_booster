#include "formation.h"

#include <algorithm>
#include <climits>
#include <cmath>

#include "utils/math.h"

using namespace std;

namespace {

// 从 from 位置面向 (tx, ty) 的朝向
double faceTo(const Pose2D &from, double tx, double ty) {
    return atan2(ty - from.y, tx - from.x);
}

// 槽位合法性夹紧: 场内收边 / 防守不进本方禁区 / 对方开球不入中圈 / 任意球距球 >= keepAway
void clampSlot(const FormationInput &in, Pose2D &p) {
    const auto &fd = in.fd;

    // 场内收边 0.5m
    p.x = cap(p.x, fd.length / 2.0 - 0.5, -fd.length / 2.0 + 0.5);
    p.y = cap(p.y, fd.width / 2.0 - 0.5, -fd.width / 2.0 + 0.5);

    // 防守场景不进本方禁区 (禁区留给守门员)
    if (in.scene == "kickoff_defense" || in.scene == "freekick_defense") {
        double paFrontX = -fd.length / 2.0 + fd.penaltyAreaLength;
        if (p.x < paFrontX + 0.25 && fabs(p.y) < fd.penaltyAreaWidth / 2.0 + 0.25) {
            p.x = paFrontX + 0.25;
        }
    }

    // 对方开球: 规则要求在本方半场且不得进入中圈
    if (in.scene == "kickoff_defense") {
        if (p.x > -0.3) p.x = -0.3;
        double r = norm(p.x, p.y);
        double minR = fd.circleRadius + 0.3;
        if (r < minR) {
            if (r < 1e-3) {
                p.x = -minR;
            } else {
                p.x = p.x / r * minR;
                p.y = p.y / r * minR;
            }
            if (p.x > -0.3) p.x = -0.3;
        }
    }

    // 任意球: 摆位阶段所有槽位距球 >= keepAwayDist (方案确认: 全员含开球者)
    if ((in.scene == "freekick_attack" || in.scene == "freekick_defense") && in.ballValid) {
        double d = norm(p.x - in.ballPos.x, p.y - in.ballPos.y);
        if (d < in.keepAwayDist - 1e-6) {
            double dir = d < 1e-3
                ? atan2(-in.ballPos.y, -fd.length / 2.0 - in.ballPos.x) // 与球重合时朝本方球门方向退让
                : atan2(p.y - in.ballPos.y, p.x - in.ballPos.x);
            p.x = in.ballPos.x + in.keepAwayDist * cos(dir);
            p.y = in.ballPos.y + in.keepAwayDist * sin(dir);
        }
    }
}

} // namespace

vector<FormationSlot> FormationPlanner::slotsForScene(const FormationInput &in) {
    const auto &fd = in.fd;
    const int n = static_cast<int>(in.playerIds.size());
    const double bx = in.ballPos.x;
    const double by = in.ballPos.y;
    vector<FormationSlot> slots;

    if (in.scene == "kickoff_attack") {
        // 图2 (已确认两人链): 最上方(y+)球员开球 -> 领传给最下方最靠右(y-, x 靠前)的 shooter 主攻框.
        Pose2D shooter{-fd.circleRadius * 1.2, -2.2, 0.0};
        Point2D lead{shooter.x + 0.5, shooter.y + 0.3}; // shooter 前方一点的领传点
        Pose2D passer{-0.75, 0.4, 0.0};
        Pose2D support1{-fd.circleRadius * 1.6, 0.6, 0.0};
        Pose2D support2{-fd.length / 2.0 + fd.penaltyDist + 0.4, 0.4, 0.0}; // 深位保护 (图2 最深红圈)
        passer.theta = atan2(lead.y, lead.x); // 开球时球在中点(0,0), 面向传球方向
        shooter.theta = faceTo(shooter, 0.0, 0.0);
        support1.theta = faceTo(support1, 0.0, 0.0);
        support2.theta = faceTo(support2, 0.0, 0.0);

        // 截断: 1人=只开球; 2人(3v3)=shooter+passer; 3人=+support_1; >=4人=+support_2.
        // id 升序对号时顺序遵循固定开球规格 §4: ids[0]=shooter, ids[1]=passer.
        if (n <= 1) {
            slots = {{"passer", passer}};
        } else {
            slots = {{"shooter", shooter}, {"passer", passer}, {"support_1", support1}, {"support_2", support2}};
        }
    } else if (in.scene == "kickoff_defense") {
        // 图3: 三层队形防中路突破. L1 两人紧贴中圈站得紧, L2 两人堵漏/侧翼, GK 为第三层.
        // y 随球平移 (开球前球在中点, 平移量 0; 通用化以便球动后复用).
        double shift1 = cap(by * 0.6, 1.2, -1.2);
        double shift2 = cap(by * 0.8, 2.0, -2.0);
        double l1x = -fd.circleRadius - 0.4;
        double l2x = -fd.length / 4.0;
        Pose2D l1a{l1x, 0.55 + shift1, 0.0}, l1b{l1x, -0.55 + shift1, 0.0};
        Pose2D l2a{l2x, 1.6 + shift2, 0.0}, l2b{l2x, -1.6 + shift2, 0.0};
        Pose2D l1c{l1x, shift1, 0.0}, l2c{l2x, shift2, 0.0};
        for (Pose2D *p : {&l1a, &l1b, &l2a, &l2b, &l1c, &l2c}) p->theta = faceTo(*p, bx, by);

        if (n >= 4) slots = {{"L1a", l1a}, {"L1b", l1b}, {"L2a", l2a}, {"L2b", l2b}};
        else if (n == 3) slots = {{"L1a", l1a}, {"L1b", l1b}, {"L2c", l2c}};
        else if (n == 2) slots = {{"L1c", l1c}, {"L2c", l2c}}; // 3v3: 每层一人居中, 中圈层优先于第二层
        else slots = {{"L1c", l1c}};
    } else if (in.scene == "freekick_attack") {
        // 图4/图5: 进攻半场(C)与防守半场(D)边线球/任意球.
        bool inOppHalf = bx > 0;
        Pose2D receiver;
        if (inOppHalf) {
            // C: 接应在对方禁区线上, 靠传球线一侧
            receiver.x = fd.length / 2.0 - fd.penaltyAreaLength - 0.3;
            receiver.y = cap(by * 0.5, fd.penaltyAreaWidth / 2.0 - 0.3, -(fd.penaltyAreaWidth / 2.0 - 0.3));
            receiver.theta = faceTo(receiver, fd.length / 2.0, 0.0); // 面向对方球门
        } else {
            // D: 接应在己方罚球线, 离球远端
            receiver.x = -fd.length / 2.0 + fd.penaltyAreaLength + 0.3;
            receiver.y = (by > 0 ? -1.0 : 1.0) * (fd.penaltyAreaWidth / 2.0 - 0.5);
            receiver.theta = faceTo(receiver, bx, by);
        }
        // kicker: 球后 keepAway 处, 面向接应 (= 传球方向); 摆位即 1.5m 合规, 恢复比赛后由决策链接近开球
        double passDir = atan2(receiver.y - by, receiver.x - bx);
        Pose2D kicker{bx - in.keepAwayDist * cos(passDir), by - in.keepAwayDist * sin(passDir), passDir};
        // blocker 卡位: 球与最近对手连线上 1.6m; 无对手可见时取球向本方球门方向
        double blockDir = in.hasNearestOpponent
            ? atan2(in.nearestOpponent.y - by, in.nearestOpponent.x - bx)
            : atan2(-by, -fd.length / 2.0 - bx);
        Pose2D blocker{bx + 1.6 * cos(blockDir), by + 1.6 * sin(blockDir), 0.0};
        blocker.theta = faceTo(blocker, bx, by);
        // cover 补防: 中圈位置偏球侧, 兼传球备选
        Pose2D cover{0.0, cap(by * 0.3, 1.0, -1.0), 0.0};
        cover.theta = faceTo(cover, bx, by);
        // midfield 中场接应 (仅防守半场开球需要, 与球异侧)
        Pose2D midfield{0.0, (by > 0 ? -1.5 : 1.5), 0.0};
        midfield.theta = faceTo(midfield, bx, by);

        // 截断: 3v3(2人)=kicker+receiver (方案: 卡位/补防仅 5v5 添加)
        if (inOppHalf) slots = {{"kicker", kicker}, {"receiver", receiver}, {"blocker", blocker}, {"cover", cover}};
        else slots = {{"kicker", kicker}, {"receiver", receiver}, {"midfield", midfield}, {"blocker", blocker}};
    } else if (in.scene == "freekick_defense") {
        // 图6: 沿"球 -> 本方球门中心"轴的三层人墙, 随球动态排布, GK 为最后一层.
        double axisDir = atan2(-by, -fd.length / 2.0 - bx);
        double nx = -sin(axisDir), ny = cos(axisDir); // 轴法向
        auto wallSlot = [&](double dist, double lateral) {
            Pose2D p{bx + dist * cos(axisDir) + lateral * nx,
                     by + dist * sin(axisDir) + lateral * ny, 0.0};
            p.theta = faceTo(p, bx, by);
            return p;
        };
        Pose2D w1a = wallSlot(1.8, 0.45), w1b = wallSlot(1.8, -0.45); // 第一层人墙: 距球 1.8m(>=1.5 合规), 站得紧
        Pose2D w2a = wallSlot(3.5, 1.1), w2b = wallSlot(3.5, -1.1);   // 第二层: 展宽堵侧漏
        Pose2D w1c = wallSlot(1.8, 0.0), w2c = wallSlot(3.5, 0.0);

        if (n >= 4) slots = {{"W1a", w1a}, {"W1b", w1b}, {"W2a", w2a}, {"W2b", w2b}};
        else if (n == 3) slots = {{"W1a", w1a}, {"W1b", w1b}, {"W2c", w2c}};
        else if (n == 2) slots = {{"W1c", w1c}, {"W2c", w2c}};
        else slots = {{"W1c", w1c}};
    }

    // 人数截断 + 合法性夹紧
    if (static_cast<int>(slots.size()) > n) slots.resize(n);
    for (auto &s : slots) clampSlot(in, s.pose);
    return slots;
}

FormationResult FormationPlanner::assign(const FormationInput &in, bool assignByDistance) {
    FormationResult res;
    auto slots = slotsForScene(in);
    if (slots.empty() || in.playerIds.empty()) return res;

    const int cnt = static_cast<int>(in.playerIds.size());
    int selfIdx = -1;
    for (int i = 0; i < cnt; i++) {
        if (in.playerIds[i] == in.selfId) { selfIdx = i; break; }
    }
    if (selfIdx < 0) return res; // 本机不参与站位 (如守门员)

    int mySlot = -1;
    if (!assignByDistance || static_cast<int>(in.playerPoses.size()) != cnt) {
        // id 升序对号: ids[k] -> slots[k]. 槽位数少于人数时, 多余球员无槽 (上层回退).
        if (selfIdx < static_cast<int>(slots.size())) mySlot = selfIdx;
    } else {
        // 贪心就近: 按槽位重要性顺序, 每个槽位分给未分配者中最近的.
        // 距离量化到 0.2m + 小 id 决胜, 降低各机位姿观测差导致的分配分歧.
        vector<bool> used(cnt, false);
        for (int s = 0; s < static_cast<int>(slots.size()); s++) {
            int best = -1;
            long bestKey = LONG_MAX;
            for (int k = 0; k < cnt; k++) {
                if (used[k]) continue;
                double d = norm(in.playerPoses[k].x - slots[s].pose.x, in.playerPoses[k].y - slots[s].pose.y);
                long key = lround(d / 0.2) * 100 + in.playerIds[k];
                if (key < bestKey) { bestKey = key; best = k; }
            }
            if (best < 0) break;
            used[best] = true;
            if (best == selfIdx) { mySlot = s; break; }
        }
    }
    if (mySlot < 0) return res;

    res.valid = true;
    res.slotName = slots[mySlot].name;
    res.target = slots[mySlot].pose;

    // 进攻场景: 给出开球第一脚小传球目标 (供开球者的 CalcKickDir 在 kickoff_guard 期间使用)
    if (in.scene == "kickoff_attack") {
        bool found = false;
        for (auto &s : slots) {
            if (s.name == "shooter") {
                res.passTarget = Point2D{s.pose.x + 0.5, s.pose.y + 0.3}; // 领传: shooter 前方一点
                found = true;
            }
        }
        if (!found) res.passTarget = Point2D{2.0, -1.5}; // 单人开球: 朝前场斜向小趟
        res.passTargetValid = true;
    } else if (in.scene == "freekick_attack") {
        for (auto &s : slots) {
            if (s.name == "receiver") {
                res.passTarget = Point2D{s.pose.x, s.pose.y};
                res.passTargetValid = true;
            }
        }
    }
    return res;
}
