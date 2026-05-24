# 收紧 RLVisionKick 入口窗口

## 修改时间

2026-05-24

## 修改目标

针对 `RLVisionKick` 多次踢不中的问题，先不修改底层 `RLVisionKick` 技能本身，而是收紧上层 `StrikerDecide` 的进入条件。原因是当前 `RLVisionKick` 节点主要负责触发底层视觉踢球，真正容易导致踢不中的是进入时机过宽，例如球偏在侧前方、太近、太远或身体未大致对准时就触发。

## 修改文件

- `src/brain/src/brain_tree.cpp`
- `src/brain/config/config.yaml`

## 关键变化

- `StrikerDecide` 新增球在机器人坐标系下的入口窗口：
  ```cpp
  bool visualKickBallWindow = ballX > 0.35 && ballX < 2.5 && fabs(ballY) < 0.55;
  ```

- `StrikerDecide` 新增偏航窗口：
  ```cpp
  bool visualKickYawWindow = fabs(ballYaw) < autoVisualKickEnableAngle;
  ```

- `auto_visual_kick` 分支现在必须同时满足：
  - `visualKickYawWindow`
  - `visualKickBallWindow`
  - `visualKickAligned`

- 移除原来对球偏航角的放宽：
  ```cpp
  fabs(ballYaw) < autoVisualKickEnableAngle * 1.3
  ```
  改为直接使用配置阈值。

- 配置收紧：
  ```yaml
  auto_visual_kick_enable_dist_min: 0.35
  auto_visual_kick_enable_dist_max: 2.5
  auto_visual_kick_enable_angle: 0.55
  ```

## 预期效果

- 侧边球不再过早进入 `RLVisionKick`。
- 太近球不再直接触发底层视觉踢球。
- 远球优先继续 `Chase`，等球进入更合适窗口后再触发。
- 减少因为入口姿态不合适导致的踢空、错脚、提前出脚。

## 验证结果

- `config.yaml` YAML 解析通过。
- 已确认源码中不再存在 `autoVisualKickEnableAngle * 1.3` 的触发条件。

## 注意事项

- 本次没有修改底层 `RobotClient::RLVisionKick(true)` 或 SDK 视觉踢球算法。
- 如果实体测试发现 `RLVisionKick` 触发明显变少，可以优先放宽：
  - `auto_visual_kick_enable_angle`
  - `visualKickBallWindow` 的 `fabs(ballY)` 阈值
  - `auto_visual_kick_enable_dist_max`
