# 配置速度激进化改动报告

## 修改目标

在保留近球限速保护的前提下，提高单机器人测试时的整体运动速度，让追球和调整不再过于保守。

## 改动文件

- `src/brain/config/config.yaml`

## 参数变化

- `robot.vx_limit`: `0.7` -> `0.9`
- `robot.vy_limit`: `0.35` -> `0.45`
- `robot.vtheta_limit`: `1.0` -> `1.2`
- `strategy.near_ball_speed_limit`: `0.3` -> `0.45`

## 内容审查结论

- 当前速度比上一版更积极，但仍低于原始配置中的 `vx_limit: 1.2`、`vtheta_limit: 1.5`。
- `strategy.limit_near_ball_speed` 仍保持 `true`，所以接近球时仍会触发限速。
- 该修改是全局配置，会影响 `game.xml`、`shoot.xml`、`chase.xml`、`single_robot.xml` 等所有使用 `robot.*` 和近球限速的流程。

## 未运行/未验证项

- 按要求未做运行测试。
- 未执行构建或实体机器人验证。

## 下一步建议

- 如果实体上仍显得慢，可以下一档把 `vx_limit` 提到 `1.0`，`near_ball_speed_limit` 提到 `0.5`。
- 如果出现冲过球或踢前晃动，优先回退 `near_ball_speed_limit`，不要先降全局速度。
