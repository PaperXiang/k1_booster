# test 带球测试改用 RLVisionKick

## 修改时间

2026-05-24

## 修改目标

纠正上一版测试树中把普通 `Kick` 当成带球入口的问题。当前测试目标是验证 `RLVisionKick` 带球过程中丢球后的行为，因此 `test.xml` 改为近球后直接调用 `RLVisionKick`，不再调用普通 `Kick`。

## 修改文件

- `src/brain/behavior_trees/test.xml`

## 关键变化

- 移除普通 `Kick`：
  - 不再使用 `<Kick speed_limit="0.45" ... />` 作为带球测试动作。

- 改为 `RLVisionKick` 带球：
  - `ball_range >= 1.4` 时，先用 `SimpleChase` 接近球。
  - `ball_range < 1.4` 时，进入：
    ```xml
    <RLVisionKick _while="ball_range&lt;1.4" min_msec_kick="1500" max_msec_kick="7000" range="6.0" />
    ```

- 仍然不走正式射门决策：
  - 不调用 `StrikerPlay`。
  - 不调用 `CalcKickDir`。
  - 不调用 `StrikerDecide`。
  - 不调用 `Adjust`。

- 保留丢球后前走观察：
  - 曾经看到过球后，如果 `ball_location_known` 变为 false，则执行：
    ```xml
    <SetVelocity x="0.28" y="0.0" theta="0.0" />
    ```

## 当前流程

1. `AutoGetUpAndLocate`
2. `CamFindAndTrackBall`
3. 看到球后设置 `had_ball=true`
4. 远球先 `SimpleChase`
5. 近球进入 `RLVisionKick`
6. 带球中丢球后继续向前走

## 验证结果

- `test.xml` XML 解析通过。

## 注意事项

- 当前 `RLVisionKick` 启动距离阈值是 `1.4m`，用于让机器人先追近再交给视觉带球。
- `range="6.0"` 是 `RLVisionKick` 内部退出判断中的球距上限，不是启动阈值。
- 这个测试仍然是“不射门决策测试”，只是动作本身使用底层视觉踢球/带球能力。
