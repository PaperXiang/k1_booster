# v1.6 Ground Plane / Z Compensation 实施方案

本文档用于记录在 `K1_5v5_Demo_v1.6` 中新增“基于动态地面平面拟合的目标定位优化”的当前实施方案。

## 目标

- 保留现有固定 `z = 0` 投影作为默认路径和 fallback 路径。
- 在现有 `vision` package 中新增一个可选的、基于深度图的地面平面估计器。
- 在不修改消息定义的前提下，使用拟合后的地面平面计算更准确的目标位置。
- 通过 YAML 开关支持分阶段测试、启用和回退。
- 在 vision 输出验证稳定之前，避免直接改变 brain 行为。

## 当前 v1.6 基线

- RGB 输入：`/boostercamera/head/rgb`。
- Depth 输入：当 `use_depth: true` 时使用 `/boostercamera/head/depth`。
- 头部姿态输入：`/head_pose`。
- Vision 输出：`/booster_vision/detection`、`/booster_vision/ball`、`/booster_vision/line_segments`。
- 相机内参和外参从 `src/vision/config/vision.yaml` 加载。
- 已经存在通过 `/booster_vision/cal_param` 进行运行时 pitch / yaw / z compensation 的机制。
- 当前目标投影使用 `CalculatePositionByIntersection(...)`，本质是将相机射线与 base 坐标系下固定的 `z = 0` 平面求交。
- Brain 当前主要消费 `DetectedObject.position_projection`。

## 实施选择

本次迭代将地面平面估计器集成到现有 `vision` package 中，而不是新建独立 ROS2 package。原因如下：

- `vision_node` 已经持有同步后的 RGB / depth / head-pose 数据。
- `vision_node` 已经持有 `Intrinsics` 和当前帧的 `p_eye2base`。
- 不需要新增跨节点 topic / message 同步。
- 该功能默认可以保持完全关闭，不影响原有主流程。

实现代码隔离在以下文件中：

```text
src/vision/include/booster_vision/base/ground_plane.h
src/vision/src/base/ground_plane.cpp
```

## 数据流

```text
RGB + depth + head pose
  -> DataSyncer
  -> p_eye2base = p_head2base * p_headprime2head_ * p_eye2head_
  -> GroundPlaneEstimator::UpdateFromDepth(depth_float, intr_, p_eye2base)
  -> YOLO detections
  -> 始终计算固定 z=0 投影
  -> 当 ground plane cache 有效时，额外计算地面平面求交结果
  -> 默认保持 position_projection 为固定投影结果
  -> position 可携带增强后的拟合坐标
```

## YAML 开关

在 `vision.yaml` 中新增 `ground_plane` 配置段，默认关闭：

```yaml
ground_plane:
  enable: false
  debug: true
  write_to_position: true
  write_to_position_projection: false
```

推荐分阶段测试：

1. `enable: false`：保持原始 v1.6 行为。
2. `enable: true`，`write_to_position_projection: false`：只将增强后的拟合结果发布到 `position` 中，用于安全对比。
3. `write_to_position_projection: true`：允许 brain 在不改代码的情况下，通过 `position_projection` 消费地面平面拟合后的坐标。

## Fallback 语义

- `position_projection`：默认表示固定 `z = 0` 投影结果；只有当 `write_to_position_projection: true` 且 ground plane 求交成功时，才会被替换为地面平面拟合坐标。
- `position`：当 `write_to_position: true` 时，表示推荐使用的最终位置；否则保持原有 depth estimator 输出语义。
- `position_confidence`：
  - `1`：固定投影 fallback。
  - `2`：depth estimator 或 ground plane 测量结果。

## 需要验证的风险

- depth 图转换后的单位必须是米。
- 相机到 base 的变换方向必须保持为 `eye -> base`。
- 地面候选点的高度筛选范围可能需要根据不同机器人进行单独调参。
- 当头部姿态变化时，缓存的 base frame 平面必须重新转换到当前 camera / eye frame。
- 第一轮验证时不要同时大幅调整 `z_compensation` 和 ground plane 高度阈值，否则难以判断误差来源。
