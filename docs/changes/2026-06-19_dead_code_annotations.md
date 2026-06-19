# 变更报告: 死代码 / 失效分支注释标注（P1-4）

- 日期 / 作者：2026-06-19 / 与 PaperXiang 结对
- 类型：文档/注释（**纯注释，零行为变化**）
- 默认是否生效：N/A（不改逻辑）
- 回退：删注释即可。

## 1. 动机
代码里有若干"已定义但当前不执行"的分支/节点。它们现在无害，但**将来调阈值/改逻辑时可能突然变成"无动作"或行为不一致**。先用注释标注清楚（删除留作单独变更），降低踩坑概率。

## 2. 标注了哪些（文件:行）
| 位置 | 失效原因（注释内容） |
|---|---|
| `brain_tree.cpp` `StrikerDecide`(~:1107) `safe_shoot` | `threatLevel()≥0` 而 `threat_threshold=-2.0` → 永不触发；且子树无 `safe_shoot` 动作节点。**若日后调高阈值会变成"无动作"**。 |
| `brain_tree.cpp:79` `REGISTER_BUILDER(RoleSwitchIfNeeded)` | 节点已注册但**未被任何行为树引用**；真正的角色切换在 `handleCooperation`。冗余。 |
| `brain_tree.cpp` `GoalieDecide`(~:1175) `enable_auto_visual_defend` | 其控制的守门视觉扑救分支体已删空、不设 `decision`；默认 false。**切勿置 true**（会进空分支沿用上帧 decision）。 |
| `brain.cpp`(~:1223) `cmd == 100` | 接收逻辑在，但**无任何发送方**；lead/assist 实际只由 cost 排名驱动。已定义未触发的协议路径。 |
| `subtree_goal_keeper_play.xml`(guard 支) | （此前变更已注释）`goalie_mode` 恒为 attack → guard 分支不可达。 |

## 3. 风险
- 零：全部是注释。起树、行为、构建都不变。

## 4. 如何验证
- 离线：`colcon build` 通过（注释不影响）；`game.xml` 起树行为与之前逐帧一致。

## 5. 后续（建议单独变更）
- 真正删除 `safe_shoot` 分支（或补动作节点）、`RoleSwitchIfNeeded` 节点、`enable_auto_visual_defend` 空分支 —— 各自一次变更 + 报告，便于回退与审阅。

## 6. 关联
- 来源：`docs/单机与团队策略解析_代码核对_2026-06-18.md` §7/§8。
