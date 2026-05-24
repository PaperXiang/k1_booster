# 回滚视觉踢球参数并增加对准门槛报告

## 修改目标

恢复 `RLVisionKick` 的常用触发参数，不再把视觉踢球当作稀有大脚分支；同时确保每次由 `CalcKickDir` 更新踢球方向后，只有在机器人已经接近正确踢球方向时才进入 `auto_visual_kick`，否则继续 `Adjust` 对准。

## 改动文件

- `src/brain/config/config.yaml`
- `src/brain/behavior_trees/shoot.xml`
- `src/brain/behavior_trees/subtrees/subtree_striker_play.xml`
- `src/brain/src/brain_tree.cpp`

## 回滚内容

- `strategy.kick_range`: `0.85` -> `1.0`
- `strategy.kick_theta_range`: `0.28` -> `0.2`
- `strategy.auto_visual_kick_enable_dist_min`: `0.35` -> `0.2`
- `strategy.auto_visual_kick_enable_dist_max`: `2.8` -> `4.0`
- `strategy.auto_visual_kick_enable_angle`: `0.45` -> `0.8`
- `RLVisionKick min_msec_kick`: `3000` -> `1500`
- `RLVisionKick max_msec_kick`: `9000` -> `7000`
- `RLVisionKick range`: `4.0` -> `6.0`
- `RLVisionKick` 控球成本退出阈值：`12.0` -> `8.0`

## 新增准确性门槛

- `StrikerDecide` 新增 `visualKickAligned` 判断：
  - `angleGoodForKick`
  - 或已经穿过/接近 `kickDir`
  - 或 `kickDir` 与当前人球方向差小于 `0.35 rad`
- `auto_visual_kick` 仍使用原来的常用距离和角度窗口，但必须满足 `visualKickAligned`。
- 如果球在对方半场但踢球方向未对准，会继续输出 `adjust`，由 `Adjust` 先绕位/转身。

## 内容审查结论

- `CalcKickDir` 在 `shoot.xml` 和正式前锋子树中仍位于 `StrikerDecide` 之前，因此每次决策前都会更新 `kickDir`。
- 视觉踢球参数已恢复常用配置，不会因为上一轮收窄窗口而难以进入。
- 新增门槛只防止“没对准就直接视觉踢球”，目标是提高踢中概率。

## 验证

- 已检查 `config.yaml` YAML 解析通过。
- 已检查 `shoot.xml` 和 `subtree_striker_play.xml` XML 解析通过。
- 未执行 C++ 构建和实体机器人测试。
