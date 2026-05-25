# VisionKick 优化策略

本文档基于当前 demo 源码分析和实战讨论整理，目标是在不盲目替代现有 `chase-adjust-kick` 的前提下，充分利用 `visualkick / RLVisionKick` 更快、更流畅的优势。

## 1. 现有代码依据

已确认的关键路径：

- `src/brain/src/brain.cpp`
- `src/brain/src/brain_tree.cpp`
- `src/brain/include/brain_tree.h`
- `src/brain/src/robot_client.cpp`
- `src/brain/include/team_communication_msg.h`
- `src/brain/behavior_trees/subtrees/subtree_striker_play.xml`
- `src/brain/config/config.yaml`
- `src/brain/msg/Kick.msg`

当前 demo 中，`visualkick` 主要对应以下代码实体：

- 行为树节点：`RLVisionKick`
- 决策字符串：`auto_visual_kick`
- SDK 调用：`RobotClient::RLVisionKick(bool start)`
- 底层 API：`api_id = 2038`

当前 `RobotClient::RLVisionKick(bool start)` 只发送：

```json
{"start": true}
```

因此，当前 demo 源码中未看到直接传给 `RLVisionKick` 的 `speed`、`power`、`strength`、`kick_speed` 等参数。若底层 SDK 支持更多字段，需要进一步查 SDK 文档或向 SDK 方确认。

## 2. 核心判断

`visualkick / RLVisionKick` 的优势：

- 动作连续；
- 进入后由底层视觉踢球技能自主处理；
- 比传统 `chase -> adjust -> kick` 分阶段逻辑更少等待；
- 近球处理速度更快；
- 更适合抢球、救球、快速解围和近距离射门。

`visualkick / RLVisionKick` 的风险：

- 进入后会倾向于直接处理球，可能破坏防守站位；
- 不适合无条件替代所有远距离 `chase`；
- 会削弱上层团队协作控制；
- 当前 demo 中没有看到模型输入输出、安全限幅、力度参数等细节；
- 如果防守机器人本应回到球门前，直接进入 visualkick 可能导致失位。

结论：

```text
不要让 visualkick 全局替代 chase。
应该把 visualkick 作为快速处理球技能，在抢球、救球、解围、队友点名和近门射门场景中使用。
```

## 3. 推荐新增四类 VisionKick 意图

建议不要只保留一个 `auto_visual_kick`，而是把上层策略拆成四类意图：

```text
auto_visual_kick
  普通进攻近球，沿用当前自动视觉踢球逻辑。

quick_visual_kick
  快速抢球、救球、队友点名协助，重点是快触球。

clear_visual_kick
  防守解围，重点是快速把球处理出危险区域。

shoot_visual_kick
  近门射门，允许更慢但更稳定或更远的踢球动作。
```

实现时不一定马上新增枚举，可以先复用行为树黑板里的 `decision` 字符串，例如：

```text
decision == "auto_visual_kick"
decision == "quick_visual_kick"
decision == "clear_visual_kick"
decision == "shoot_visual_kick"
```

## 4. 推荐决策优先级

整体优先级建议如下：

```text
摔倒 / 恢复 / 比赛非 PLAY
  ↓
防守强制回位
  ↓
本方危险区域 clear_visual_kick
  ↓
队友点名 quick_visual_kick
  ↓
普通 auto_visual_kick
  ↓
chase-adjust-kick
```

这样可以避免防守机器人被普通 visualkick 拉离球门前关键防守区域。

## 5. 远距离策略

球场半场约 8 米，当前行为树里 `RLVisionKick` 有：

```xml
range="6.0"
```

需要注意：这个 `range` 在当前逻辑中更像 `RLVisionKick` 运行后的退出保护距离，不等价于完整进入条件。

建议按距离分层：

```text
0.35m - 2.5m：
  普通 auto_visual_kick。

2.5m - 6.0m：
  只在 quick / clear / 队友点名 / 救球场景下进入 visualkick。

6.0m - 8.0m：
  先使用 chase 全速接近。
  到 6m 内或进入可靠处理窗口后，再切 quick_visual_kick。
```

原因：

- chase 已经全速，不应继续减速；
- 超远距离直接交给 visualkick，底层模型是否适配无法从当前代码确认；
- 超远距离直接 visualkick 更容易破坏团队站位；
- 更合理的职责分工是：`chase` 负责快速接近，`visualkick` 负责最后一段快速处理球。

## 6. 队友触发 quick_visual_kick

这是最值得优先实现的优化方向。

### 6.1 触发思路

当队友找到球，而目标机器人离队友较远、但更适合快速处理球时，可以由队友发送命令，让目标机器人切入 `quick_visual_kick`。

建议扩展 `cmd` 语义，例如：

```text
200 + player_id = 请求指定队友进入 quick_visual_kick
300 + player_id = 请求指定队友进入 clear_visual_kick
```

这是建议语义，当前源码中尚未实现，需要修改通信解析逻辑。

### 6.2 发送方条件

队友发送 quick visualkick 请求前，应满足：

```text
队友检测到球
  +
队友球位置可靠
  +
目标队友离队友较远，或者目标队友更接近球/角度更好
  +
当前需要快速处理球
  +
目标队友不是必须回防的关键防守位
```

### 6.3 接收方本地复核

目标机器人收到命令后不应无条件执行，应再次检查：

```text
cmd target 是自己
  ↓
命令没有超时
  ↓
球位置可靠，来自自己视觉或队友共享球
  ↓
球未出界
  ↓
当前没有摔倒/恢复
  ↓
当前不是必须回防站位
  ↓
允许进入 quick_visual_kick
```

这样既能利用队友的信息，又不会让机器人被错误命令带离关键位置。

## 7. 防守门控

由于 `visualkick` 会倾向于直接追到球前面，防守场景必须加门控。

建议规则：

```text
如果当前角色/状态要求回防：
  默认回到球门前防守位置。

如果球进入本方危险区域：
  允许 clear_visual_kick。

如果队友请求 quick_visual_kick：
  判断是否会破坏防守位。
  如果会破坏，拒绝。
  如果是紧急解围，允许。
```

防守机器人应有两个不同目标：

```text
平时：
  站在球门前关键位置，阻断射门路线。

危险时：
  快速处理球，执行 clear_visual_kick。
```

不要让普通 `auto_visual_kick` 长时间控制防守机器人。

## 8. power 策略

`brain.cpp` 中 `Brain::pubKickMsg()` 会发布 `/kick_ball` 消息，并设置：

```cpp
if (dist > 6.0) {
    power = 2.0;
} else {
    power = 6.0;
}
```

`src/brain/msg/Kick.msg` 中说明：

```text
float64 power # disired kick range
```

讨论中已确认：`power` 越大，动作越慢。因此它不应简单理解成“越大越快”。

当前逻辑的实际效果更接近：

```text
离对方球门远：
  power = 2.0，动作更快。

离对方球门近：
  power = 6.0，动作更慢，可能更偏向射门距离/稳定性。
```

建议改成按意图优先，而不是只按球到对方球门距离判断：

```text
quick_visual_kick:
  power = 2.0
  目标是快速碰球、快速处理。

clear_visual_kick:
  power = 2.0 - 3.0
  目标是快速解围，不追求慢速大力动作。

shoot_visual_kick:
  power = 6.0
  目标是近门射门，可以接受慢一点。

普通 auto_visual_kick:
  保留原 dist 判断，或根据测试结果微调。
```

注意：

```text
当前 demo 源码中没有看到 RLVisionKick 直接读取这个 power。
该 power 属于 /kick_ball 消息。
它能否影响 visualkick 的最后踢球动作，需要上机确认底层订阅和 SDK 行为。
```

## 9. 推荐流程图

```text
每一帧决策
  ↓
检查比赛状态 / 摔倒 / 丢球 / 出界
  ↓
是否处于防守强制回位
  ├─ 是
  │   ↓
  │ 是否球在本方危险区域
  │   ├─ 是
  │   │   ↓
  │   │ clear_visual_kick
  │   │   ↓
  │   │ 使用低 power 快速解围
  │   │
  │   └─ 否
  │       ↓
  │     回防站位
  │
  └─ 否
      ↓
    是否收到队友点名 quick_visual_kick
      ├─ 是
      │   ↓
      │ 本地复核球位置 / 命令时效 / 角色 / 出界 / 摔倒
      │   ├─ 通过
      │   │   ↓
      │   │ quick_visual_kick
      │   │   ↓
      │   │ 使用低 power 快速触球
      │   │
      │   └─ 不通过
      │       ↓
      │     继续当前策略
      │
      └─ 否
          ↓
        是否满足普通 auto_visual_kick 条件
          ├─ 是
          │   ↓
          │ auto_visual_kick
          │
          └─ 否
              ↓
            chase-adjust-kick
```

## 10. 建议代码改动点

### 10.1 通信层

修改方向：

- 扩展 `TeamCommunicationMsg::cmd` 的语义；
- 增加 quick visualkick / clear visualkick 请求；
- 记录命令来源、目标队员、命令时间；
- 接收方检查命令是否过期。

建议新增数据：

```text
tmQuickVisualKickRequested
tmClearVisualKickRequested
tmVisualKickCmdTime
tmVisualKickCmdFrom
tmVisualKickCmdTarget
```

命名仅为建议，实际实现时应结合现有 `BrainData` 命名风格。

### 10.2 决策层

在 `StrikerDecide::tick()` 中新增优先级：

```text
clear_visual_kick > quick_visual_kick > auto_visual_kick > chase
```

并增加防守门控，避免普通 visualkick 打断回防。

### 10.3 行为树

在 `subtree_striker_play.xml` 中可增加不同意图的 `RLVisionKick` 分支，例如：

```xml
<RLVisionKick _while="decision == 'quick_visual_kick'" min_msec_kick="1000" max_msec_kick="5000" range="6.0"/>
<RLVisionKick _while="decision == 'clear_visual_kick'" min_msec_kick="1000" max_msec_kick="4000" range="6.0"/>
<RLVisionKick _while="decision == 'auto_visual_kick'" min_msec_kick="1500" max_msec_kick="7000" range="6.0"/>
```

具体数值需要实测后确定。

### 10.4 踢球力度

在 `Brain::pubKickMsg()` 中，将当前只看 `dist > 6.0` 的逻辑改为优先看当前踢球意图：

```text
quick_visual_kick:
  power = 2.0

clear_visual_kick:
  power = 2.0 或 3.0

shoot_visual_kick:
  power = 6.0

default:
  保留原逻辑
```

注意：如果 `/kick_ball` 与 `RLVisionKick` 的底层逻辑没有关联，则此修改只影响订阅 `/kick_ball` 的踢球模块，不直接改变 `RLVisionKick(api_id=2038)`。

## 11. 测试方案

### 11.1 距离测试

固定球在不同距离：

```text
0.4m
0.7m
1.0m
1.4m
2.5m
4.0m
6.0m
8.0m
```

分别记录：

```text
进入 decision 的时间
第一次触球时间
是否成功踢出
是否摔倒
是否丢球
是否破坏站位
```

### 11.2 角度测试

测试球在机器人前方不同角度：

```text
0 度
左/右 20 度
左/右 30 度
左/右 45 度
```

对比：

```text
auto_visual_kick
quick_visual_kick
chase-adjust-kick
```

### 11.3 队友命令测试

测试流程：

```text
队友检测到球
  ↓
发送 quick_visual_kick 命令
  ↓
目标机器人收到命令
  ↓
本地复核通过
  ↓
进入 quick_visual_kick
```

记录：

```text
命令发送时间
命令接收时间
decision 切换时间
第一次触球时间
是否抢到球
是否破坏队形
```

### 11.4 防守回位测试

测试场景：

```text
球不在危险区：
  防守机器人应回防站位，不进入普通 visualkick。

球进入危险区：
  防守机器人允许 clear_visual_kick。

队友发送 quick_visual_kick：
  如果会破坏防守位，应拒绝。
  如果是紧急解围，应允许。
```

### 11.5 power 测试

测试不同 `power`：

```text
2.0
3.0
4.0
6.0
```

记录：

```text
动作启动时间
触球时间
球滚动距离
球速
动作稳定性
是否摔倒
```

重点验证：

```text
power 越大是否稳定变慢
quick / clear 是否应固定低 power
shoot 是否值得使用高 power
```

## 12. 最小可执行 TODO

1. 保留当前 `auto_visual_kick`，不要直接替换全部 chase。
2. 新增队友命令语义：`quick_visual_kick` / `clear_visual_kick`。
3. 在协作逻辑中解析队友命令，并保存目标队员、命令时间、球位置。
4. 在 `StrikerDecide::tick()` 中新增决策优先级：`clear_visual_kick > quick_visual_kick > auto_visual_kick > chase`。
5. 增加防守门控：非危险球时禁止普通 visualkick 拉走防守位。
6. 给 `quick_visual_kick` 和 `clear_visual_kick` 使用低 `power`，优先测试 `2.0`。
7. 给 `shoot_visual_kick` 使用高 `power`，优先测试 `6.0`。
8. 确认 `/kick_ball` 的订阅端和底层行为，判断 `power` 是否影响 visualkick 最后踢球动作。
9. 实测 2m、4m、6m、8m 四档距离，对比 `chase-adjust-kick`、`auto_visual_kick`、`quick_visual_kick`。
10. 实测防守回位场景，确认 visualkick 不会破坏球门前站位。

## 13. 最终策略总结

推荐最终架构：

```text
chase：
  负责远距离全速接近。

auto_visual_kick：
  负责普通近球快速处理。

quick_visual_kick：
  负责队友点名、快速抢球、快速救球。

clear_visual_kick：
  负责防守危险区快速解围。

shoot_visual_kick：
  负责近门射门。

防守门控：
  负责避免 visualkick 破坏站位。

队友通信：
  负责发现机会并点名触发 quick / clear。
```

一句话目标：

```text
让 chase 负责快速接近，让 visualkick 负责最后一段高效率处理球，让队友通信负责触发机会，让防守门控负责不乱位。
```
