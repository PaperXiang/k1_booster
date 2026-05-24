# 视觉大脚踢球参数改动报告

## 修改目标

把 `auto_visual_kick` 作为真正的大脚踢球技能使用，减少“进入技能但踢不到/很快退出”的情况。

## 改动文件

- `src/brain/config/config.yaml`
- `src/brain/behavior_trees/shoot.xml`
- `src/brain/behavior_trees/subtrees/subtree_striker_play.xml`
- `src/brain/src/brain_tree.cpp`

## 参数变化

- `strategy.kick_range`: `1.0` -> `0.85`
- `strategy.kick_theta_range`: `0.2` -> `0.28`
- `strategy.auto_visual_kick_enable_dist_min`: `0.2` -> `0.35`
- `strategy.auto_visual_kick_enable_dist_max`: `4.0` -> `2.8`
- `strategy.auto_visual_kick_enable_angle`: `0.8` -> `0.45`
- `RLVisionKick min_msec_kick`: `1500` -> `3000`
- `RLVisionKick max_msec_kick`: `7000` -> `9000`
- `RLVisionKick range`: `6.0` -> `4.0`

## 关键逻辑变化

- 视觉大脚触发角度收窄，要求球更靠近机器人正前方，避免底层视觉踢球技能在偏球状态下启动。
- 视觉大脚触发距离收窄，避免球太远时提前进入底层技能，也避免球太近时看不全球。
- `RLVisionKick` 最短执行时间延长，避免刚完成启动/低头扫描就因为退出条件离开。
- `RLVisionKick` 控球成本退出阈值从 `8.0` 放宽到 `12.0`，减少对方半场准备大脚时过早退出。
- 修复 `RLVisionKick` 中文日志字符串破损问题，避免编译时出现字符串未闭合。

## 内容审查结论

- 现在 `auto_visual_kick` 仍保持开启。
- `shoot.xml` 和正式前锋子树 `subtree_striker_play.xml` 都使用同一组更长的视觉大脚执行时间。
- 当前策略更偏向“先确保球在正前方再启动大脚”，如果现场发现很难进入 `auto_visual_kick`，优先放宽 `auto_visual_kick_enable_angle`。

## 未运行/未验证项

- 未做实体机器人运行测试。
- 未执行 C++ 构建。
- 已做 YAML/XML 解析检查，结果通过。

## 下一步建议

- 如果日志长时间 `Decision: adjust`，把 `auto_visual_kick_enable_angle` 从 `0.45` 提到 `0.55`。
- 如果进入 `auto_visual_kick` 但仍踢不到，把 `auto_visual_kick_enable_dist_min` 提到 `0.45`，让球别太贴近脚下。
- 如果进入后退出太早，继续增大 `min_msec_kick` 到 `3500`。
