# K1 5v5 v1.6 球路预测改造方案记录

## 如何启动或停止调用预测球

当前实现默认**不改变原有追球逻辑**。是否调用预测球由 `src/brain/config/config.yaml` 中的 `ball_prediction` 参数控制。

### 完全停止球路预测

```yaml
ball_prediction:
  enable: false
```

效果：

- `brain` 不更新预测球；
- 行为树仍使用原始 `data->ball`；
- 原始 `/booster_vision/detection`、`/booster_vision/ball` 不受影响。

### 只计算预测结果，但不让追球使用

```yaml
ball_prediction:
  enable: true
  use_for_chase: false
```

效果：

- `brain` 内部更新 `filteredBall`、`predictedBall`、`predictedBallPos`、速度和加速度；
- 可用于日志和后续守门/拦截调试；
- 追球仍使用原始球位置。

### 让追球使用预测球

```yaml
ball_prediction:
  enable: true
  use_for_chase: true
```

效果：

- `Chase` 和 `SimpleChase` 优先使用 `data->predictedBall`；
- 如果预测无效，自动回退到原始 `data->ball`。

### 回退方式

比赛或调试中如发现机器人追球异常，直接将：

```yaml
ball_prediction:
  enable: false
```

或至少将：

```yaml
ball_prediction:
  use_for_chase: false
```

即可回退到原始行为。

---

## 设计目标

v1.6 中球位置主要由 `vision` 发布到 `/booster_vision/detection`，再由 `brain` 转换为 `GameObject` 和 field 坐标。为了避免机器人自身运动污染速度估计，球路预测放在 `brain` 侧使用 field 坐标更新，而不是直接在 vision 的 robot/camera 相对坐标中预测。

本次改造采用混合方案：

1. 新建 `k1_ball_predictor` package，提供可复用的 C++ 球运动预测库；
2. `brain` 在 field 坐标系中调用该库；
3. 原始视觉 topic 保持不变；
4. 行为树通过参数逐步选择是否使用预测球。

## 当前已落地范围

第一阶段实现内容：

- `[x, y, vx, vy]` 二维 Kalman 滤波；
- 速度、加速度估计；
- `predict_time` 秒后的短时单点预测；
- 最大速度、最大加速度限幅；
- 时间间隔异常时重置或降级；
- 短时丢球时只预测不更新观测；
- 简单异常跳点剔除；
- `Chase` / `SimpleChase` 可选使用预测球；
- 默认关闭，不破坏原有流程。

## 数据流

```text
/booster_vision/detection
  -> Brain::detectionsCallback()
  -> getGameObjects()/detectProcessBalls()
  -> data->ball field 坐标
  -> Brain::updateBallPrediction()
  -> k1_ball_predictor::BallMotionPredictor
  -> data->filteredBall / data->predictedBall / data->predictedBallPos
  -> Chase/SimpleChase 可选使用 predictedBall
```

## 参数说明

```yaml
ball_prediction:
  enable: false                 # 是否启用预测计算
  use_for_chase: false          # Chase/SimpleChase 是否使用预测球
  predict_time: 0.25            # 预测未来多少秒后的单点位置
  min_dt: 0.02                  # 小于该帧间隔不估计速度
  max_dt: 0.20                  # 大于该帧间隔不估计速度
  max_history_gap: 0.50         # 超过该历史间隔重置
  max_speed: 4.0                # 球速限幅 m/s
  max_acceleration: 8.0         # 加速度限幅 m/s^2
  enable_kalman: true           # 是否启用 Kalman 平滑
  process_noise_position: 0.03
  process_noise_velocity: 0.60
  measurement_noise: 0.06
  initial_covariance: 1.0
  lost_prediction_timeout: 0.4  # 丢球后最多只预测多久
  max_jump_distance: 1.2        # 异常跳点距离阈值
  max_jump_speed: 6.0           # 异常跳点速度阈值
  trajectory_step: 0.1          # 多步轨迹步长
  trajectory_count: 20          # 多步轨迹点数量
```

## 后续建议

1. 先开启 `enable: true, use_for_chase: false` 做日志观察；
2. 确认 field 坐标预测方向正确后，再开启 `use_for_chase: true`；
3. 第二阶段再将 `predictedBallPos` 接入守门和 `Intercept`；
4. 第三阶段再考虑球门线穿越点、可达拦截点、机器人速度模型和场地边界处理。
