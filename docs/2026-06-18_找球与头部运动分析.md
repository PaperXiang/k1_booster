# Booster K1 找球逻辑 与 丢球时头/身转动方式 解析报告

> 生成日期：2026-06-18
> 对比代码：`k1_booster-main`（上游/原版）vs `k1_booster-1.6_remake`（remake 版）
> 主题：① 找球（找球）逻辑链路 ② 球丢失时**头部 + 身体**的转动方式 ③ 与官方头部自由度（DOF）极限的合规性核对
> 单位换算：1 rad ≈ 57.296°

---

## 0. 结论速览（TL;DR）

1. **找球分两种场景**：
   - **被动找球**（INITIAL/SET/追踪间隙）：`CamFindAndTrackBall` → 看不到球就 `CamFindBall`，**只动头不动身**，做 6 点离散扫描。
   - **主动找球**（决策 `decision=='find'`）：进入 `FindBall` 子树，**头 + 身体同时动**。
2. **两套代码的找球差异主要在"主动找球"**（`FindBall` 子树 + `RobotFindBall`）：
   - `main`：连续 **Lissajous（利萨如/椭圆）头部扫描** ∥ **`RobotFindBall` 身体转 + 边走边找**，可早退、可朝队友球位转。
   - `1.6_remake`：离散 **`CamFastScan` 快扫** → **`TurnOnSpot` 原地转 180°** → 再快扫 → 放弃则走回 ready 位。`RobotFindBall` 仅原地转、不前进、无队友球回退。
3. **头部软限位（两套代码完全相同）写得比 K1 实际关节极限更"宽"**：
   - Yaw 软限位 = **±1.1 rad (±63°)** → 比 URDF（±57.3°）和说明书（±59°）都**超出约 4–6°**。
   - Pitch **只夹了"抬头"方向**（`max(pitch, 0.2)`），**完全没有夹"低头"方向** → 找球时下俯指令 **1.0 rad (57.3°)** / Lissajous 1.017 rad (58.3°) / 快扫 0.9 rad (51.6°) 被**原样下发**，超出 URDF 低头极限（49°）和说明书（43°）。
   - 即代码**依赖底层固件硬限位兜底**，软件层没有按官方 DOF 收口。建议收紧（见第 6 节）。

---

## 1. 找球链路总览：在哪里、谁来找球

行为树里"找球"出现在两处（两套代码结构一致）：

```
① 被动找球（贯穿 INITIAL / SET / READY / PLAY 追踪间隙）
   CamFindAndTrackBall 子树：
     IfThenElse( ball_location_known || tm_ball_pos_reliable )
        ├─ [Yes] CamTrackBall      // 头部视线锁球
        └─ [No]  CamFindBall       // ★只动头：6 点离散扫描

② 主动找球（StrikerPlay 中 StrikerDecide 输出 decision=='find' 时）
   StrikerPlay: <SubTree ID="FindBall" _while="decision=='find'" .../>
   FindBall 子树：★头 + 身体一起动（两版差异最大，见第 4、5 节）
```

> 关键点：**"被动找球"在两套代码里完全相同**（`CamFindBall` 实现逐字一致）；**差异集中在"主动找球"的 `FindBall` 子树与 `RobotFindBall`**。

---

## 2. 找球相关节点逐一解析

下表数值即代码写死/默认值（rad；括号为换算角度）。`+pitch=低头，-pitch=抬头`（与官方坐标系一致）。

| 节点 | 作用 | 头部动作 | 身体动作 | 两套代码 |
|------|------|---------|---------|---------|
| `CamTrackBall` | 已知球位时视线锁球 | 比例跟踪（平滑系数 3.5），看不见则转向记忆/队友球位 | 无（身体跟随逻辑被注释掉）| **相同** |
| `CamFindBall` | 看不到球时头部扫描 | 6 点序列，间隔 **800ms** | 无 | **相同** |
| `CamScanField` | 入场/校准时扫场 | 正弦式连续扫，参数化 pitch/yaw | 无 | **相同** |
| `CamFastScan` | 快速一轮扫描 | 7 点序列，间隔默认 300ms | 无 | **相同** |
| `CamLissajousScan` | 平滑椭圆扫描 | 连续 Lissajous 曲线，检测到球即转跟踪 | 无 | **仅 `main` 有** |
| `RobotFindBall` | 转身找球 | （main 会朝队友球位转头）| **转身**（main 还会前进）| **差异大** |
| `TurnOnSpot` | 原地转固定角度 | 无 | 闭环转到目标角（如 180°）| **相同** |

### 2.1 `CamFindBall`（被动找球的核心，两版一致）
`main: brain_tree.cpp:278-330` ／ `remake: brain_tree.cpp:283-334`

构造里写死扫描范围：

```cpp
double lowPitch  = 1.0;   // 57.3° 低头（看近处/脚下）
double highPitch = 0.2;   // 11.5° 接近水平（看远处）
double leftYaw   = 1.1;   //  63°  左
double rightYaw  = -1.1;  // -63°  右
// 6 点顺序：低头(左→中→右) → 抬头(右→中→左)，循环；每点间隔 800ms
```

逻辑：看到球立即 SUCCESS；否则每 800ms 走一个点；若超过 50s 没动作则从头开始。**全程身体不动**。

> ⚠️ `lowPitch=1.0 (57.3°)` 与 `yaw=±1.1 (±63°)` 均超官方极限（见第 6 节）。

### 2.2 `CamFastScan`（快扫，两版一致）
头部点表（`_cmdSequence[7][2]`，`{pitch, yaw}`，`main: brain_tree.h:191` ／ `remake` 同）：

```
{0.2, 1.1}{0.2, 0.0}{0.2,-1.1}  // 抬头一排：左→中→右   (yaw ±63°)
{0.9,-1.1}{0.9, 0.0}{0.9, 1.1}  // 低头一排：右→中→左   (pitch 0.9=51.6°, yaw ±63°)
{0.2, 0.0}                       // 回中
```
间隔默认 300ms，扫完一轮返回 SUCCESS。

### 2.3 `CamLissajousScan`（**仅 `main`**，brain_tree.cpp:2278-2335）
平滑的椭圆/利萨如扫描，替代离散点跳转，避免头部猛甩：

```cpp
yaw   = yaw_amplitude * sin(t)               // 默认 yaw_amplitude=1.012 (58°)
pitch = pitch_center + pitch_amplitude*cos(t) // 默认 center=-0.253, amp=0.567
```
- `onStart` 会根据**当前头部 yaw** 计算相位 `_phaseOffset`，保证从当前位置平滑接入（无跳变）。
- 一旦 `ballDetected`，立即切换为**视线跟踪球**（按像素偏差比例移头），不再扫描。
- 在 `FindBall` 子树里被实例化为 `pitch_center=0.45, pitch_amplitude=0.567, yaw_amplitude=1.012, cycle_msec=2200`，
  → 实际 pitch ∈ [-0.117, **+1.017** (58.3°)]，yaw ∈ ±1.012 (58°)。**低头峰值 58.3° 仍超官方极限**。

### 2.4 `CamTrackBall`（锁球，两版一致）
看见球：按球在画面中心的像素偏差，比例移头（`smoother=3.5` 平滑）；看不见但记忆/队友可信：把头转向记忆球位。
两版都**保留了一段被注释掉的"头部 yaw 超 ±0.8 就转身体"逻辑**（`remake:247-278` / `main:242-273`）——即**当前追踪时身体不会自动跟随头部转**，转身找球只发生在 `FindBall` 内。

---

## 3. 球丢失时——头部转动方式

| 场景 | 触发 | 头部行为 | 代码 |
|------|------|---------|------|
| 刚跟丢/追踪间隙 | `ball_location_known==false` 且不在 `find` 决策 | `CamFindBall` 6 点离散扫描（低头排+抬头排，左右各 63°），800ms/点 | 两版一致 |
| 进入主动找球(`main`) | `decision=='find'` → FindBall | `CamLissajousScan` 连续椭圆扫描，检测到球即平滑转跟踪 | 仅 main |
| 进入主动找球(`remake`) | `decision=='find'` → FindBall | `CamFastScan` 7 点快扫（两轮，中间夹一次原地转身）| 仅 remake |
| 入场/暂停校准 | `!odom_calibrated` | `CamScanField` 正弦连续扫场 | 两版一致 |

**共同点**：头部找球都覆盖 **低头(看脚下近球) + 接近水平(看远处)** 两个 pitch 档，并左右大幅扫 yaw；区别只是 remake 用"离散跳点"，main 用"连续曲线 + 相位续接"。

---

## 4. 球丢失时——身体转动方式（两版差异最大）

身体转动只在 `FindBall` 子树（主动找球）里发生，靠两个节点：`RobotFindBall`（速度转身/前进）和 `TurnOnSpot`（闭环定角转身）。

### 4.1 `RobotFindBall`
**`1.6_remake`（brain_tree.cpp:1652-1708）—— 简单版：只原地转**
```cpp
onStart:  _turnDir = (ball.yawToRobot > 0 ? +1 : -1);   // 朝最后看到球的一侧转
onRunning: 看到球→停 (0,0,0)；否则 setVelocity(0, 0, vyaw_limit*_turnDir); // 纯原地自转
```
- 只 `vtheta`，**不前进**；无队友球位回退；看到球即停。

**`main`（brain_tree.cpp:2184-2253）—— 增强版：转身 + 边走边找 + 队友球回退**
```cpp
onStart:  优先用"队友球位"决定转向(若自己不知道而队友可信)，否则用自己记忆
onRunning:
  if (ball_location_known) return SUCCESS;                 // 已定位则交回上层
  if (ballDetected) {                                       // 看到球：比例转 + 前进衔接
      vtheta = cap(yawErr*1.2, ±vyaw_limit);
      vx = (稳定检测≥3帧 ? transition_vx : transition_vx*0.5);  // 默认 0.25，先慢后快
      setVelocity(vx, 0, vtheta);
  } else if (tm_ball_pos_reliable) {                        // 自己没看到但队友可信
      moveHead(tmBall.pitch, tmBall.yaw);                   // 头转向队友球位
      setVelocity(0, 0, cap(tmBall.yawErr*2.0, ±vyaw_limit));// 身体也转过去
  } else setVelocity(0, 0, vyaw_limit*_turnDir);            // 都没有：原地自转
```
- 端口默认 `vyaw_limit=1.2`、`transition_vx=0.25`；有 `_stableDetectedCount` 防抖（先 0.5×速度再全速前进）。

### 4.2 `TurnOnSpot`（两版一致，brain_tree.cpp `remake:1733` / `main:2337`）
闭环原地转固定角度：`onStart` 读 `rad`（如 3.14=180°），`towards_ball=true` 时按球在画面左右侧决定转向；`onRunning` 用里程计 `theta` 累计实际转角，闭环 `vtheta=(目标-已转)*2`，转够（误差<0.1rad）或超时 5s 停。

### 4.3 两版 `FindBall` 子树编排对比

**`1.6_remake`（subtree_find_ball.xml）—— 离散三段式 Sequence**
```
Sequence:
  ① [GoBackInField + CamFastScan]            // 站住快扫一轮
  ② TurnOnSpot rad=3.14 towards_ball=true     // ★原地转 180°
  ③ [GoBackInField + CamFastScan]            // 再快扫一轮
  ④ [GoToReadyPosition(vx=0.7) + Sleep 5000] // 还找不到 → 走回 ready 位等 5s
```
→ 行为是"扫一圈 → 转半圈 → 再扫 → 放弃回位"，节奏分段、头身**交替**。

**`main`（subtree_find_ball.xml）—— 反应式并行 ReactiveFallback**
```
ReactiveFallback:
  ① ScriptCondition ball_location_known        // ★一旦定位立即整树退出
  ② ReactiveSequence:
       GoBackInField
       Parallel(success_count=1):               // ★头身并行
         CamLissajousScan(yaw=1.012,pitch_center=0.45,amp=0.567,cycle=2200)
         RobotFindBall(vyaw_limit=1.2, transition_vx=0.25)
  ③ ReactiveSequence: GoToReadyPosition(vx=0.7) + Sleep 2000
```
→ 行为是"边平滑扫头边转身/趋近，发现即退出"，头身**同时**动、更连续、更快收敛；放弃等待也更短（2s vs 5s）。

---

## 5. 两套代码找球差异汇总

| 维度 | `k1_booster-main`（原版）| `k1_booster-1.6_remake` |
|------|--------------------------|--------------------------|
| 被动找球 `CamFindBall` | 6 点离散扫，800ms/点 | **完全相同** |
| 主动找球头部 | **`CamLissajousScan` 连续椭圆扫 + 相位续接 + 检测即跟踪** | `CamFastScan` 7 点离散快扫 |
| 主动找球身体 `RobotFindBall` | 转身 + **边走边趋近(transition_vx)** + **队友球位回退** + 防抖 | **只原地自转**，不前进、无队友回退 |
| `FindBall` 编排 | `ReactiveFallback` + `Parallel`（头身**并行**，早退）| `Sequence`（快扫→**转180°**→快扫→回位，头身**交替**）|
| 放弃前等待 | `Sleep 2000` | `Sleep 5000` |
| 节点完整性 | 多一个 `CamLissajousScan` 类 | 无该类 |
| 头部软限位 / `moveHead` | 与 remake **逐字相同** | 与 main **逐字相同** |

> 一句话：**`main` 的找球更"现代"**（连续扫描、头身并行、边走边找、利用队友信息、快速收敛）；**`remake` 的找球更"朴素"**（离散扫描 + 原地大转身 + 较长放弃等待）。两者**头部硬件限制处理完全一致**。

---

## 6. ★头部自由度（DOF）合规性核对（重点）

### 6.1 软限位实现（两套代码相同）
`robot_client.cpp`（`remake:49` / `main:48`）：
```cpp
int RobotClient::moveHead(double pitch, double yaw) {
    yaw   = cap(yaw, headYawLimitLeft, headYawLimitRight);  // 夹 yaw 到 [-1.1, +1.1]
    pitch = max(pitch, headPitchLimitUp);                   // ⚠️ 只夹"抬头"下界(0.2)，低头方向不夹！
    return call(CreateRotateHeadMsg(pitch, yaw));
}
```
配置值（`brain_config.h:84-86`，两套**完全相同**）：
```cpp
headYawLimitLeft  =  1.1;  // +63.0°
headYawLimitRight = -1.1;  // -63.0°
headPitchLimitUp  =  0.2;  // 抬头方向下界 = 0.2rad(11.5°低头)；即"绝不抬头过水平"
```
含义：
- **Yaw** 被夹到 **±1.1 rad (±63°)**。
- **Pitch** 仅保证 `≥0.2`（防止头抬过 0.2，注释说"看全场足够，再高都是干扰"）；**低头方向无任何软件上限**——下俯多少全靠固件硬限位。

### 6.2 代码实际下发的头部角 vs 官方极限

| 量 | 代码下发的极值 | URDF 极限 | 说明书极限 | 是否越界 |
|----|---------------|-----------|-----------|---------|
| Yaw 左/右 | **±1.1 rad = ±63.0°** | ±1.0 rad = ±57.3° | ±59° | **超 URDF +5.7°，超说明书 +4°** |
| 低头 pitch（`CamFindBall`）| **1.0 rad = 57.3°** | +0.855 rad = +49° | +43° | **超 URDF +8.3°，超说明书 +14.3°** |
| 低头 pitch（`CamFastScan`）| 0.9 rad = 51.6° | +49° | +43° | 超 URDF +2.6°，超说明书 +8.6° |
| 低头 pitch（`main` Lissajous）| **1.017 rad = 58.3°** | +49° | +43° | **超 URDF +9.3°，超说明书 +15.3°** |
| 抬头 pitch | 实际被夹到 0.2（11.5°低头），**从不抬头** | -0.349 rad = -20° | -17° | 不越界，但**抬头自由度完全未用** |

**结论**：
- 软件层把**头部活动范围设得比 K1 实际关节极限更大**（yaw ±63° > 59°/57.3°），并且**低头方向没有软件夹紧**，找球扫描的下俯指令（0.9~1.017 rad，51.6°~58.3°）普遍**超过官方低头极限（49°/43°）**。
- 这些越界指令最终**靠底层固件/机械硬限位兜底**：要么被固件截断到极限值，要么顶到机械止挡。表现上"看起来能跑"，但存在隐患：
  1. 长期顶限位 → 舵机/关节发热、磨损、定位漂移；
  2. 不同固件/说明书版本极限不一（59° vs 57.3°、43° vs 49°），软件按 63°/无下夹下发**不可移植**，换机或固件收紧后行为突变；
  3. 头部到位角与软件"以为的角"不一致 → 影响 `CamTrackBall`/测距/投影标定精度。

### 6.3 与"±90°"传言的关系
代码里**任何**头部 yaw 极值都是 **±1.1 rad (±63°)**，**不存在 ±90°**。用户提到的第三方"±90°"在本代码中**无对应**，与官方 URDF / 说明书也不符，应以官方值为准。

---

## 7. 改进建议（如需按官方 DOF 收口）

1. **`moveHead` 增加低头方向夹紧**（当前缺失）：
   ```cpp
   yaw   = cap(yaw,  headYawLimitLeft, headYawLimitRight);
   pitch = cap(pitch, headPitchLimitDown, headPitchLimitUp); // 新增 Down 下界
   ```
   并修正 `fabs(pitch > 2.0)` 笔误（应为 `fabs(pitch) > 2.0`，仅影响日志级别）。
2. **按官方极限收紧配置**（建议取说明书保守值，留 ~2° 余量）：
   - `headYawLimitLeft/Right = ±1.0`（57.3°，对齐 URDF）或 `±1.02`（≈58.5°，贴近说明书 59° 留余量）；
   - 新增 `headPitchLimitDown ≈ 0.73`（≈42°，对齐说明书 43°）或 `0.83`（≈47.5°，对齐 URDF 49° 留余量）。
3. **下调找球扫描的下俯峰值**：`CamFindBall.lowPitch` 由 `1.0` → ≤ `headPitchLimitDown`；`CamFastScan` 低头排 `0.9`、`main` Lissajous `pitch_center+amplitude` 同步收到限内。
4. **yaw 扫描幅度**：`leftYaw/rightYaw = ±1.1` 与 Lissajous `yaw_amplitude=1.012` 建议收到 `≤±1.0`，避免 yaw 顶限位。
5. 若要保留"大范围搜索"，应**用身体转动补足**（remake 的 `TurnOnSpot 180°` 或 main 的 `RobotFindBall`），而不是让头 yaw 越界。

---

## 8. 文件/行号速查（双仓）

| 内容 | `k1_booster-main` | `k1_booster-1.6_remake` |
|------|-------------------|--------------------------|
| 被动找球子树 | `behavior_trees/subtrees/subtree_cam_find_and_track_ball.xml` | 同名（含注释掉的 CamScanField）|
| 主动找球子树 | `subtree_find_ball.xml`（ReactiveFallback+Parallel）| `subtree_find_ball.xml`（Sequence+TurnOnSpot）|
| `CamFindBall` | `src/brain/src/brain_tree.cpp:278` | `:283` |
| `CamScanField` | `brain_tree.cpp:332` | `:336` |
| `CamFastScan` | 头表 `brain_tree.h:191`，实现 `brain_tree.cpp:2255` | 头表 `brain_tree.h:169`，实现 `:1710` |
| `CamLissajousScan` | `brain_tree.h:200`，`brain_tree.cpp:2278` | **无** |
| `CamTrackBall` | `brain_tree.cpp:171` | `:176` |
| `RobotFindBall` | `brain_tree.cpp:2184`（增强版）| `:1652`（原地转版）|
| `TurnOnSpot` | `brain_tree.cpp:2337` | `:1733` |
| 头部软限位 `moveHead` | `src/brain/src/robot_client.cpp:48` | `:49` |
| 头部限位配置 | `src/brain/include/brain_config.h:84-86` | `:84-86` |

---

*本报告基于对两套仓库 `brain_tree.cpp/.h`、`robot_client.cpp`、`brain_config.h` 及行为树 XML 的静态对比分析；角度换算按 1 rad=57.296°；官方 DOF 极限取自用户提供的 URDF（`K1_22dof.urdf`）与 2025 年 K1 说明书。*
