# 变更报告 (Change Reports)

> 约定：**每次代码更改都在此目录建一份报告**，文件名 `YYYY-MM-DD_<slug>.md`。
> 目的：在"只有 1h 赛前测试、改动多又不敢确认"的现状下，让每个改动都能被**独立验证、独立回退**，不污染原代码。

## 模板（复制下面这段）

```markdown
# 变更报告: <一句话标题>

- 日期 / 提交 / 作者：
- 类型：功能新增 / 行为修改 / 修复 / 重构 / 工具
- 默认是否生效：是 / 否（默认走原行为）
- 开关与回退：<怎么关掉、怎么退回原状>

## 1. 动机（为什么改）
## 2. 改了什么（文件 + 一句话）
## 3. 对比原先（before → after，最好有数字）
## 4. 风险（哪里可能出问题）
## 5. 如何验证
   - 离线（编译/起树/单测/仿真/回放）：
   - 赛场（看什么指标，go/no-go）：
## 6. 关联文档 / 代码位置
```

## 索引

| 日期 | 报告 | 一句话 |
|---|---|---|
| 2026-06-19 | [chase_arc_walk](2026-06-19_chase_arc_walk.md) | 让比赛实际用的 `Chase` 节点走弧线而非原地转（端口 `arc_walk` 门控） |
| 2026-06-19 | [webui_validation_telemetry](2026-06-19_webui_validation_telemetry.md) | WebUI 暴露队友球共享融合结果 + 找球/共享徽标，便于验证 |
| 2026-06-19 | [fieldview_viz](2026-06-19_fieldview_viz.md) | FieldView 加本机/对手/球速矢量/共享来源连线，并放大球场视图 |
| 2026-06-19 | [adjust_timeout](2026-06-19_adjust_timeout.md) | Adjust 绕球死锁超时→chase（借鉴八一队，默认关） |
| 2026-06-19 | [external_sources_survey](2026-06-19_external_sources_survey.md) | 八一 demo + B-Human 思路调研：取所需、列提案 |
| 2026-06-19 | [localization_bugfixes](2026-06-19_localization_bugfixes.md) | P1-1：修定位 5 处笔误(2X/_doubleX 中点、LT/PT 跨数组循环、markerType 未初始化) |
| 2026-06-19 | [head_dof_clamp](2026-06-19_head_dof_clamp.md) | P1-3：头部限位按官方 DOF 收口(yaw±1.0、低头≤0.85、两端夹) |
| 2026-06-19 | [dead_code_annotations](2026-06-19_dead_code_annotations.md) | P1-4：标注 safe_shoot/RoleSwitch/visual_defend/cmd==100 等死分支(纯注释) |
| 2026-06-19 | [selflocate_circle](2026-06-19_selflocate_circle.md) | P2-1：实现全位姿(含朝向)校正 SelfLocateCircle(中圈+中线)，默认不挂级联 |

> 0618 当天的其余改动（队友球共享包、intercept 分离、定位崩溃修复、扑救删除）见
> `docs/0618_总变更_2026-06-18.md`，本约定从 2026-06-19 起执行。
> 非"更改"的代码评审（如球路预测核对）放在 `docs/` 下，不进本目录。
