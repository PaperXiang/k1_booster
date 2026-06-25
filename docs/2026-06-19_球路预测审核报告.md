# 球路预测代码核对报告（确认有无大问题）

> 日期：2026-06-19 ｜ 范围：`src/k1_ball_predictor` + brain 集成 ｜ 结论：**未发现大问题，可放心开启测试**

## 0. 结论速览

- 算法是一套标准的 **2D 匀速卡尔曼滤波（状态 [x,y,vx,vy]，只观测位置）+ 速度/加速度平滑 + 异常值剔除 + 短时外推 + 丢球纯预测**。
- **卡尔曼数学正确**（逐项核对了 `P=FPFᵀ+Q` 与 `(I-KH)P`、2×2 增益求逆）。
- **已完整接入 brain 并每帧调用**（`updateBallPrediction()` `brain.cpp:1575`，在 `brain.cpp:2825` 被调用）——不是 intercept 那种死代码。
- **默认关闭**（`ball_prediction.enable=false`、`use_for_chase=false`），关闭时 `getBallForChase()` 直接返回原始球，**不影响现有比赛行为**。
- 未发现会导致崩溃/错误决策的缺陷。剩下的是**调参**与**是否真的有用**的问题——这正好用 WebUI 的 Prediction 卡片观察。

## 1. 数据流

```
data->ball (field系) ──▶ Brain::updateBallPrediction() (brain.cpp:1575, 每 tick)
                          └─ ballPredictor->update(obs)  [k1_ball_predictor]
                               ├─ Kalman 预测+校正 → filtered
                               ├─ 速度(卡尔曼/差分)+平滑, 加速度+平滑, 死区
                               ├─ predicted = filtered + v·T + ½a·T²
                               └─ trajectory[N]
                          └─ 写回 data->filteredBall / predictedBall /
                             ballVelocityToField / predictedBallPos / ballPredictionValid
                                   │
getBallForChase() (brain.cpp:1619): 仅当 enable && use_for_chase && valid && 非定位球 → 用 predictedBall, 否则原始 ball
```

> 注意：还有一个**独立节点** `ball_predictor_node`（订阅 `/booster_vision/ball`，发 filtered/predicted 话题）。它和 brain 内嵌实例是**两套**；比赛走的是 brain 内嵌这套。两者默认 `enable` 不同（节点 true、brain 由 config 给 false），别混淆。

## 2. 逐项核对结果

| 部分 | 核对 | 结论 |
|---|---|---|
| `predictKalman`（预测协方差 `F·P·Fᵀ+Q`） | `transition[c][k]` 即 `Fᵀ`，索引正确；Q 加在对角，位置项 `×dt²`、速度项 `×dt` | ✅ 正确 |
| `correctKalman`（2×2 创新协方差求逆、增益、`(I-KH)P`） | 行列索引、`factor` 构造逐项核对 | ✅ 正确 |
| 置信度→测量噪声放大 | 低置信度时 `R×(1+gain·(1-ratio))`，合理 | ✅ |
| 异常值剔除 `isOutlier` | 跳变距离 / 跳变速度双门限 | ✅ |
| 丢球纯预测 `predictOnly` | 从**最后可信样本**按匀速外推，`lost_prediction_timeout` 超时 reset | ✅（加速度置 0，保守） |
| 速度/加速度平滑、`min_motion_speed` 死区 | 上一帧加权混合 + 静止归零 | ✅ |
| `enable=false` 路径 | 透传原始观测（reliable+finite 则 valid） | ✅ 安全 |

## 3. 小观察（非缺陷，调参/认知）

1. **`acceleration_prediction_scale` 默认 0.0**：加速度照算照发，但**不参与外推**（预测是纯匀速）。这是保守安全的默认；想要"拐弯预测"再调高，但噪声大时反受其害。
2. **外推时长 `predict_time=0.25s`**：预测球领先当前球 0.25s 的量。球快时领先明显，球慢/静止时≈原位。觉得"过冲"就调小。
3. **`min_confidence=40`**：低于此视为不可信→进纯预测分支。和视觉置信度尺度要对齐。
4. **纯预测期**直线外推，遮挡越久越飘，靠 `lost_prediction_timeout`(0.4s) 兜底，合理。

## 4. 怎么验证"球路预测到底有没有用"（用现成 WebUI）

WebUI 的 **Prediction 卡片已经显示全部所需**（无需改前端）：`Enabled/Use for chase/Valid/Predicted only` 徽标 + 预测 x/y、速度 x/y、加速度、轨迹点数。

步骤（**先看不接管**）：
1. 只开 `ball_prediction.enable=true`，保持 `use_for_chase=false`。
2. 在场上滚球，看 Prediction 卡片：
   - `Valid` 常亮；`Velocity x/y` 方向/大小与球运动一致；
   - `Predicted x/y` 沿运动方向**领先**当前球；
   - 短暂遮挡时 `Predicted only` 亮、恢复后灭。
3. 速度抖/方向错 → 调 `velocity_smoothing` / `measurement_noise`；预测过冲 → 调小 `predict_time`。
4. 上述都正常后，再把 `use_for_chase=true` 让追球用预测球（此时 `SimpleChase`/`Chase` 追的是"球将到的位置"）。

## 5. 代码位置

- 算法：`src/k1_ball_predictor/src/ball_motion_predictor.cpp`、`include/.../ball_motion_predictor.hpp`
- 独立节点：`src/k1_ball_predictor/src/ball_predictor_node.cpp`（默认参数在此 `loadConfig`）
- brain 集成：`Brain::updateBallPrediction()` `src/brain/src/brain.cpp:1575`（调用点 `:2825`）、`getBallForChase()` `:1619`
- 配置：`src/brain/config/config.yaml` 的 `ball_prediction` 段（brain 内嵌用）；`src/k1_ball_predictor/config/ball_prediction.yaml`（独立节点用）
