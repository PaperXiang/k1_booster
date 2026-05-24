# test 入口改为直接比赛进攻流程

## 修改时间

2026-05-24

## 修改目标

把原来 `start.sh` 的正式启动流程抽出为测试入口 `scripts/test.sh`，并删除旧的 `test`/`shoot` 对照入口。新的 `test` 行为树不再做“不转头直接 Kick”或单独 `shoot` 测试，而是直接进入比赛进攻链路，用于验证找球、追球、调整、视觉踢球/射门的完整流程。

## 修改文件

- `scripts/test.sh`
- `src/brain/behavior_trees/test.xml`
- 删除 `scripts/shoot.sh`
- 删除 `src/brain/behavior_trees/shoot.xml`

## 关键变化

- `scripts/test.sh` 对齐 `scripts/start.sh` 的启动流程：
  - 停止已有节点。
  - 设置 Jetson 性能模式。
  - 屏蔽自动更新相关服务。
  - 使用 `/opt/booster` 下的视觉配置。
  - 使用 `/opt/booster/BoosterRos2/fastdds_profile_udp_only.xml`。
  - 启动 `vision`、`brain`、`game_controller`。

- `scripts/test.sh` 的 brain 启动参数固定为：
  - `tree:=test`
  - `role:=striker`
  - `disable_com:=true`

- `src/brain/behavior_trees/test.xml` 改为直接比赛进攻测试：
  - `RunOnce` 设置 `control_state=3`、`decision='find'`。
  - 执行 `AutoGetUpAndLocate`。
  - 执行 `StrikerPlay`，复用正式前锋流程中的 `CamFindAndTrackBall`、`CalcKickDir`、`StrikerDecide`、`Chase`、`Adjust`、`RLVisionKick`、`Kick`。

- 删除旧 `shoot` 入口，避免后续测试时出现 `test`、`shoot` 两套射门流程混用。

## 预期效果

运行 `./scripts/test.sh` 后，机器人会跳过 GameController 比赛状态门控，直接按前锋比赛逻辑开始找球、追球、调整并尝试视觉踢球/射门。这个入口更适合单机快速验证完整进攻流程。

## 验证结果

- `test.xml` XML 解析通过。
- `config.yaml` YAML 解析通过。
- 已确认 `scripts` 和 `src/brain/behavior_trees` 下没有残留 `shoot` 入口文件或 `tree:=shoot` 引用。

## 注意事项

- `scripts/test.sh` 仍会启动 `game_controller`，但 `test.xml` 本身不等待 GameController 状态进入 `PLAY`。
- 当前 `disable_com:=true`，适合单机器人流程测试；如果要测试队友通信，需要启动时覆盖该参数。
- `git status` 在当前 Windows 沙箱用户下被 dubious ownership 拦截，未作为本次验证依据。
