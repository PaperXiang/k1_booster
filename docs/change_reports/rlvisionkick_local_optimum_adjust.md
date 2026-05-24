# RLVisionKick 常用化与 Adjust 局部最优改动报告

## 修改目标

明确 `RLVisionKick` 是高频视觉踢球/带球技能，不应只作为极少数完美角度下的大脚终结。避免机器人在近球后反复 `Adjust`，为了追求完美踢球角度而陷入无法最优解的循环。

## 改动文件

- `src/brain/src/brain_tree.cpp`

## 关键逻辑变化

- 保留 `CalcKickDir -> StrikerDecide -> Adjust/RLVisionKick` 的正常流程。
- `auto_visual_kick` 仍要求基本对准，但新增局部最优逃逸条件：
  - 上一轮决策已经是 `adjust`
  - 球距小于 `1.4m`
  - 球偏角仍在视觉踢球允许范围内
  - `kickDir` 与当前人球方向差小于 `0.75rad`
  - 已调整超过 `800ms`
  - 最近 `350ms` 没有明显变好
- 满足上述条件时，允许进入 `auto_visual_kick`，接受当前局部最优解。

## 预期效果

- 球在脚边且方向差难以继续改善时，不再无限 `Adjust`。
- `RLVisionKick` 更符合“常用视觉踢球/带球技能”的定位。
- 如果已经足够接近可踢方向，会更快进入视觉踢球。

## 内容审查结论

- `CalcKickDir` 仍在 `StrikerDecide` 前执行，踢球方向仍会持续更新。
- `Adjust` 仍然优先用于对准，只是在调整停滞时放行局部最优解。
- 未改动 `auto_visual_kick` 的配置窗口和 `RLVisionKick` XML 时间参数。

## 验证

- 已检查 `config.yaml`、`shoot.xml`、`subtree_striker_play.xml` 解析通过。
- 未执行 C++ 构建和实体机器人测试。
