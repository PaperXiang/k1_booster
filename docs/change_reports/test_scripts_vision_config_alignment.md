# 测试脚本视觉配置对齐报告

## 修改目标

解决 `start.sh` 启动正常，但 `chase.sh` 启动后 `vision_node` 容易死亡的问题。重点对齐测试脚本和正式启动脚本的视觉配置与 FastDDS 配置。

## 问题分析

- `start.sh` 启动 vision 时显式传入 `vision_config_path:=/opt/booster`。
- `chase.sh`、`shoot.sh`、`test.sh`、`assist.sh` 原来没有传 `vision_config_path`，会使用包内默认 `src/vision/config/vision.yaml`。
- 包内默认视觉配置可能使用和机器人现场不一致的模型、相机或 TensorRT engine。
- `start.sh` 使用 `/opt/booster/BoosterRos2/fastdds_profile_udp_only.xml`，测试脚本原来使用 `./configs/fastdds.xml`。
- 测试脚本里有重复 `cd dirname`，虽然不一定导致崩溃，但会增加路径不确定性。

## 改动文件

- `scripts/chase.sh`
- `scripts/shoot.sh`
- `scripts/test.sh`
- `scripts/assist.sh`

## 关键变化

- vision 启动统一改为：
  - `ros2 launch vision launch.py vision_config_path:=/opt/booster save_data:=true`
- brain 启动统一传入：
  - `vision_config_path:=/opt/booster`
- FastDDS 配置统一改为：
  - `/opt/booster/BoosterRos2/fastdds_profile_udp_only.xml`
- 删除重复的 `cd dirname`。

## 预期效果

- `chase.sh` 与 `start.sh` 使用同一套现场视觉配置。
- 避免测试脚本加载仓库默认视觉模型/配置导致 `vision_node` 崩溃。
- 测试脚本之间行为更一致，方便排查真正的 tree 问题。

## 未运行/未验证项

- 未在机器人上运行 `chase.sh`。
- 未检查 `/opt/booster/vision.yaml` 实际内容。
- 若仍崩溃，下一步应查看 `vision.log` 中模型路径、camera_type、TensorRT engine 与设备是否匹配。
