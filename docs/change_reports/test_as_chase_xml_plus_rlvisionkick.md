# test 改为 chase.xml + RLVisionKick

## 修改时间

2026-05-24

## 修改目标

将 `src/brain/behavior_trees/test.xml` 改为基于 `chase.xml` 的结构，只在近球时额外加入 `RLVisionKick`。本次不改 `scripts/test.sh`，也不改 `chase.xml` 本体。

## 修改文件

- `src/brain/behavior_trees/test.xml`

## 关键变化

- `test.xml` 现在使用与 `chase.xml` 基本一致的流程：
  - 引入 `subtree_cam_find_and_track_ball.xml`
  - `control_state>=2` 时执行 `CamFindAndTrackBall`
  - `control_state==3` 时进入测试流程
  - 未知球位置时播放 `search`
  - 已知球位置时执行 `SimpleChase`

- 在 `chase.xml` 的近球位置增加：
  ```xml
  <RLVisionKick _while="ball_range&lt;1.4" min_msec_kick="1500" max_msec_kick="7000" range="6.0" />
  ```

- 移除上一版 test 中的自定义流程：
  - `AutoGetUpAndLocate`
  - `CalcKickDir`
  - `Chase`
  - `had_ball`
  - 丢球后 `SetVelocity x=0.28`

## 当前 test 流程

```xml
<SubTree ID="CamFindAndTrackBall" _autoremap="true" _while="control_state>=2" />
<Sequence _while="control_state==3">
  <Sequence _while="!ball_location_known">
    <PlaySound sound="search" />
  </Sequence>
  <Sequence _while="ball_location_known">
    <SimpleChase stop_dist="1.0" stop_angle="0.3" />
    <RLVisionKick _while="ball_range&lt;1.4" min_msec_kick="1500" max_msec_kick="7000" range="6.0" />
    <PlaySound sound="playful" _while="ball_range>=1.5" />
    <PlaySound sound="exited" _while="ball_range&lt;1.0" />
  </Sequence>
</Sequence>
```

## 验证结果

- `test.xml` XML 解析通过。
- 已确认 `test.xml` 和 `scripts/test.sh` 中没有以下残留：
  - `AutoGetUpAndLocate`
  - `CalcKickDir`
  - `<Chase`
  - `had_ball`
  - `SetVelocity x`
  - `game_controller`

## 注意事项

- 这次是“test 等于 chase.xml 加 RLVisionKick”，不是正式前锋 `StrikerPlay` 流程。
- 当前近球阈值为 `ball_range < 1.4`，如果实体上触发过早或过晚，可以只调这个阈值。
