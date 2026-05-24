# 单机器人稳定控球改动报告

## 修改目标

本次改动用于单机器人实体安全场测试，目标是先提高控球稳定性，减少追球冲过球、近球乱冲、踢前未对准和急转摔倒风险。

## 改动文件

- `src/brain/behavior_trees/single_robot.xml`
- `scripts/single_robot.sh`
- `src/brain/include/brain_tree.h`
- `src/brain/src/brain_tree.cpp`
- `src/brain/src/brain.cpp`
- `src/brain/config/config.yaml`
- `src/brain/behavior_trees/chase.xml`

## 关键逻辑变化

- 新增 `SingleRobotDecide` 行为树节点：
  - 不使用队友 lead/assist 判断。
  - 球未知或球信息过期时输出 `find`。
  - 球距离较远时输出 `chase`。
  - 球近但位置、朝向或踢球方向未满足时输出 `adjust`。
  - 球进入稳定踢球窗口时输出 `kick`。
- `Kick` 新增可选端口：
  - `align_during_kick`
  - `align_vtheta_factor`
  - `align_vtheta_limit`
  - 默认不启用，旧行为树未配置时仍保持原来的 `crabWalk` 行为。
- `single_robot.xml` 使用保守速度链路：
  - `Chase` 使用较低 `vx/vy/vtheta`。
  - `Adjust` 使用较低切向速度和更早的转身优先。
  - `Kick` 使用较低速度，并启用轻微身体对准。
- `chase.xml` 修复 XML 特殊字符：
  - `ball_range<1.0` 改为 `ball_range&lt;1.0`。

## 参数变化

- `robot.vx_limit`: `1.2` -> `0.7`
- `robot.vy_limit`: `0.5` -> `0.35`
- `robot.vtheta_limit`: `1.5` -> `1.0`
- `strategy.limit_near_ball_speed`: `false` -> `true`
- `strategy.near_ball_speed_limit`: `0.6` -> `0.3`
- `strategy.near_ball_range`: `2.0` -> `1.5`
- `strategy.kick_range`: `1.0` -> `0.7`
- `strategy.kick_theta_range`: `0.2` -> `0.22`
- `strategy.enable_auto_visual_kick`: `true` -> `false`

## 内容审查结论

- 单机测试链路已经和多人协作逻辑解耦，适合做单机器人稳定控球验证。
- `Kick` 的身体对准逻辑是可选端口，默认不影响旧 XML。
- 当前 `Chase`、`Adjust` 的稳定化改动是全局生效，会影响正式比赛树。
- 当前 `config.yaml` 的全局速度和自动视觉踢球参数也会影响 `game.xml`、`shoot.xml` 等其他树。

## 未运行/未验证项

- 按用户要求，本报告只做内容审查，不做运行测试。
- 未执行 `colcon build`。
- 未在实体机器人上验证近球速度、转身稳定性和踢球效果。

## 下一步建议

- 若要避免影响正式比赛流程，建议把全局 `Chase/Adjust` 改动收窄到单机专用节点或端口开关。
- 若要保持正式 `game.xml` 原行为，建议恢复全局 `robot.*` 速度参数，只在 `single_robot.xml` 的节点参数中限速。
- 实体测试第一轮建议只跑 `scripts/single_robot.sh`，观察是否仍冲过球、是否踢前转身、是否急转不稳。
