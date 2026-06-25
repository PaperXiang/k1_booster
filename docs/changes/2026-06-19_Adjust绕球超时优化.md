# 变更报告: Adjust 防卡死超时（借鉴八一队 demo）

- 日期 / 作者：2026-06-19 / 与 PaperXiang 结对（夜间自主，待审核）
- 类型：行为修改（前锋决策）
- 默认是否生效：**否**。`adjust_timeout_secs` 默认 `0.0` = 关闭，**行为与现在完全一致**。
- 开关与回退：在 `config.yaml` 把 `strategy.adjust_timeout_secs` 设 `>0`（如 `3.0`）开启；设回 `0.0` 关闭。

## 1. 动机
八一队 demo 列出的实战问题之一是"绕球微调时死锁"，他们加了 `adjust_timeout_secs`：`adjust` 持续太久就回退 `chase`。我们的 `StrikerDecide` 同样存在"球在侧前方→一直 `adjust` 绕球却不前进"的潜在死锁（尤其角度迟迟对不准时）。这是会直接丢球权的实战风险，且修法很小。

## 2. 改了什么（文件 + 一句话）
| 文件 | 改动 |
|---|---|
| `src/brain/config/config.yaml` | `strategy.adjust_timeout_secs: 0.0`（默认关） |
| `src/brain/src/brain.cpp` | `declare_parameter("strategy.adjust_timeout_secs", 0.0)` |
| `src/brain/include/brain_tree.h` | `StrikerDecide` 加成员 `_adjustSince` / `_adjustActive` |
| `src/brain/src/brain_tree.cpp` | `StrikerDecide::tick()`：`adjust` 连续超过阈值→强制 `chase`，并重置计时 |

## 3. 逻辑（before → after）
- before：决策为 `adjust` 时一直 `adjust`，无超时。
- after：当 `adjust_timeout_secs>0` 且决策为 `adjust`：首次进入记时刻；连续超过该秒数→本帧改为 `chase`（退后重新接近再尝试对位），并清零计时下次重计。决策一旦不是 `adjust`（踢到/追到/找球等）即清零。**阈值=0 时整段逻辑短路，等于没加。**

## 4. 风险
- 默认关 → 零风险（未启用时代码路径只多一次 `get_parameter` + 一个 `if`，决策不变）。
- 启用后（中等）：阈值太小会让机器人"对位没耐心"、频繁 chase↔adjust 抖动；太大则防卡死失效。建议从 `3.0~4.0s` 起调。
- 计时基于 `brain->get_clock()`，与节点其它计时同源；`_adjustSince` 仅在置位后才参与相减，避免跨时钟相减问题。

## 5. 如何验证
- 离线：`colcon build --packages-select brain`（动了 C++）；起 `game.xml` 确认不崩；`adjust_timeout_secs=0` 时行为应与之前**逐帧一致**。
- 赛场：设 `3.0` 开启。制造"球在侧前方对不准"场景，观察机器人**不再无限绕球**，超时后退回追球重新接近。WebUI Behavior 卡片可看 `decision` 在 `adjust`↔`chase` 的切换；日志有 `adjust timeout -> chase`。
- 不满意就设回 `0.0`。

## 6. 关联 / 来源
- 来源：八一学校 demo `hungryhenry101/Booster-K1-5v5`（README「Adjust 超时机制」）。见 `docs/changes/2026-06-19_external_sources_survey.md`。
- 代码：`StrikerDecide::tick()` `src/brain/src/brain_tree.cpp`（默认 else=`adjust` 之后）。
