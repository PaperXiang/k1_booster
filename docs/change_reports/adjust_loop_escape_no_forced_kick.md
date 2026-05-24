# Adjust 防循环改动报告

## 修改目标

防止近球后反复多次 `Adjust` 陷入同一种绕位循环，但不采用“调不好就立刻踢”的方式。`RLVisionKick` 仍然需要基本对准后再进入。

## 改动文件

- `src/brain/src/brain_tree.cpp`

## 关键逻辑变化

- 撤回上一版 `Adjust` 停滞后直接放行 `auto_visual_kick` 的逻辑。
- 在 `Adjust::tick()` 内新增调整进展跟踪：
  - 记录当前 `kickDir` 与人球方向差的历史最优值。
  - 如果方向差持续没有改善，判定当前绕位策略可能陷入局部循环。
  - 进入 `escapeMode` 后，短暂切换绕球切向方向，并降低切向速度。
  - 一旦方向差重新改善，退出 `escapeMode`。
- `StrikerDecide` 仍要求 `visualKickAligned` 才允许 `auto_visual_kick`。

## 预期效果

- 机器人不会因为 `Adjust` 一直用同一方向绕球而卡住。
- 调整停滞时会换一个调整方向继续寻找更好解，而不是马上踢。
- `RLVisionKick` 仍然是常用技能，但入口保持基本对准要求。

## 内容审查结论

- `CalcKickDir` 仍在决策前执行。
- `Adjust` 负责跳出局部调整循环。
- `StrikerDecide` 负责判断是否可以进入 `auto_visual_kick`。

## 验证

- 已检查 `config.yaml`、`shoot.xml`、`subtree_striker_play.xml` 解析通过。
- 未执行 C++ 构建和实体机器人测试。
