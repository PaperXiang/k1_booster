# test 脚本移除 game_controller

## 修改时间

2026-05-24

## 修改目标

`scripts/test.sh` 用于单机测试 `test` 行为树，不需要启动裁判机节点。本次只从 test 启动入口中移除 `game_controller`，其它 vision、brain、环境准备流程保持不变。

## 修改文件

- `scripts/test.sh`

## 关键变化

- 删除：
  ```bash
  echo "[START GAME_CONTROLLER]"
  nohup ros2 launch game_controller launch.py > game_controller.log 2>&1 &
  ```

- 保留：
  - 停止旧节点
  - Jetson 性能设置
  - 自动更新服务屏蔽
  - `vision` 启动
  - `brain` 启动，仍使用 `tree:=test role:=striker disable_com:=true`

## 验证结果

- 已确认 `scripts/test.sh` 和 `src/brain/behavior_trees/test.xml` 中没有 `game_controller` / `GAME_CONTROLLER` 残留引用。

## 注意事项

- `test.xml` 本身已经通过 `control_state=3` 直接进入测试流程，不依赖 GameController 状态。
- 如果后续要做正式比赛状态测试，应继续使用 `scripts/start.sh` 或正式比赛入口。
