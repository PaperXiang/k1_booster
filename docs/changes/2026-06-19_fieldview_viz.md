# 变更报告: WebUI FieldView 可视化增强 + 放大

- 日期 / 作者：2026-06-19 / 与 PaperXiang 结对（夜间自主，待审核）
- 类型：工具（前端可视化，**不影响机器人行为**）
- 默认是否生效：是（纯前端显示）
- 开关与回退：`git checkout` 这几个前端文件即可；不影响 brain。

## 1. 动机
FieldView 之前只画了球、预测球、队友、球门柱、场线，**没画本机自己、对手、障碍、球速方向、共享球来源**——验证时"看不到自己在哪、朝哪""看不到共享球到底采用了谁"。而且球场视图被挤在小网格里，太小看不清。

## 2. 改了什么（文件 + 一句话）
| 文件 | 改动 |
|---|---|
| `webui/frontend/src/components/StatusCard.tsx` | 新增可选 `className` 透传 |
| `webui/frontend/src/App.tsx` | Field 卡片用 `className="fieldCard"`（占满整行→放大） |
| `webui/frontend/src/styles.css` | `.card.fieldCard` 占满整行、`.fieldCard .field` 居中放大到 max 980px；新增球速矢量/共享连线样式 |
| `webui/frontend/src/components/FieldView.tsx` | 新增：**本机机器人 R + 朝向线**、对手 O、障碍 X、**球速矢量**（黄实线，沿预测速度，0.6s 提前量）、**队友球共享来源连线**（绿虚线：来源队友→球） |

## 3. 对比原先（before → after）
- before：小图，无自身/对手/障碍/速度/共享来源。
- after：球场视图放大占满整行；能看到**自己的位置与朝向**、对手、障碍；**黄线**=球往哪走（验证球路预测方向）；**绿虚线**=当前共享球采用的是哪个队友（验证"共享信息"）。图例同步增加。

## 4. 风险
- 极低：纯前端渲染，读的都是已有/本批新增的遥测字段，缺字段时安全跳过（`mapPoint`/可选链返回 null 即不画）。
- 不触碰任何 ROS / 比赛逻辑。

## 5. 如何验证
- 前端 `npm run build`（或 dev 热更）确认 TS 编译过。
- 打开页面选机器人：球场应放大；机器人动起来后 `R + 朝向线`跟随；滚球时黄线指向运动方向；两机开共享时绿虚线从来源队友连到球。

## 6. 关联
- 配合 `docs/changes/2026-06-19_webui_validation_telemetry.md`（brain 端 `team.ball_share` 字段）。
- 球速来自已有 `prediction.velocity`；本机位姿来自已有 `pose.field`（含 theta）。
