#pragma once
/**
 * @file formation.h
 * @brief 站位规划 (方案任务4): 纯函数槽位框架, 不依赖 ROS / Brain, 便于独立单测.
 *
 * 设计要点 (详见 docs/2026-07-10_方案落地_实施计划.md §5):
 *  - 每个场景给出按重要性排序的槽位表, 存活 field player 数不足时从表尾截断,
 *    一套代码天然覆盖 5v5 -> 3v3 -> 罚下缩编;
 *  - 坐标全部由 FieldDimensions 参数化 (适配 adult/kid/robo_league);
 *  - 分配决定性: 输入 (GC 存活集合 + 通信位姿) 全队同源, 各机独立计算得到一致结果;
 *  - y 约定: 场地坐标系 x 指向对方球门, y 向左; 示意图"上方"= y>0, "最下方最靠右"= y<0 且 x 靠前.
 */

#include <string>
#include <vector>

#include "types.h"

// 一个阵型槽位
struct FormationSlot {
    std::string name; // 槽位语义名: passer/shooter/kicker/receiver/blocker/cover/midfield/W1a...
    Pose2D pose;      // field 系目标位姿
};

// 场景输入 (全部为值拷贝, 无外部依赖)
struct FormationInput {
    std::string scene;               // kickoff_attack | kickoff_defense | freekick_attack | freekick_defense
    FieldDimensions fd;
    Point2D ballPos{0.0, 0.0};       // field 系球位
    bool ballValid = false;
    std::vector<int> playerIds;      // 参与站位的 field player id (升序, 不含守门员, 含本机)
    std::vector<Pose2D> playerPoses; // 与 playerIds 一一对应的当前位姿 (贪心就近分配用)
    int selfId = 0;
    double keepAwayDist = 1.5;       // 任意球摆位阶段距球合规距离 (方案: 全员 >= 1.5m)
    bool hasNearestOpponent = false; // blocker 卡位参考; 无对手时退化为朝本方球门方向卡位
    Point2D nearestOpponent{0.0, 0.0};
};

// 分配结果 (本机)
struct FormationResult {
    bool valid = false;
    std::string slotName;
    Pose2D target;
    bool passTargetValid = false; // 进攻场景: 开球第一脚小传球目标点
    Point2D passTarget{0.0, 0.0};
};

class FormationPlanner {
public:
    // 生成场景槽位表: 已按人数截断, 已做合法性夹紧 (场内/禁区/中圈/距球限制)
    static std::vector<FormationSlot> slotsForScene(const FormationInput &in);

    // 槽位分配并返回本机结果. assignByDistance=true 用贪心就近(0.2m 量化 + 小 id 决胜),
    // false 则按 id 升序对号 (与 docs/固定开球优化提示词.md §4 的 ID 升序规则一致).
    static FormationResult assign(const FormationInput &in, bool assignByDistance);
};
