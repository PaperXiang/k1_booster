# 球路预测测试说明

日期：2026-06-15

## 1. 构建检查

建议先避免 `webui_port_bundle` 中重复 ROS 包影响 colcon：

```bash
colcon build --base-paths src --packages-select k1_ball_predictor brain
```

如果仍在 workspace 根目录直接 `colcon build`，确认没有重复包名，或在不参与构建的 bundle 目录放置 `COLCON_IGNORE`。

## 2. 静态参数检查

确认以下文件中的参数一致：

- `src/k1_ball_predictor/config/ball_prediction.yaml`
- `src/brain/config/config.yaml`

关键参数：

```yaml
min_confidence: 40.0
confidence_full: 100.0
confidence_noise_gain: 2.0
prefer_kalman_velocity: true
velocity_smoothing: 0.5
acceleration_smoothing: 0.6
min_motion_speed: 0.05
acceleration_prediction_scale: 0.0
disable_for_set_play_chase: true  # brain only
```

## 3. 功能验证：只计算不接管追球

配置：

```yaml
ball_prediction:
  enable: true
  use_for_chase: false
```

预期：

- `brain` 内部更新 `filteredBall`、`predictedBall`、`predictedBallPos`、球速和加速度。
- Chase/SimpleChase 仍使用原始球。
- 机器人追球行为与关闭预测时一致。

观察点：

- 预测点应沿球真实运动方向，不能长期反向。
- 球静止时速度应逐渐变小并低速冻结。
- 低置信度球点不应明显拉动预测轨迹。

## 4. 功能验证：追球接管

配置：

```yaml
ball_prediction:
  enable: true
  use_for_chase: true
  disable_for_set_play_chase: true
```

预期：

- 正常比赛 `gc_game_sub_state_type == "NONE"` 且 `ball_out == false` 时，Chase/SimpleChase 可使用预测球。
- 预测无效时自动回退原始球。

测试动作：

1. 缓慢移动球，观察机器人是否朝球运动方向前方追。
2. 球静止，观察机器人目标点是否稳定，避免左右抖动。
3. 人为制造低置信度或短时丢球，观察是否短时外推且不大幅乱跳。

## 5. 边线球/定位球回归测试

重点验证用户反馈的“球预测打开后不开边线球”。

配置：

```yaml
ball_prediction:
  enable: true
  use_for_chase: true
  disable_for_set_play_chase: true
```

预期：

- 当 `gc_game_sub_state_type != "NONE"` 时，`Brain::getBallForChase()` 返回原始 `data->ball`，不返回 `data->predictedBall`。
- 当 `ball_out == true` 时，也返回原始 `data->ball`。
- 边线球/定位球行为不应被预测点带偏。

验证方式：

1. 进入边线球/定位球状态。
2. 确认机器人仍按原有边线球/定位球逻辑走位和开球。
3. 对比关闭预测时行为，应无明显差异。

## 6. 独立节点调试

启动：

```bash
ros2 launch k1_ball_predictor ball_predictor.launch.py
```

确认：

- `/k1_ball_predictor/ball_filtered` 有输出。
- `/k1_ball_predictor/ball_predicted` 有输出。
- 当 `/booster_vision/ball.confidence < 40.0` 时，当前观测不会作为可靠观测更新。

注意：独立节点主要用于调试/可视化；比赛追球优先验证 `brain` 内嵌 field 坐标预测链路。

## 7. 失败回退

如果比赛或调试中发现异常，按风险从小到大回退：

```yaml
ball_prediction:
  use_for_chase: false
```

或：

```yaml
ball_prediction:
  enable: false
```
