# Ground Plane / Z 平面拟合测试与使用指南

本文档说明 `K1_5v5_Demo_v1.6` 中动态地面平面拟合功能的测试流程，以及如何启停该功能、如何查看或调用拟合后的目标坐标。

## 1. 涉及文件

### 代码文件

```text
src/vision/include/booster_vision/base/ground_plane.h
src/vision/src/base/ground_plane.cpp
src/vision/include/booster_vision/vision_node.h
src/vision/src/vision_node.cpp
src/vision/src/base/CMakeLists.txt
```

### 配置文件

```text
src/vision/config/vision.yaml
```

新增配置段：

```yaml
ground_plane:
  enable: false
  debug: true
  update_every_n_frames: 5
  sample_step: 8
  min_depth: 0.2
  max_depth: 6.0
  min_ground_height: -0.20
  max_ground_height: 0.25
  ransac_distance_threshold: 0.02
  min_inlier_ratio: 0.35
  max_normal_tilt_deg: 25.0
  force_update_head_pitch_delta_deg: 3.0
  force_update_head_yaw_delta_deg: 5.0
  use_cached_plane_when_fit_failed: true
  write_to_position: true
  write_to_position_projection: false
```

## 2. 输出字段含义

视觉检测结果发布在：

```text
/booster_vision/detection
```

消息类型：

```text
vision_interface/msg/Detections
```

单个目标对象字段：

```text
DetectedObject.position_projection
DetectedObject.position
DetectedObject.position_confidence
DetectedObject.target_uv
```

当前约定：

| 字段 | 含义 |
|---|---|
| `position_projection` | 默认是原始固定 `z = 0` 投影结果；当 `write_to_position_projection: true` 且 ground plane 求交成功时，会被替换为地面平面拟合后的结果。|
| `position` | 当 `write_to_position: true` 时，优先写入 depth 或 ground plane 增强后的目标位置；失败时 fallback 到固定投影。|
| `position_confidence` | `1` 表示固定投影 fallback；`2` 表示 depth estimator 或 ground plane 测量结果。|
| `target_uv` | ground plane 求交使用的目标像素点。球、人、对手、门柱使用 bbox 底部中心；场地标志点使用 bbox 中心。|

brain 当前主要读取：

```text
DetectedObject.position_projection
```

因此，若只想安全对比 ground plane 结果，不要立即打开 `write_to_position_projection`。

## 3. 编译验证

在 workspace 根目录执行：

```bash
cd K1_5v5_Demo_v1.6/K1_5v5_Demo_v1.6
colcon build --packages-select vision
```

成功标准：

```text
vision package 构建成功，无编译错误。
```

如果编译失败，优先检查：

```text
1. src/vision/src/base/CMakeLists.txt 是否包含 ground_plane.cpp
2. ground_plane.h 路径是否正确
3. PCL / OpenCV 依赖是否正常
4. vision_node.cpp 是否包含必要头文件
```

## 4. 启停 ground plane 功能

### 4.1 完全关闭，保持原始 v1.6 行为

配置：

```yaml
ground_plane:
  enable: false
```

效果：

```text
1. 不进行 depth ground plane 拟合。
2. 目标位置仍走原固定 z=0 投影。
3. brain 行为与原版一致。
```

这是默认安全模式。

### 4.2 开启拟合，但只做对比，不影响 brain

配置：

```yaml
ground_plane:
  enable: true
  debug: true
  write_to_position: true
  write_to_position_projection: false
```

效果：

```text
1. vision 会从 depth 图拟合地面平面。
2. ground plane 结果写入 DetectedObject.position。
3. DetectedObject.position_projection 仍保持原固定 z=0 投影。
4. brain 当前仍读 position_projection，因此不会影响主策略。
```

推荐第一轮实机测试使用此模式。

### 4.3 开启拟合，并让 brain 使用拟合后的坐标

配置：

```yaml
ground_plane:
  enable: true
  debug: true
  write_to_position: true
  write_to_position_projection: true
```

效果：

```text
1. ground plane 求交成功时，position_projection 会被替换为拟合平面坐标。
2. brain 不需要改代码，即可通过 position_projection 使用增强结果。
3. ground plane 求交失败时，自动 fallback 到原固定 z=0 投影。
```

注意：只有在对比测试确认稳定后，才建议打开该模式。

### 4.4 配置修改后如何生效

当前 ground plane 通过 YAML 初始化，不是运行时动态参数。

修改 `vision.yaml` 后，需要重启 `vision_node`：

```bash
ros2 launch vision launch.py
```

或使用项目原有启动方式重启 vision。

## 5. 启动 vision

常用启动：

```bash
ros2 launch vision launch.py
```

如果使用自定义配置目录：

```bash
ros2 launch vision launch.py vision_config_path:=/path/to/config_dir
```

其中配置目录应包含：

```text
vision.yaml
vision_local.yaml，可选
```

如果需要显示检测画面：

```bash
ros2 launch vision launch.py show_det:=true
```

## 6. 话题检查

### 6.1 检查 RGB / depth 是否存在

```bash
ros2 topic list | grep boostercamera
```

应能看到：

```text
/boostercamera/head/rgb
/boostercamera/head/depth
```

查看频率：

```bash
ros2 topic hz /boostercamera/head/rgb
ros2 topic hz /boostercamera/head/depth
```

如果 depth 不存在或频率很低，ground plane 无法稳定工作。

### 6.2 检查 head pose

```bash
ros2 topic echo /head_pose
```

如果 `/head_pose` 没有数据，`p_eye2base` 不可靠，ground plane 与投影都会异常。

### 6.3 检查检测输出

```bash
ros2 topic echo /booster_vision/detection
```

重点观察每个目标的：

```text
label
position_projection
position
position_confidence
target_uv
```

## 7. 日志观察

当配置：

```yaml
ground_plane:
  enable: true
  debug: true
```

vision 终端会输出类似信息：

```text
ground_plane valid: 1, sampled_points: 1234, inlier_ratio: 0.56, reason:
```

字段含义：

| 字段 | 含义 |
|---|---|
| `valid` | 当前是否有有效平面。|
| `sampled_points` | 通过 depth、距离、高度过滤后的地面候选点数量。|
| `inlier_ratio` | RANSAC 平面内点比例。|
| `reason` | 拟合失败原因；如果使用缓存，会包含 `using cached plane`。|

成功状态通常应满足：

```text
valid = 1
sampled_points > 100
inlier_ratio > min_inlier_ratio，默认 0.35
```

## 8. 分阶段测试流程

### 阶段 1：原始流程确认

配置：

```yaml
ground_plane:
  enable: false
```

测试命令：

```bash
ros2 launch vision launch.py
ros2 topic echo /booster_vision/detection
```

成功标准：

```text
1. detection 正常发布。
2. position_projection 有值。
3. robot / brain 行为与改动前一致。
```

失败优先检查：

```text
1. vision 是否成功启动。
2. detection model 路径是否正确。
3. RGB 图像是否正常。
```

### 阶段 2：只开启拟合，不影响 brain

配置：

```yaml
ground_plane:
  enable: true
  debug: true
  write_to_position: true
  write_to_position_projection: false
```

测试命令：

```bash
ros2 launch vision launch.py
ros2 topic echo /booster_vision/detection
```

观察：

```text
position_projection: 原固定 z=0 投影
position: ground plane 成功时为拟合后坐标
position_confidence: ground plane 成功时为 2，否则为 1
```

成功标准：

```text
1. vision 不崩溃。
2. ground_plane valid 多数时间为 1。
3. position 与 position_projection 差异合理，不出现巨大跳变。
4. brain 因仍读 position_projection，行为不受影响。
```

失败优先检查：

```text
1. depth 是否为有效米制深度。
2. depth 是否与 RGB 对齐。
3. min_ground_height / max_ground_height 是否过窄。
4. p_eye2base 是否正常。
```

### 阶段 3：静态目标距离对比

测试方法：

```text
1. 将球或场地标志物放在已知距离，例如 1m、2m、3m。
2. 机器人保持静止。
3. 分别记录 position_projection 与 position。
```

命令：

```bash
ros2 topic echo /booster_vision/detection > detection_ground_plane_test.log
```

成功标准：

```text
1. position 的距离更接近实测距离。
2. position 的 x/y 抖动不大于 position_projection。
3. position_confidence 能正确反映 1 或 2。
```

失败优先检查：

```text
1. target_uv 是否合理。
2. ground plane 是否拟合到地面而不是障碍物。
3. camera extrin 与 z_compensation 是否准确。
```

### 阶段 4：让 brain 使用拟合后的坐标

确认阶段 2、3 稳定后，配置：

```yaml
ground_plane:
  enable: true
  debug: true
  write_to_position: true
  write_to_position_projection: true
```

测试：

```bash
ros2 launch vision launch.py
ros2 launch brain launch.py
```

成功标准：

```text
1. robot 追球距离判断更稳定。
2. 不出现明显左右抖动。
3. 不出现目标位置突然跳远或跳近。
4. ground plane 失败时仍能 fallback 到原固定投影。
```

失败回退：

```yaml
ground_plane:
  write_to_position_projection: false
```

或完全关闭：

```yaml
ground_plane:
  enable: false
```

## 9. 如何调用拟合后的坐标

### 9.1 在 ROS topic 中查看

```bash
ros2 topic echo /booster_vision/detection
```

若使用安全对比模式：

```yaml
write_to_position: true
write_to_position_projection: false
```

则读取：

```text
detected_objects[i].position
```

作为拟合后的坐标。

固定投影对照为：

```text
detected_objects[i].position_projection
```

### 9.2 在新代码中使用增强坐标

如果你编写新的 ROS2 节点订阅 `/booster_vision/detection`，推荐逻辑：

```cpp
if (obj.position_confidence >= 2 && obj.position.size() >= 2) {
    x = obj.position[0];
    y = obj.position[1];
} else if (obj.position_projection.size() >= 2) {
    x = obj.position_projection[0];
    y = obj.position_projection[1];
}
```

这样可以在 ground plane 或 depth 失败时自动使用固定投影。

### 9.3 不修改 brain 的调用方式

当前 brain 使用：

```text
obj.position_projection
```

因此若不想修改 brain，只需要配置：

```yaml
ground_plane:
  write_to_position_projection: true
```

此时 ground plane 成功时，brain 会直接拿到拟合后的坐标；失败时仍是固定投影。

### 9.4 修改 brain 后的推荐调用方式

如果后续允许修改 brain，推荐增加配置，例如：

```yaml
vision:
  use_enhanced_position: true
```

并在 brain 里采用：

```cpp
if (use_enhanced_position && obj.position_confidence >= 2 && obj.position.size() >= 2) {
    gObj.posToRobot.x = obj.position[0];
    gObj.posToRobot.y = obj.position[1];
} else {
    gObj.posToRobot.x = obj.position_projection[0];
    gObj.posToRobot.y = obj.position_projection[1];
}
```

这种方式比覆盖 `position_projection` 更清晰。

## 10. 参数调试建议

### `sample_step`

```yaml
sample_step: 8
```

调试建议：

```text
4: 点更多，更慢。
8: 默认平衡。
12 或 16: 更快，但点更少。
```

### `min_depth` / `max_depth`

```yaml
min_depth: 0.2
max_depth: 6.0
```

如果远处 depth 噪声大，可把 `max_depth` 调小，例如：

```yaml
max_depth: 4.0
```

### `min_ground_height` / `max_ground_height`

```yaml
min_ground_height: -0.20
max_ground_height: 0.25
```

如果日志显示：

```text
not enough ground candidate points
```

可以适当放宽，例如：

```yaml
min_ground_height: -0.30
max_ground_height: 0.35
```

如果拟合到障碍物，可收紧范围。

### `ransac_distance_threshold`

```yaml
ransac_distance_threshold: 0.02
```

含义：点到平面 2cm 内算内点。

depth 噪声大时可尝试：

```yaml
ransac_distance_threshold: 0.03
```

### `min_inlier_ratio`

```yaml
min_inlier_ratio: 0.35
```

如果经常失败但平面看起来合理，可适当降低：

```yaml
min_inlier_ratio: 0.25
```

不要一开始降得太低，否则可能接受错误平面。

## 11. z compensation 调试

v1.6 已有运行时校准 topic：

```text
/booster_vision/cal_param
```

消息类型：

```text
vision_interface/msg/CalParam
```

发布示例：

```bash
ros2 topic pub /booster_vision/cal_param vision_interface/msg/CalParam \
"{pitch_compensation: 0.0, yaw_compensation: 0.0, z_compensation: 0.0}"
```

例如临时增加 1cm z 补偿：

```bash
ros2 topic pub /booster_vision/cal_param vision_interface/msg/CalParam \
"{pitch_compensation: 0.0, yaw_compensation: 0.0, z_compensation: 0.01}"
```

注意：

```text
1. pitch/yaw 单位是 degree。
2. z 单位是 meter。
3. z compensation 是相机/头部外参补偿，不是 ground plane 参数。
4. 调试时不要同时大幅修改 z_compensation 和 ground_plane 高度阈值。
```

## 12. 常见问题与回退

### 问题：ground plane 一直 invalid

优先检查：

```text
1. depth topic 是否正常。
2. depth 是否为 16UC1 或 32FC1。
3. depth 是否已经 aligned 到 RGB。
4. min_depth / max_depth 是否合理。
5. min_ground_height / max_ground_height 是否过窄。
```

临时回退：

```yaml
ground_plane:
  enable: false
```

### 问题：目标坐标突然跳变

优先检查：

```text
1. ground plane 是否拟合到了错误平面。
2. inlier_ratio 是否过低。
3. max_normal_tilt_deg 是否过宽。
4. 是否有障碍物占据画面下半部分。
```

回退：

```yaml
ground_plane:
  write_to_position_projection: false
```

### 问题：brain 行为异常

如果已经打开：

```yaml
write_to_position_projection: true
```

先改回：

```yaml
write_to_position_projection: false
```

如果仍异常，完全关闭：

```yaml
enable: false
```

## 13. 推荐实机启用顺序

```text
1. enable=false，确认原流程正常。
2. enable=true, write_to_position_projection=false，只观察 position。
3. 静态目标实测 position 与 position_projection。
4. 调整 min/max depth、ground height、RANSAC 参数。
5. 确认稳定后，write_to_position_projection=true。
6. 接入 brain 实测追球、踢球、守门行为。
7. 若策略稳定，再考虑 brain 侧显式读取 position + confidence。
```

