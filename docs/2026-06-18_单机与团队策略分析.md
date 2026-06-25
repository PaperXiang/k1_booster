# 比赛流程中的「单机策略」与「团队策略」解析报告

> 生成日期：2026-06-18
> 适用代码：`src/brain`（K1 Booster v1.6 remake）
> 阅读对象：策略 / 算法开发者

本报告解析当前代码中**正式比赛流程**（`behavior_trees/game.xml`）下的两层策略：

- **单机策略**：单台机器人如何根据自身感知，在前锋 / 守门员两种角色下完成「找球→追球→对位→踢射」的决策闭环。
- **团队策略**：多台机器人如何通过 UDP 通讯互相协调，完成「控球权分配、角色动态切换、守门员↔前锋交接、站位分工」。

---

## 1. 总体架构

策略层运行在 `brain` ROS2 节点中，核心由三块组成：

| 模块 | 文件 | 职责 |
|------|------|------|
| 行为树（BT） | `behavior_trees/*.xml` | 描述**流程**：状态机骨架，决定"什么时候做什么" |
| 行为树节点实现 | `src/brain_tree.cpp` | 实现**动作 / 决策节点**：`StrikerDecide`、`GoalieDecide`、`Chase`、`Kick`、`Assist` 等 |
| 大脑主循环 + 数据 | `src/brain.cpp` / `brain_data.h` | `tick()` 主循环、感知融合、**团队协同 `handleCooperation()`**、成本计算 |
| 团队通讯 | `src/brain_communication.cpp` / `team_communication_msg.h` | UDP 发现 + 单播，收发队友状态 |

行为树与 C++ 代码通过 **Blackboard（黑板）键值**交互，关键键包括：

- `control_state`（1=手柄手动 / 2=重定位 / 3=自动比赛）
- `player_role`（`striker` / `goal_keeper`）
- `gc_game_state`（`INITIAL`/`READY`/`SET`/`PLAY`/`END`）、`gc_game_sub_state_type`（`NONE`/`FREE_KICK`/`TIMEOUT`）
- `decision`（单机决策结果：`find`/`chase`/`adjust`/`kick`/`assist`/`auto_visual_kick`…）
- `is_lead`（团队：我是否控球主力）、`wait_for_opponent_kickoff`、`goalie_mode`（`attack`/`guard`）

---

## 2. 比赛流程总览（`game.xml`）

`game.xml` 顶部注释即点明：**「正式比赛专用，严格执行规则，无法人工接管」**。其主树是一个大 `Sequence`，按 `control_state` 分三层：

```
RunOnce: control_state = 3            // 默认进入自动模式

control_state == 1  →  手动接管（assist_chase / assist_kick / go_manual）
control_state == 2  →  Pickup 重定位（走到入场点，LT+A 重新校准，LT+B 继续）
control_state == 3  →  自动比赛（核心）
```

### 自动比赛（`control_state == 3`）内部状态机

先 `AutoGetUpAndLocate`（自动起身 + 定位），随后按裁判机（GameController）状态分流：

| GameController 状态 | 行为 |
|---------------------|------|
| `gc_is_under_penalty`（罚时/替补） | 扫场定位 → 找球 → `SelfLocateEnterField`（在场边入场位站好） |
| `sub_state == TIMEOUT`（暂停） | 停车 + 持续定位 |
| `INITIAL` | 场外入场位站好 + 定位 |
| `READY` | `GoToReadyPosition` 走到场内起始位 + `Locate` |
| `SET` | 站定等待，持续找球 + 定位 |
| **`PLAY`** | **按 `player_role` 进入 `StrikerPlay` 或 `GoalKeeperPlay`** ← 单机策略核心 |
| `END` | 停车 |
| `FREE_KICK`（任意球，`STOP`/`GET_READY`/`SET` 三阶段） | `GET_READY` 阶段进入 `StrikerFreekick` / `GoalKeeperFreekick` |

> 关键：**比赛流程框架（game.xml）对前锋/守门员是统一的**，二者的差异完全收敛在 `PLAY` 状态下根据 `player_role` 选择的子树里。而 `player_role` 本身由**团队策略**动态决定（见第 4 节）。

---

## 3. 单机策略

「单机策略」= 一台机器人在 `PLAY` 状态下、给定角色后，如何独立决策与执行。核心是两个**决策节点**输出 `decision` 字符串，行为树再按 `decision` 选择对应动作节点。

### 3.1 前锋单机策略（`subtree_striker_play.xml` + `StrikerDecide`）

**子树流程**（`subtree_striker_play.xml`）：

```
SelfLocate(trust_direction) → Locate(若 decision!=find)
├─ wait_for_opponent_kickoff 为真 → 仅找球+站定（等对方开球，规则要求不能先碰球）
└─ 正常比赛
   ├─ ball_out 为真 → 找球 + GoBackInField（回场内）
   └─ ball_out 为假
      └─ CamFindAndTrackBall → CalcKickDir → StrikerDecide → 按 decision 执行：
         find  → FindBall（找球子树）
         assist→ Assist（团队接应站位，见 4.5）
         chase → Chase（追球，vx≤0.9）
         auto_visual_kick → RLVisionKick（强化学习视觉踢球）
         adjust→ Adjust（绕球微调到踢球方向）
         kick  → Kick（射门，speed≤0.9）
         cross → Kick（传中，speed≤0.6）
```

**`StrikerDecide::tick()` 决策优先级**（`brain_tree.cpp:947`）——自上而下短路：

| 优先级 | decision | 触发条件 |
|--------|----------|----------|
| 1 | `find` | 自己**和**队友都不知道球的位置（`!ball_location_known && !tm_ball_pos_reliable`） |
| 2 | `auto_visual_kick` | 开关开 **且 我是控球主力(`tmImLead`) 且 cost 排名第 0** 且球未出界、未丢球、cost<7、球距在 [0.2,4.0]、朝向合适、球与自身都在有效场区 |
| 3 | `assist` | **我不是控球主力（`!tmImLead`）** → 让位接应（团队耦合点） |
| 4 | `chase` | 球距 > `chase_threshold`（带 0.9 滞回，避免抖动） |
| 5 | `kick`/`cross`/`safe_shoot` | 对球角度合适或已转到踢球方向，且无需避障、看得到球、球在可踢范围（`yaw<KICK_THETA_RANGE`、`range<KICK_RANGE`） |
| 6 | `adjust` | 其余情况：绕球调整到 `kickDir` |

要点：
- **`kickDir`（踢球方向）由 `CalcKickDir`/`calcKickDir()` 计算**（`brain.cpp:1333`）：优先直接射向球门可见角度区间，被门柱挡住时取最近的门柱边缘角。
- 第 5 档里，`kickType=="cross"` 出 `cross`（传中），否则按 `threatLevel` 与 `shoot.threat_threshold`(默认 -2.0) 区分 `safe_shoot` / `kick`。由于阈值为 -2.0 而威胁等级通常 ≥ -2.0，**实际几乎总是走 `kick`**；注意子树中并未给 `safe_shoot` 配动作节点，属于潜在的策略遗留分支。
- 决策 2、3 直接依赖团队状态（`tmImLead`、`tmMyCostRank`），是单机/团队的**主要耦合点**。

### 3.2 守门员单机策略（`subtree_goal_keeper_play.xml` + `GoalieDecide`）

守门员有两种 `goalie_mode`（默认 `attack`，由扑救节点 `Intercept` 结束后复位为 `attack`）：

- **`attack`（主动防守）**：`GoalieDecide` 输出决策 → 出击 / 回位 / 扑救
- **`guard`（守门）**：球位置未知时 `GoToReadyPosition`，已知时 `GoToGoalBlockingPosition`（贴门线封堵）

**`GoalieDecide::tick()` 决策优先级**（`brain_tree.cpp:1115`）：

| 优先级 | decision | 触发条件 | 动作 |
|--------|----------|----------|------|
| 1 | `find` | 自己和队友都不知道球 | `GoToGoalBlockingPosition`（边封门边找） |
| 2 | `retreat` | 球在**对方半场**（`ball.x>0`，带滞回） | `GoToGoalBlockingPosition` 回防 |
| 3 | `chase` | 球距 > `chase_threshold` | `Chase`（守门员追球允许 vx≤1.5，比前锋更激进） |
| 4 | `kick` | 角度合适（`dir_rb_f∈(-π/2,π/2)`） | `Kick`（speed≤1.2，大力解围） |
| 5 | `adjust` | 其余 | `Adjust` |

> 注：代码中"自动视觉扑救"（`enable_auto_visual_defend`）分支已删除，开源版守门员不含 `RLVisionKick`。

### 3.3 公共单机子流程（开球 / 任意球 / 出界）

- **等待对方开球**（`wait_for_opponent_kickoff`，`brain.cpp:1264 updateKickoffMemory`）：当处于 `SET`/`READY` 且**非己方开球方**时置真；直到「球移动超过阈值」或「超时 10s」才解除。期间机器人只找球+站定，**不主动碰球**（规则要求）。
- **任意球**（`StrikerFreekick`/`GoalKeeperFreekick`）：前锋按团队 cost 排名走到进攻/防守任意球站位（`GoToFreekickPosition`，见 4.5）；守门员回 `GoToReadyPosition` / `GoToGoalBlockingPosition`。
- **球出界**（`updateBallOut`，`brain.cpp:1470`）：综合「定位坐标越界」与「视觉边线距离」判断，带滞回放宽以防抖动；出界后前锋执行 `GoBackInField` 回到场内。

---

## 4. 团队策略

「团队策略」= 多机通过 UDP 通讯共享状态，统一计算"谁控球、谁当守门、各自站哪"。**全部协同决策集中在 `Brain::handleCooperation()`（`brain.cpp:925`）**，每个主循环 tick 调用一次。

### 4.1 通讯架构（`brain_communication.cpp`）

由 `BrainCommunication` 管理多个线程，总开关为 `enable_com`（config 中默认 True）：

| 通道 | 端口 | 频率 | 作用 |
|------|------|------|------|
| **Discovery 广播** | `20000+teamId` | 1s | 广播自身 `playerId`，让队友发现自己的 IP |
| **Discovery 接收** | 同上 | — | 维护 `_teammate_addresses`（IP→队友映射），20s 超时清除 |
| **Communication 单播** | `30000+teamId` | **100ms** | 向每个已知队友发送完整 `TeamCommunicationMsg` |
| **Communication 接收** | 同上 | — | 解析队友消息，写入 `data->tmStatus[playerId-1]` |
| **GameController 单播** | `GAMECONTROLLER_RETURN_PORT` | 1s | 向裁判机回报存活 |

**队友状态报文 `TeamCommunicationMsg`**（`team_communication_msg.h`）关键字段：

```c
int  playerRole;        // 1:striker 2:goal_keeper
bool isAlive;           // 在场且未罚时
bool isLead;            // 是否控球主力
bool ballDetected / ballLocationKnown;
double ballConfidence / ballRange / cost;   // cost ≈ 我踢到球所需秒数
Point  ballPosToField;  Pose2D robotPoseToField;
double kickDir;
int  cmdId;  int cmd;   // 协同指令（见 4.4）
```

接收端会用 `validation`(31202)、`teamId` 过滤非己方/无效包，并忽略替补（`SUBSTITUTE`）队员；收到 `cmdId` 更新时把 `cmd` 转交主循环处理。

### 4.2 成本（cost）计算 —— 团队分工的度量基础

`Brain::updateCostToKick()`（`brain.cpp:1348`）把"我接近并踢到球的难度"折算成一个**约等于秒数**的标量，叠加以下惩罚后做 0.8/0.2 平滑：

| 项 | 惩罚 |
|----|------|
| 距上次看到球的时间 | `+ secsSinceBallDet` |
| 球位置未知 | `+5` |
| 球距 | `+ ball.range` |
| 球方向上有障碍（<1.5m） | `+0.5` |
| 需要转身的角度 | `+ |ball.yaw|` |
| 会**撞到更近的队友**（横向<1m） | 每个 `+2`（避免抢同一个球） |
| 需要绕球对位的角度 | `+ |kickDir - 球方向|·1.33` |
| 摔倒 | `+15` |
| 未完成定位 | `+100`（基本退出竞争） |

这个 cost 通过单播广播给全队，是 4.3/4.5 一切分工的输入。

### 4.3 控球权（lead）与排名

在 `handleCooperation()` 中（`brain.cpp:1063`）：

- `tmMinCost` = 全队（含自己）最低 cost；
- `myCostRank` = **cost 比我低的存活队友数**（0 表示我最该上球）；
- `myStrikerIDRank` = ID 比我小的存活前锋数（用于固定站位分工）。

**控球判定**（`brain.cpp:1079`）：

```
若 (tmMinCost < ball_control_cost_threshold 且 我的cost > tmMinCost)  或  myCostRank >= 2
   → tmImLead = false（我不是主力，去接应 assist）
否则
   → tmImLead = true （我是控球主力，去 chase/kick）
```

`ball_control_cost_threshold` 默认 5.0（config）。效果：**cost 最低的 1 人控球进攻，其余前锋转入 `assist` 接应站位**，避免一拥而上。

### 4.4 角色动态切换与守门员↔前锋交接

**(a) 基于人数的角色重排**（`handleCooperation` `brain.cpp:1041` + `RoleSwitchIfNeeded` `brain_tree.cpp:3414`，两处逻辑一致）：

> 策略原则：**只有满员时才保留守门员，其余情况全员进攻。**

- 我未罚时 且 全队存活数 < 总人数 → 我强制为 `striker`；
- 我在罚时 且 其他人全部存活（存活数 == 总人数-1）→ 我（回场后）当 `goal_keeper`；
- `INITIAL` 状态 → 复位为 config 初始角色；
- `READY`/`GET_READY` 且满员 → 复位为 config 初始角色。

**(b) 守门员主动出击交接**（`brain.cpp:1096`）：当我是**存活的、且恰好成为控球主力的守门员**，且距离上次指令冷却>2s：

- 计算各队友到己方球门的距离，找出**离门最近的队友 minIndex** 与最远距离 maxDist；
- 若**我离门最远**（`myDist > maxDist`）→ 发指令 `cmd = 10 + (minIndex+1)`，请最近队友接替守门，**我自己改任 `striker` 出击**。

**(c) 指令处理**（`brain.cpp:1132`）：

- `cmd == 100`：队友宣告要控球 → 我让位（`tmImLead=false`，去 assist）；
- `cmd ∈ (10,20)`：守门员请求出击，`newGoalieId = cmd-10`，若指的是我 → 我接任 `goal_keeper`。

### 4.5 站位协同（按排名分工）

团队通过排名把多名机器人摊开到不同位置，避免重叠：

| 场景 | 节点 | 分工依据 | 站位规则（摘要） |
|------|------|----------|------------------|
| 进攻接应 | `Assist`（`brain_tree.cpp:706`） | `tmMyCostRank` | rank0/1：球后方 2m、堵在「球→己方门」连线上；rank2/3：禁区前沿；rank≥4：兜底防守位 |
| 任意球 | `GoToFreekickPosition`（`brain_tree.cpp:504`） | `tmMyCostRank` | 进攻 rank0=主罚（球后 0.7m 朝踢球方向），rank1=后插 2m，rank2/3=禁区两角；防守 rank0/1 拉开纵深，rank2/3 守角 |
| 开球就位 | `GoToReadyPosition`（`brain_tree.cpp:3258`） | `myStrikerIDRank` | 前锋 rank0=中圈前，rank1=偏 y，rank2/3=后场；守门员=小禁区中央 |
| 封门 | `GoToGoalBlockingPosition`（`brain_tree.cpp:644`） | `player_role` | 站在「球→己方球门」连线上，守门员贴门线、前锋可前压 |

> 注意：进攻/任意球用 **cost 排名**（谁更该上谁上，动态），开球就位用 **ID 排名**（固定、可预测），二者刻意区分。

### 4.6 队友球信息共享

`handleCooperation`（`brain.cpp:1000`）会在自己看不到球时，采纳**距自己足够远**（>`tm_ball_dist_threshold`=1.5m，避免和自己视野冲突）的队友报告的球位置，置 `tm_ball_pos_reliable=true`，供 `StrikerDecide`/`GoalieDecide` 的 `find` 判定与任意球站位使用。1s 无可信队友球则失效。

---

## 5. 单机 ↔ 团队 的耦合关系（一图速览）

```
                 ┌──────────── 团队层 (handleCooperation, 100ms 单播) ────────────┐
   感知/定位 ──► updateCostToKick ──► tmMyCost ──► 广播
                          │                         ▲
                          ▼                         │ 队友 cost
                 myCostRank / tmMinCost ──► tmImLead(控球?) ──► is_lead
                 myStrikerIDRank          ──► player_role(角色) ──► 站位分工
                 received cmd             ──► 守门↔前锋交接
                          │
   ┌──────────────────────┴───── 单机层 (PLAY 状态行为树) ───────────────────────┐
   │  player_role == striker → StrikerPlay → StrikerDecide                       │
   │       !tmImLead → assist；tmImLead & rank0 → chase/kick/auto_visual_kick    │
   │  player_role == goal_keeper → GoalKeeperPlay → GoalieDecide                 │
   └────────────────────────────────────────────────────────────────────────────┘
```

**一句话总结**：团队层用 *cost* 和 *人数* 算出"我现在是什么角色、要不要控球、该站哪"，把结论写进黑板（`player_role` / `is_lead` / `tmMyCostRank`）；单机层的行为树读取这些黑板值，决定本帧具体做找球、追球、接应还是踢射。

---

## 6. 关键可调参数（`config/config.yaml`）

| 参数 | 默认 | 含义 |
|------|------|------|
| `enable_com` | True | 团队通讯总开关（关闭则退化为纯单机） |
| `strategy.cooperation.enable_role_switch` | true | 是否允许按人数动态切换角色 |
| `strategy.cooperation.ball_control_cost_threshold` | 5.0 | 控球权抢占阈值（越小越容易让位） |
| `strategy.tm_ball_dist_threshold` | 1.5 | 采纳队友球位的最小距离 |
| `strategy.chase`（`StrikerDecide`/`GoalieDecide` 入参 `chase_threshold`） | 1.0 | 追球/对位切换的球距阈值 |
| `strategy.kick_range` / `kick_theta_range` | 1.0 / 0.2 | 进入踢球的距离/角度窗口 |
| `strategy.enable_auto_visual_kick` | true | 前锋强化学习视觉踢球开关 |
| `strategy.enable_auto_visual_defend` | false | 守门员视觉扑救（开源版已移除实现） |
| `game.number_of_players` | 5 | 总人数，决定"满员"判断 |

---

## 7. 观察与建议（供后续优化参考）

1. **`safe_shoot` 悬空分支**：`StrikerDecide` 可能输出 `safe_shoot`（`brain_tree.cpp:1082`），但 `subtree_striker_play.xml` 未配置对应动作节点；当前因 `threat_threshold=-2.0` 几乎不触发，建议要么补节点要么清理，避免将来调阈值时出现"无动作"。
2. **角色切换逻辑双实现**：`handleCooperation`（`brain.cpp:1041`）与 `RoleSwitchIfNeeded`（`brain_tree.cpp:3414`）实现了几乎相同的"满员才有守门员"规则，存在重复，建议统一到一处以免日后改动不同步。
3. **`ball_control_cost_threshold` 代码/配置不一致**：`handleCooperation` 内默认值写 3.0，但随后被 `get_parameter` 覆盖为 config 的 5.0（`brain.cpp:1076-1077`）；以 config 为准，注意阅读代码时不要被字面默认值误导。
4. **通讯超时分层**：地址发现超时 20s、协同存活判定超时 5s（`COM_TIMEOUT`）、队友球位超时 1s，三者层级不同，调网络相关问题时需分别确认。
5. **cost 中 `+100`（未定位）惩罚**意味着任一机器人定位失败会立刻退出控球竞争，这是合理的安全设计，但也说明**定位稳定性直接决定团队分工是否正常**，是比赛中的关键风险点。

---

*报告依据：`game.xml`、`subtree_striker_play.xml`、`subtree_goal_keeper_play.xml`、`subtree_striker_freekick.xml`、`subtree_goal_keeper_freekick.xml`、`brain.cpp`、`brain_tree.cpp`、`brain_communication.cpp`、`team_communication_msg.h`、`types.h`、`config.yaml`。*
