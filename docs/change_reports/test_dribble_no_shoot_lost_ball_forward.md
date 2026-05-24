# test 行为树改为不射门带球丢球测试

## 修改时间

2026-05-24

## 修改目标

把 `test.xml` 从“直接比赛进攻射门流程”改成“单机不射门带球测试”。目标是观察机器人在带球过程中如果丢球，是否会继续向前走，以及前走时的稳定性和方向表现。

## 修改文件

- `src/brain/behavior_trees/test.xml`

## 关键变化

- 删除 `StrikerPlay` 流程引用，避免进入正式前锋决策。
- 不再执行 `CalcKickDir`、`StrikerDecide`、`Adjust`、`RLVisionKick`。
- 保留 `AutoGetUpAndLocate`，保证倒地恢复和基础定位仍参与测试。
- 保留 `CamFindAndTrackBall`，用于找球和跟踪球。
- 新增黑板变量 `had_ball`：
  - 初始为 `false`。
  - 一旦 `ball_location_known`，设置为 `true`。
  - 之后如果丢球，进入丢球后前走逻辑。

## 当前行为

- 看到球时：
  - `ball_range >= 0.75`：使用 `SimpleChase` 低速追近球。
  - `ball_range < 0.85`：使用低速 `Kick` 推带球，`speed_limit=0.45`。

- 从未看到球时：
  - 停止移动，等待 `CamFindAndTrackBall` 找球。

- 曾经看到球但后来丢球时：
  - 执行 `SetVelocity x=0.28 y=0.0 theta=0.0`。
  - 即继续向前走，不射门、不调整、不调用视觉踢球。

## 验证结果

- `test.xml` XML 解析通过。

## 测试观察重点

- 丢球后是否能稳定向前走。
- 丢球后前走是否会明显偏航。
- 球再次进入视野后，是否能重新进入追球/推带。
- `Kick` 低速推带时是否仍会把球推出视野。

## 注意事项

- 这里的 `Kick` 是代码里的普通推带节点，底层行为是按球方向 `crabWalk`，不是 `RLVisionKick` 大脚视觉踢球。
- 当前丢球后前走速度为 `0.28 m/s`，偏保守，适合先看稳定性。
