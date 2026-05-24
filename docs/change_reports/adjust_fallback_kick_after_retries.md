# Adjust 多次失败后踢球兜底报告

## 修改目标

防止机器人在近球后过度 `Adjust`。当多次调整仍无法明显改善时，允许进入 `auto_visual_kick` 兜底踢球，但仍保留距离和角度限制，避免明显乱踢。

## 改动文件

- `src/brain/src/brain_tree.cpp`

## 关键逻辑变化

- `Adjust::tick()` 新增 `adjustEscapeCount`：
  - 每次判断当前调整方向卡住并切换绕球方向时，计数加 1。
  - 一旦退出调整区域、球未知、球距变远或重新开始调整，计数清零。
  - 计数写入行为树黑板：`adjust_escape_count`。
- `StrikerDecide` 新增 `adjustFallbackKick`：
  - `adjust_escape_count >= 2`
  - `ballRange < 1.2`
  - `fabs(ballYaw) < auto_visual_kick_enable_angle * 1.3`
  - `fabs(deltaDir) < 0.8`
- 满足兜底条件时，`visualKickAligned` 视为通过，允许进入 `auto_visual_kick`。

## 预期效果

- 前两次仍优先尝试 `Adjust` 找更好角度。
- 如果多次换方向仍没有改善，接受当前可用角度进入视觉踢球。
- 降低机器人在球前长时间绕位和犹豫的概率。

## 验证

- 已检查 `config.yaml`、`shoot.xml`、`subtree_striker_play.xml` 解析通过。
- 未执行 C++ 构建和实体机器人测试。
