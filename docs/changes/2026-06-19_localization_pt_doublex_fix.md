# 变更报告: 修复定位历史 bug — SelfLocatePT 几何 + _doubleX 方向判据

- 日期 / 作者：2026-06-19 / 与 PaperXiang 结对
- 类型：修复（定位几何校正器）
- 默认是否生效：**两者当前均休眠**。`SelfLocatePT` 几何 bug 已修，但**为稳妥已在 `subtree_locate.xml` 注释回休眠**（验证后取消注释即启用）；`_doubleX` 所属 `SelfLocateLocal` 不在级联，亦休眠（代码已正确）。
- 来源：本会话 P1/P2-1 审核 `docs/2026-06-19_P1_P2-1_审核.md` §6 发现的历史 bug（非本会话引入）

> **稳妥决定（2026-06-19）**：`SelfLocatePT` 修复后本会让它在级联中生效，但因未实机验证，已**注释掉 `subtree_locate.xml` 中该行**让它回到原本的休眠态（零损失）。比赛默认不跑它；仿真验证 `/locate/pt/success` 合理后，取消注释即可启用。下文 §3「风险」中"由休眠转生效"的描述，仅在你取消注释启用后才适用。

## 1. 动机
审核 P1 时发现：`SelfLocatePT`、`_doubleX` 各有坐标笔误，使它们**根本无法匹配/通过** —— 表面在跑、实际从不生效。这与团队自评"定位鲁棒性极差"吻合：能真正生效的几何校正器比表面少。修好等于**白捡回两个定位校正器**。

## 2. 改了什么（均 `src/brain/src/brain_tree.cpp`）
| 位置 | 原 (bug) | 改为 | 依据 |
|---|---|---|---|
| `SelfLocatePT` 匹配条件(:2960) | `fabs(\|t.x−p.x\| − \|goalAreaWidth−goalWidth\|/2) < 0.3` | `t.y−p.y`（用 y 间隔） | 球门柱(y=±goalWidth/2) 与 球门区 T(y=±goalAreaWidth/2) 在 **y** 方向相隔 `(goalAreaWidth−goalWidth)/2`；x 相同。原用 x 差(≈0) 比 0.7 永远为假 |
| `SelfLocatePT` 地图点(:2988) | `pos_m.x = half * fd.length` | `half * fd.length/2.0` | 球门区 T 在底线上 x=±length/2=±7.08；原 ±14.16 落到场外 → `norm(pos_o−pos_m)` 永 > maxDrift 不匹配 |
| `_doubleX` 方向判据(:2217) | `fabs(p0.y − p1.y) > 0.3 // 方向不对` | `p0.x − p1.x`（用 x） | 两 X 在中线上 x≈0(方向)、y 间距≈中圈直径(距离)。原用 y 做方向判据，与下一行 y≈2R 自相矛盾 → 永不通过。对照可工作的孪生 `SelfLocate2X`(用 .x 判方向) |

> 用 `FD_ADULTSIZE`(goalWidth=2.6→柱 y=±1.3; goalAreaWidth=4→T y=±2.0; y 间隔=0.7; 均 x=±7.08) 核算确认。

## 3. 风险（重点）
- **`SelfLocatePT` 由"休眠"变"生效"** —— 这是本次最该注意的点。修复后它会在比赛中真正参与定位（球门柱+球门区 T 模式 → 平移校正）。
  - **护栏**：与其它在用的几何校正器(2X/LT/Border)**同级** —— 几何模式自检 + `maxDrift` 匹配门限 + **全地标 residual 一致性校验**(>tolerance 拒绝)。即误检会被 residual 拦。风险等级与既有在用校正器相同。
  - **但未实机验证** → 见 §4 必须仿真/回放验证；**若不放心，安全回退 = 在 `subtree_locate.xml` 注释掉 `<SelfLocatePT .../>` 一行**（回到它原本的休眠态，零损失）。
  - 依赖 `getGoalposts()` 球门柱检测质量；柱检测噪声大时靠 residual 兜底。
- **`_doubleX`**：仍休眠（`SelfLocateLocal` 不在级联），修复只是让代码正确，**当前零运行影响**。要用需另外把 `SelfLocateLocal` 加入级联（本次未做）。
- 本机**未编译**：上机前 `colcon build` 必做。

## 4. 如何测试与启用
- 离线：`colcon build --packages-select brain`；起 `game.xml` 不崩。
- **`SelfLocatePT` 验证（已默认在级联生效，务必验）**：让机器人站在能同时看到**球门柱 + 球门区角 T** 的位置，看日志 `/locate/pt/success|fail`：此前应几乎全 fail，修复后在该位姿应能 success 且校正量(Dist)合理；WebUI `R+朝向线` 是否更贴合。**不稳 → 注释掉 `subtree_locate.xml` 的 `SelfLocatePT` 行回退**。
- `_doubleX`：默认不生效，无需测；除非把 `SelfLocateLocal` 加入级联。

## 5. 道理
- 定位是分工命脉（未定位 cost+100），多一个能用的校正器=多一处纠偏机会，尤其 `SelfLocatePT` 覆盖**贴近球门**区域（与覆盖中圈的 `SelfLocateCircle` 互补）。
- 为何敢让 PT 生效：它和团队已在用的 2X/LT/Border 是同一套"几何匹配 + residual 校验"机制、同一风险等级；修好后它本就该和它们一起工作。给了一行注释的回退路径以防万一。
- 为何不顺便把 `SelfLocateLocal` 加入级联：那是"启用新校正器"的独立决定，超出"修 bug"范围；`_doubleX` 修对即可，是否启用 SelfLocateLocal 留给后续评估。

## 6. 关联
- 审核来源：`docs/2026-06-19_P1_P2-1_审核.md` §6。
- 与 P1-1（`docs/changes/2026-06-19_localization_bugfixes.md`）区别：P1-1 修的是中点 `(p0+p1)/2`、循环 `j=0`、markerType 初始化（使配对/计算正确）；**本次修的是更深的几何判据/地图坐标 bug**（使 PT/_doubleX 从"永不命中"变为"能命中"）。两者叠加，PT 与 _doubleX 的逻辑才完整正确。
