# 球路预测稳定性改动记录

日期：2026-06-15

## 背景

现象和需求：

- 独立预测节点原来只判断 `confidence > 0.0`，可靠性过宽。
- 预测路径有时看起来方向随机或抖动。
- 需要明确比赛行为主链路应使用 `brain` 内嵌 field 坐标预测，而不是独立节点直接预测 robot/camera 相对坐标。
- 开启预测球接管追球后，边线球/定位球阶段可能被预测点干扰，出现“不去开边线球”的行为。

## 结论说明

球预测路径程序不是随机的。同样输入、时间戳和参数会得到同样输出。看起来随机方向主要来自：

1. 低置信度或误检点进入滤波；
2. 视觉坐标抖动；
3. `dt` 很小时位置差分速度放大噪声；
4. 加速度由速度差分得到，噪声更大；
5. 如果在 robot/camera 相对坐标中预测，机器人自身运动会污染球速方向。

因此本次改动重点是降低低置信度观测权重、减少差分噪声、限制预测球接管范围。

## 代码改动

### 1. `src/k1_ball_predictor`

核心文件：

- `include/k1_ball_predictor/ball_motion_predictor.hpp`
- `src/ball_motion_predictor.cpp`
- `src/ball_predictor_node.cpp`
- `config/ball_prediction.yaml`

新增配置：

```yaml
min_confidence: 40.0
confidence_full: 100.0
confidence_noise_gain: 2.0
prefer_kalman_velocity: true
velocity_smoothing: 0.5
acceleration_smoothing: 0.6
min_motion_speed: 0.05
acceleration_prediction_scale: 0.0
```

行为变化：

- 低于 `min_confidence` 的观测不再作为可靠观测。
- 独立节点可靠性从 `confidence > 0.0` 改为 `confidence >= min_confidence`，默认门槛为 `40.0`。
- Kalman 校正时根据置信度动态放大测量噪声，置信度越低，观测权重越小。
- 默认优先使用 Kalman 速度，避免 filtered 位置差分速度带来的方向抖动。
- 速度和加速度加入平滑。
- 低于 `min_motion_speed` 的速度归零，减少静止球附近的小抖动。
- 默认 `acceleration_prediction_scale: 0.0`，即不把差分加速度用于未来预测点，降低轨迹突然转向。
- 丢球 `predictOnly()` 现在先把当前位置外推到当前时间，再从该当前位置预测未来 `predict_time`，语义更清晰。

### 2. `src/brain`

核心文件：

- `include/brain_config.h`
- `src/brain.cpp`
- `config/config.yaml`

新增 brain 参数：

```yaml
ball_prediction:
  disable_for_set_play_chase: true
  min_confidence: 40.0
  confidence_full: 100.0
  confidence_noise_gain: 2.0
  prefer_kalman_velocity: true
  velocity_smoothing: 0.5
  acceleration_smoothing: 0.6
  min_motion_speed: 0.05
  acceleration_prediction_scale: 0.0
```

行为变化：

- `brain` 初始化 `BallMotionPredictor` 时传入新增参数。
- `brain` 侧可靠观测需要同时满足：
  - `ballDetected`
  - `ball_location_known`
  - `confidence >= max(strategy.ball_confidence_threshold, ball_prediction.min_confidence)`
- `getBallForChase()` 在以下情况默认回退原始球，不使用预测球：
  - `gc_game_sub_state_type != "NONE"`，即定位球/边线球等 set play 阶段；
  - `ball_out == true`，即出界处理阶段。

## 配置建议

### 安全观察模式

```yaml
ball_prediction:
  enable: true
  use_for_chase: false
```

只计算和记录预测结果，不改变追球。

### 追球使用预测球

```yaml
ball_prediction:
  enable: true
  use_for_chase: true
  disable_for_set_play_chase: true
```

正常比赛追球可使用预测球；定位球、边线球、球出界阶段自动回退原始球。

### 抖动仍明显时

优先尝试：

```yaml
velocity_smoothing: 0.7
acceleration_smoothing: 0.8
acceleration_prediction_scale: 0.0
predict_time: 0.15
```

### 预测滞后明显时

可适当降低平滑或增加预测时间：

```yaml
velocity_smoothing: 0.3
predict_time: 0.25
```

## 回退方式

完全关闭预测：

```yaml
ball_prediction:
  enable: false
```

只关闭追球接管：

```yaml
ball_prediction:
  use_for_chase: false
```

只在定位球/边线球/出界阶段回退原始球：

```yaml
ball_prediction:
  disable_for_set_play_chase: true
```
