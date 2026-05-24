# 回滚 Adjust escape 防循环逻辑

## 修改时间

2026-05-24

## 修改目标

用户确认如果 `Adjust` 改动较大则回滚。经检查，当前 `Adjust` 中新增了历史最优角度记录、escape 反向尝试、调整次数统计，以及 `StrikerDecide` 基于调整次数的放行逻辑，属于中等偏大的行为逻辑修改。本次只回滚这部分大逻辑，保留现有基础 Adjust 粗调整方式。

## 修改文件

- `src/brain/src/brain_tree.cpp`

## 回滚内容

- 移除黑板变量：
  - `adjust_escape_count`

- 移除 `Adjust::tick()` 中的状态记忆：
  - `bestAdjustDelta`
  - `adjustStart`
  - `adjustLastImprove`
  - `escapeDir`
  - `escapeMode`
  - `adjustEscapeCount`

- 移除 escape 模式下的行为改变：
  - 不再因为长时间无改善而反向绕球。
  - 不再在 escape 模式下降低切向速度。
  - 不再翻转切向绕球方向。

- 移除 `StrikerDecide::tick()` 中的 fallback 放行：
  - 不再读取 `adjust_escape_count`。
  - 不再使用 `adjustFallbackKick`。
  - `RLVisionKick` 前的对准判断恢复为：
    ```cpp
    bool visualKickAligned = angleGoodForKick || reachedKickDir || fabs(deltaDir) < 0.35;
    ```

## 当前保留逻辑

- `Adjust` 仍然根据 `kickDir`、`robotBallAngleToField`、`ballYaw` 做基础绕球和转身。
- `RLVisionKick` 仍然可以在 `angleGoodForKick`、`reachedKickDir` 或 `fabs(deltaDir) < 0.35` 时进入。
- 没有引入“多次 Adjust 失败后强行 RLVisionKick”的逻辑。

## 验证结果

- 已检查无以下残留引用：
  - `adjust_escape_count`
  - `adjustFallbackKick`
  - `bestAdjustDelta`
  - `escapeMode`
  - `escapeDir`
- `config.yaml` YAML 解析通过。

## 注意事项

- 本次没有编译 C++，只做源码级检查。
- 如果后续仍担心 Adjust 循环，建议优先通过行为树门槛和参数保守化解决，而不是在 Adjust 内加入复杂状态机。
