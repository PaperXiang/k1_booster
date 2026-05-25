# 3v3 Demo 行为树分析与测试优化方案

## 1. 相关文件定位

| 文件 | 作用 | 和带球/射门的关系 | 是否需要重点看 |
|---|---|---|---|
| `src/brain/behavior_trees/game.xml` | 正式比赛主行为树，默认 launch 入口 | 只负责比赛状态、角色分支、手动辅助入口；不直接写追球/射门细节 | 是 |
| `src/brain/behavior_trees/subtrees/subtree_striker_play.xml` | 前锋 PLAY 主逻辑 | `CamFindAndTrackBall -> CalcKickDir -> StrikerDecide -> Find/Assist/Chase/RLVisionKick/Adjust/Kick` | 是，最核心 |
| `src/brain/behavior_trees/subtrees/subtree_cam_find_and_track_ball.xml` | 摄像头找球/跟踪球子树 | 知道球或队友球可靠则 `CamTrackBall`，否则 `CamFindBall` | 是 |
| `src/brain/behavior_trees/chase.xml` | 单独追球 demo | 未被 `game.xml` 引用；用于独立追球测试 | 参考 |
| `shoot.xml` | 未找到 | 现工程没有该文件；文档里提到过历史版本 | 未找到 |
| `src/brain/config/config.yaml` | 策略、速度、协作、避障参数 | 包含近球限速、视觉踢球、踢球中断、协作 cost 参数 | 是 |
| `src/brain/src/brain_tree.cpp` | 行为树节点实现 | `Chase/Adjust/CalcKickDir/StrikerDecide/Kick/RLVisionKick` 均在这里 | 是 |
| `src/brain/src/brain.cpp` | 记忆、协作、GameController、视觉回调 | 更新球记忆、lead/cost、出界、丢球、开球等待 | 是 |

## 2. 正式比赛行为链路

`src/brain/launch/launch.py` 默认启动 `tree='game.xml'`，并把它转成 `tree_file_path` 传给 `brain_node`。

`game.xml` 主流程：

```text
启动 MainTree
-> control_state 默认设为 3 自动模式
-> 若手动/assist_chase/assist_kick，则 SimpleChase 或 Kick
-> 若自动且未被罚下
-> 按 GameController 状态分支
-> READY: 去起始位置 + 定位
-> SET: 找/跟踪球 + 停住 + 定位
-> PLAY: 根据 player_role 进入 StrikerPlay 或 GoalKeeperPlay
-> FREE_KICK: 按 STOP/GET_READY/SET 分支定位、摆位或停住
```

前锋真实进攻链路在 `src/brain/behavior_trees/subtrees/subtree_striker_play.xml`：

```text
PLAY
-> SelfLocate trust_direction
-> 非 find 时持续 Locate
-> 若等待对方开球: 找/跟踪球 + 停住
-> 若球出界: 找/跟踪球 + GoBackInField + Locate
-> 正常比赛:
   CamFindAndTrackBall
   -> CalcKickDir
   -> StrikerDecide
   -> decision=find: FindBall
   -> decision=assist: Assist
   -> decision=chase: Chase
   -> decision=auto_visual_kick: RLVisionKick
   -> decision=adjust: Adjust
   -> decision=kick/cross: Kick
```

结论：当前 demo 不是传统意义的“持续控球带球策略”。普通路径是“追球 -> 调整 -> 踢球/传中”。但存在 `RLVisionKick` 分支，它更像底层视觉带球/视觉踢球技能，由上层在条件合适时交给 SDK 控制。

## 3. 什么情况会切入 RLVisionKick

`RLVisionKick` 不是所有近球场景都会切入，而是由 `src/brain/src/brain_tree.cpp` 中的 `StrikerDecide::tick()` 把 `decision` 设置为 `auto_visual_kick` 后，在前锋子树中触发：

```xml
<RLVisionKick _while="decision == 'auto_visual_kick'" min_msec_kick="1500" max_msec_kick="7000" range="6.0"/>
```

必须同时满足以下条件：

| 条件组 | 具体条件 |
|---|---|
| 功能开关 | `strategy.enable_auto_visual_kick == true` |
| 协作身份 | 自己是 lead，即 `tmImLead == true` |
| cost 排名 | 自己是最低控球成本，即 `tmMyCostRank == 0` |
| 球状态 | `ball_out == false` 且 `lose_ball == false` |
| 控球成本 | `tmMyCost < 7.0` |
| 球距窗口 | `ballRange < strategy.auto_visual_kick_enable_dist_max` 且 `ballRange > strategy.auto_visual_kick_enable_dist_min` |
| 球偏航窗口 | `abs(ball.yawToRobot) < strategy.auto_visual_kick_enable_angle` |
| 球在机器人前方窗口 | `ball.posToRobot.x > 0.35 && ball.posToRobot.x < 2.5 && abs(ball.posToRobot.y) < 0.55` |
| 踢球方向基本对齐 | `angleGoodForKick || reachedKickDir || abs(deltaDir) < 0.35` |
| 场地区域限制 | 球和机器人都要满足代码里的半场/横向区域条件：`x > fieldLength / 2 - 14.3` 且 `abs(y) < 5.0` |

当前 `src/brain/config/config.yaml` 中与入口直接相关的配置值是：

```yaml
strategy:
  enable_auto_visual_kick: true
  auto_visual_kick_enable_dist_min: 0.35
  auto_visual_kick_enable_dist_max: 2.5
  auto_visual_kick_enable_angle: 0.55
```

直观理解：

```text
我方主攻机器人
-> 自己最适合控球
-> 球没出界、没丢
-> 球在前方 0.35m 到 2.5m 且左右偏差不大
-> 身体/人球方向已经基本朝向目标踢球方向
-> 进入 RLVisionKick
```

因此，如果机器人近球后仍然反复 `Adjust`，通常说明以下某个条件不满足：不是 lead、cost rank 不是 0、球太偏、球太近/太远、`deltaDir` 没对齐、视觉认为 `lose_ball`，或 `ball_out` 被置位。

`RLVisionKick` 退出条件在 `RLVisionKick::onRunning()` 中，主要包括：

| 退出原因 | 触发条件 |
|---|---|
| 主动退出 | `shouldExitRLVisionKick == true` |
| 球太远 | 看到球且 `ball.range > range`，并且执行时间超过 `min_msec_kick` |
| 控球成本过高 | `tmMyCost > 8.0`，并且执行时间超过 `min_msec_kick` |
| 丢球 | `lose_ball == true`，并且执行时间超过 `min_msec_kick` |
| 球出界 | `ball_out == true` |
| 超时 | 执行时间超过 `max_msec_kick` |

## 4. 核心决策逻辑

`StrikerDecide` 的优先级大致是：

1. 不知道球且无可靠队友球：`find`
2. 满足视觉踢球入口：`auto_visual_kick`
3. 自己不是 lead：`assist`
4. 球距大于 `chase_threshold=1.0`：`chase`
5. 近球、角度合格、球可见、球偏角和距离满足限制：`kick` 或 `cross`
6. 否则：`adjust`

普通 `Kick` 触发条件包括：`angleGoodForKick` 或穿过目标踢球方向、不是任意球开球阶段、未触发踢球避障、当前视觉检测到球、`abs(ball.yawToRobot) < strategy.kick_theta_range`、`ball.range < strategy.kick_range`。

注意：`strategy.kick_range`、`strategy.kick_theta_range` 在 YAML 中有，但在 `src/brain/src/brain.cpp` 的参数声明列表里未看到对应声明；后续调参前必须先做运行确认。

## 5. 参数与节点关系

### 追球 Chase

`Chase` 的行为树参数来自 `src/brain/behavior_trees/subtrees/subtree_striker_play.xml`：

| 参数 | 当前值 | 作用 | 实体测试关注点 |
|---|---:|---|---|
| `vx_limit` | `2.0` | 前向追球速度上限 | 太大时容易冲过球、撞球 |
| `vy_limit` | `0.75` | 横向切入速度上限 | 太大时近球横移可能把球蹭走 |
| `vtheta_limit` | `2.4` | 转向角速度上限 | 太大时身体晃动，太小则转不过来 |
| `dist` | `0.3` | 目标点在球后方的距离 | 太小会贴球过近，太大可能迟迟不进入踢球窗口 |
| `safe_dist` | `0.65` | 绕球回到球后方的安全半径 | 太小绕球可能碰球，太大绕行耗时 |

`Chase` 还读取配置参数：

| 配置 | 当前值 | 是否明显接入代码 | 建议 |
|---|---:|---|---|
| `strategy.limit_near_ball_speed` | `false` | 是 | 第一批低风险测试建议改为 `true` 做 A/B |
| `strategy.near_ball_speed_limit` | `0.6` | 是 | 建议从 `0.4/0.5/0.6` 三档试 |
| `strategy.near_ball_range` | `2.0` | 是，但代码实际还和 `0.75` 取较小值 | 先不要只调它，重点看是否开启限速 |
| `obstacle_avoidance.avoid_during_chase` | `true` | 是 | 多机接近球时建议保持开启 |
| `obstacle_avoidance.chase_ao_safe_dist` | `1.5` | 是 | 如果绕障过度导致不追球，再考虑降低 |

### 调整 Adjust

`Adjust` 的目标不是射门，而是绕球调整“人-球-目标方向”的关系。当前前锋配置：

| 参数 | 当前值 | 作用 | 可能问题 |
|---|---:|---|---|
| `turn_threshold` | `0.5` | 需要调整方向的阈值 | 太大可能不够精细，太小容易反复调整 |
| `range` | `0.4` | 调整时希望保持的球距 | 太近容易踢前碰球，太远踢不到 |
| `vx_limit` | `0.7` | 径向移动速度上限 | 太大近球不稳 |
| `vy_limit` | `0.6` | 切向移动速度上限 | 太大绕球时易蹭球 |
| `vtheta_limit` | `1.5` | 转向上限 | 太大晃，太小对准慢 |
| `tangential_speed_far` | `0.7` | 远离理想方向时绕球速度 | 反复绕球时优先降这个 |
| `tangential_speed_near` | `0.15` | 接近理想方向时微调速度 | 太小可能微调慢 |
| `near_threshold` | `0.8` | 切换远/近切向速度的判断 | 影响绕球末段稳定性 |
| `no_turn_threshold` | `0.1` | 球偏航很小时不转身 | 太大可能身体不对球 |
| `turn_first_threshold` | `0.5` | 球偏太大时先停走转向 | 太小会停走频繁，太大可能边走边晃 |

### 普通 Kick

普通 `Kick` 不是一个独立射门规划器，而是按当前球相对方向调用 `crabWalk` 推/趟球。进入 `Kick` 之前，`StrikerDecide` 已经要求近球、角度和可见性满足条件。

| 参数 | 当前值 | 作用 | 风险 |
|---|---:|---|---|
| `speed_limit` | `0.9` | 踢/推球速度 | 太大容易擦球或失稳 |
| `min_msec_kick` | `1000` | 最短执行时间 | 太短球没动，太长球动后仍继续走 |
| `msecs_stablize` | `1000` | 稳定时间，仅在 stable kick 开启时有意义 | 当前 `enable_stable_kick` 默认未真正加载到 config |
| `strategy.abort_kick_when_ball_moved` | `true` | 球明显移动/丢失后中断 | 建议保持开启 |
| `strategy.abort_kick_ball_move_threshold` | `0.3` | 判断球已移动的阈值 | 需要确认参数声明后再调 |

## 6. RLVisionKick 排查表

如果实体测试中 `RLVisionKick` 没有进入，按下面顺序查日志和现场现象：

| 现象 | 最可能原因 | 先看什么 |
|---|---|---|
| 近球后一直 `adjust` | `visualKickAligned` 不满足，或者球在侧前方窗口外 | `debug/striker_decide` 中 `visualKickDelta`、`visualKickBallWindow` |
| 明明最近但不进 `RLVisionKick` | 不是 lead 或 `tmMyCostRank != 0` | `tree/Decide` 的“主控”，`debug/updateCostToKick` |
| 单机能进，多机不进 | 协作 cost 或队友通信让本机转 assist | `tmMyCostRank`、`tmImLead`、队友 cost |
| 远距离直接进视觉踢球 | `auto_visual_kick_enable_dist_max` 太大或 ball range 估计偏小 | 球距日志和视觉深度 |
| 侧方球也进入 | `auto_visual_kick_enable_angle` 或 `abs(ballY)<0.55` 窗口偏宽 | 球偏角、`ball.posToRobot.y` |
| 刚进就退出 | `ball_out`、`lose_ball`、`tmMyCost>8` 或超时 | `debug/RLVisionKick` 退出原因 |
| 进入后踢不中 | 入口过早、球太偏、底层视觉踢球需要更小速度/更稳姿态 | 进入瞬间的球距、偏航、机器人是否还在高速移动 |

建议第一轮不直接改底层 `RLVisionKick`，只用入口条件和前置追球/调整稳定性来控制它。实体机器人上，进入底层技能前的速度状态很关键：如果 `Chase` 还很快，虽然条件满足，也可能因为身体未稳导致视觉踢球效果差。

## 7. 最可能暴露的问题

- 近球限速当前配置为 `limit_near_ball_speed: false`，`Chase` 虽然有近球限速代码，但不会生效；实体上容易冲过球或把球撞偏。
- `RLVisionKick` 入口较复杂，可能过早进入导致踢不中，也可能入口太窄导致近球反复 `Adjust`。
- 普通 `Kick` 是按球方向 `crabWalk`，更像推/趟球，不是完整射门动作。
- `Adjust` 是绕球切向调整，若球距、侧向速度和转向阈值不匹配，实体上可能绕不出角度或反复振荡。
- 协作 lead 依赖 `tmMyCost` 和队友通信；通信延迟、定位不准、cost 平滑滞后会导致主攻频繁切换或多人抢球。
- `ball_memory_timeout=3.0s`，视觉短暂丢球时仍会相信记忆球位；实体遮挡/误检时可能朝旧球位追。

## 8. 分阶段测试计划

### 第 0 阶段：不改参数，只建立基线

目标：确认真实短板属于“看不见、追不稳、绕不好、踢不准、协作乱”中的哪一类。

| 测试 | 场景 | 记录 |
|---|---|---|
| 单机找球 | 球在正前、左前、右前、短暂遮挡 | 重新看到球用时、是否原地转过头 |
| 单机追球 | 球距 0.8m、1.2m、2.0m | 是否冲过球、是否接近后减速 |
| 单机调整 | 球在左右 30/45 度 | 是否进入 `Adjust`，是否绕到球后 |
| 单机踢球 | 球距 0.4m、0.7m、1.0m | 是否进入 `Kick` 或 `RLVisionKick`，是否踢中 |
| 双机协作 | 两台前锋同时看到同一球 | 是否只有一台 lead |
| 三机小局 | 前锋、辅助、守门完整运行 | 是否抢球、互挡、守门离位 |

### 第 1 阶段：低风险参数优化

每次只改一个参数组，每组至少 10 次测试。

| 问题 | 优先调整 | 推荐顺序 |
|---|---|---|
| 近球冲过球 | `strategy.limit_near_ball_speed`、`near_ball_speed_limit` | 先开 `limit_near_ball_speed=true`，再试 `0.6 -> 0.5 -> 0.4` |
| 追球太猛 | `Chase vx_limit` | 从 `2.0` 降到 `1.5` 或 `1.2` 做对比 |
| 侧向蹭球 | `Chase vy_limit`、`Adjust tangential_speed_far` | 先降 `Adjust tangential_speed_far`，再动 `vy_limit` |
| 近球反复绕 | `turn_first_threshold`、`tangential_speed_far` | 先降绕球速度，再考虑阈值 |
| 普通踢球空走 | `kick_range`、`kick_theta_range` | 先确认参数是否生效，再收紧窗口 |
| 视觉踢球过早 | `auto_visual_kick_enable_dist_min/max/angle` | 优先收紧 angle，再收紧距离 |

### 第 2 阶段：中风险行为树优化方向

这里先作为路线，不在第一阶段直接做：

- 给 `Adjust` 增加超时或局部最优判断，但进入 `RLVisionKick` 前仍保留球距、球偏角、lead 条件。
- 把 `RLVisionKick` 入口窗口从硬编码局部变量进一步参数化，便于现场调参。
- 给 lead 切换增加滞回，避免 cost 轻微波动导致主攻频繁切换。
- 把“正式前锋测试”和“追球 demo 测试”分离，不用 `chase.xml` 代表正式比赛表现。

### 第 3 阶段：高风险后期优化方向

- 增加真正“控球/带球”状态：近球后低速保持球在前方窗口，而不是只靠 `RLVisionKick` 或普通 `Kick`。
- 按场区切策略：后场优先清球，中场控球推进，前场收紧射门角度。
- 重构 `StrikerDecide`，把关键硬编码阈值整理成可配置参数，并补全 ROS2 参数声明。
- 优化 team cost 模型，把定位可信度、队友位置、路径遮挡、当前速度状态都纳入控球成本。

## 9. 实地照做清单

1. 单机静止球基线：正前方 1.5m，连续 10 次，目标是稳定进入 `chase`，近球不冲过。
2. 单机侧前方球：左/右 30 度、45 度各 10 次，记录 `Adjust` 是否能把球调整到可踢窗口。
3. 单机近球射门：球距 0.4m、0.7m、1.0m 各 10 次，记录普通 `Kick` 是否踢中和是否提前中断。
4. `RLVisionKick` 专项：球距 0.8m、1.2m、2.0m 各 10 次，记录进入率、踢中率、退出原因。
5. 丢球专项：带球/视觉踢球中人为遮挡 1s、2s、3s，观察是否继续走旧球位、是否退出。
6. 出界专项：球放边线附近 0.3m、0.6m、1.0m，观察 `ball_out` 是否误触发。
7. 双机协作：两台前锋同时开，固定球位，观察 `tmMyCostRank` 和 `tmImLead` 是否只有一台主攻。
8. 三机 3v3：加入守门员/辅助位，记录是否多人抢球、是否辅助位过深、是否挡住主攻路线。
9. 每轮只改一个参数，至少跑 10 次，记录成功率、失败类型、对应日志截图。
10. 先调低风险参数，确认稳定后再改行为树；不要同时改视觉、定位、追球和踢球参数。

## 10. 现场记录模板

| 字段 | 填写示例 |
|---|---|
| 日期/场地 | 2026-xx-xx，半场/全场 |
| 机器人 | P1 striker / P2 striker / P3 goal_keeper |
| 行为树 | `game.xml` |
| 参数改动 | 无，或 `limit_near_ball_speed=true` |
| 场景 | 正前方静止球 1.5m |
| 决策链路 | `chase -> adjust -> RLVisionKick` |
| 是否进入 RLVisionKick | 是/否，原因 |
| 是否踢中 | 是/否 |
| 失败类型 | 看不见、冲过、绕不出、踢偏、协作抢球、摔倒 |
| 下一轮只改一个点 | 例如降低 `near_ball_speed_limit` 到 `0.5` |

