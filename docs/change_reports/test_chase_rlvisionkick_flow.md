# test 行为树改为 Chase + RLVisionKick

## 修改时间

2026-05-24

## 修改目标

保持 `scripts/test.sh` 不动，只调整 `test.xml` 的测试流程。将原来的 `SimpleChase + RLVisionKick` 改为更接近正式前锋追球逻辑的 `Chase + RLVisionKick`，用于测试追球后交给视觉带球的表现。

## 修改文件

- `src/brain/behavior_trees/test.xml`

## 关键变化

- 保留：
  - `AutoGetUpAndLocate`
  - `CamFindAndTrackBall`
  - 丢球后继续前走 `SetVelocity x=0.28`
  - 不启动 `game_controller`
  - 不使用 `StrikerDecide`
  - 不使用 `Adjust`

- 新增：
  ```xml
  <CalcKickDir />
  ```
  因为 `Chase` 会使用 `kickDir` 判断追球目标和绕球方向。

- 替换：
  ```xml
  <SimpleChase ... />
  ```
  改为：
  ```xml
  <Chase _while="ball_range>=1.4" vx_limit="1.0" vy_limit="0.35" vtheta_limit="1.2" dist="0.5" safe_dist="0.8" />
  ```

- 保留近球视觉带球：
  ```xml
  <RLVisionKick _while="ball_range&lt;1.4" min_msec_kick="1500" max_msec_kick="7000" range="6.0" />
  ```

## 当前流程

1. 起身和基础定位。
2. 找球/跟踪球。
3. 看到球后计算 `kickDir`。
4. 球距大于等于 `1.4m` 时使用 `Chase` 追球。
5. 球距小于 `1.4m` 时进入 `RLVisionKick`。
6. 曾经看到球后如果丢球，继续向前走。

## 验证结果

- `test.xml` XML 解析通过。
- 已确认 `scripts/test.sh` 中没有 `game_controller` 启动残留。

## 注意事项

- 当前 `Chase` 参数比正式比赛树保守，避免单机测试时追球过猛。
- 这个流程仍然不是正式射门流程，只用于验证 `Chase` 接 `RLVisionKick` 的带球表现。
