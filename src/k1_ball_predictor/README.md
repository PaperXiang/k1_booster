# k1_ball_predictor

`k1_ball_predictor` 提供二维足球运动预测能力，核心算法封装为 C++ 库 `k1_ball_predictor::BallMotionPredictor`，同时保留一个可单独启动的 ROS2 节点 `ball_predictor_node`。

## 推荐主链路

比赛行为主链路优先使用 `brain` 内嵌预测，而不是直接使用独立节点对 `/booster_vision/ball` 做预测。

原因：

- `brain` 内部使用 field 坐标 `data->ball.posToField`，球速估计不容易被机器人自身运动污染。
- 独立节点输入 `/booster_vision/ball` 的坐标语义依赖视觉发布侧，若是 robot/camera 相对坐标，机器人移动会让球路方向看起来不稳定。
- `brain` 可以结合比赛状态，在定位球、边线球、球出界阶段自动回退原始球，避免预测球影响开边线球等行为。

## 算法概要

输入：

- 时间戳 `stamp`
- 二维位置 `position`
- 置信度 `confidence`
- 可靠性标志 `reliable`

处理流程：

1. 检查坐标是否有限。
2. 用 `min_confidence` 过滤低置信度观测，默认 `40.0`。
3. 检查时间间隔，过大或倒退时重置历史。
4. 检查跳点距离和跳点速度。
5. 使用 `[x, y, vx, vy]` Kalman 滤波位置和速度。
6. 根据置信度动态放大测量噪声：置信度越低，观测权重越小。
7. 对速度、加速度做平滑和限幅。
8. 默认不把差分加速度用于未来点预测，减少轨迹随机转向/抖动。
9. 输出滤波球、预测球、速度、加速度和多步轨迹。

球预测路径不是随机程序；同样输入和参数会得到同样输出。看起来“随机方向”通常来自低置信度观测、坐标抖动、短时间差分速度、加速度噪声或坐标系不稳定。本包通过置信度权重、Kalman 速度优先、速度/加速度平滑、低速冻结、默认关闭加速度预测项来降低这种现象。

## 关键参数

### 可靠性和权重

- `min_confidence`: 最低置信度，默认 `40.0`。低于该值不作为可靠观测。
- `confidence_full`: 满置信度参考值，默认 `100.0`。
- `confidence_noise_gain`: 低置信度测量噪声放大系数，默认 `2.0`。

### 抖动抑制

- `prefer_kalman_velocity`: 默认 `true`，优先使用 Kalman 速度，减少 filtered 位置差分带来的方向抖动。
- `velocity_smoothing`: 速度平滑，默认 `0.5`。
- `acceleration_smoothing`: 加速度平滑，默认 `0.6`。
- `min_motion_speed`: 低速冻结阈值，默认 `0.05`。
- `acceleration_prediction_scale`: 加速度预测权重，默认 `0.0`，即不把差分加速度用于未来点。

### brain 集成保护

`brain` 中还有：

- `ball_prediction.use_for_chase`: 是否让 Chase/SimpleChase 使用预测球。
- `ball_prediction.disable_for_set_play_chase`: 默认 `true`。定位球、边线球、球出界阶段不让预测球接管追球。

## 独立节点

启动：

```bash
ros2 launch k1_ball_predictor ball_predictor.launch.py
```

默认订阅：

```text
/booster_vision/ball
```

默认发布：

```text
/k1_ball_predictor/ball_filtered
/k1_ball_predictor/ball_predicted
```

注意：独立节点保留用于调试和可视化，比赛追球优先使用 `brain` 内嵌预测链路。
