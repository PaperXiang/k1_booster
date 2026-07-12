# 技术设计

## 数据流与边界

### 守门员交接

GameController 和队友周期状态仍作为角色事实来源。现有交接命令扩展为请求/确认状态机：原 GK 发起请求后继续承担 GK 职责；目标机器人收到请求后声明接任；原 GK 只有观察到目标周期角色状态确认后才退出。请求超时、目标离线或目标受罚时清除请求并保持/恢复原 GK。

不修改 UDP 传输机制，不引入 ACK 报文格式版本；优先复用已有 `cmd`、`cmdId` 和周期 `playerRole`，减少协议兼容影响。

### 阵型分配

`FormationPlanner` 保留两种分配方式。默认配置切换为按升序 player ID 映射；按距离分配仅作为显式实验选项。3v3 的两名 field player 因共同的存活 ID 集合和固定槽位顺序得到一致结果。

### 几何合法化

把所有适用约束视为联合约束，而非一次性顺序写坐标。合法化过程有限次迭代，每轮依次处理场内边距、禁区、开球半场/中圈和任意球距离；迭代后再次验证。无法同时满足时返回无效计划，由行为树走旧站位回退，而不是输出非法目标。

### 任意球行为树

在现有 striker freekick 子树中增加 formation 分支。formation 开启且 `GoToFormationSlot` 产生有效目标时执行新 planner；关闭或节点失败时执行原有 `GoToFreekickPosition`。守门员任意球行为保持原逻辑。

### Kickoff guard

guard 将球移动视为时间序列事件。只有连续若干可信观测均超过阈值才确认首触球；单帧突变只记录候选。若机器人 field 位姿在同期发生大幅修正，则候选清零并重新锁定稳定基准。保留原有最大时间窗口，避免永久锁死。

## 配置兼容

- 保留用户当前 `team_id`、`player_id` 和 `number_of_players: 3`。
- `formation.assign_by_distance` 默认改为 `false`。
- 新增 GK 交接确认超时和 kickoff guard 连续帧/定位跳变参数时提供保守默认值。
- formation 总开关继续默认关闭，避免未经实机验证自动启用。

## 测试边界

- 对 `FormationPlanner` 增加纯 C++ 测试，覆盖 3v3 映射和几何边界。
- 对可抽取的 guard 判定或角色交接状态转换增加纯逻辑测试；若现有构建结构不适合无 ROS 单测，则至少通过小型 helper 将决策逻辑从 ROS 回调中分离。
- 行为树 XML 接入通过静态结构检查和相关包构建验证。

## 回滚

每项功能保留配置开关和旧行为回退。若多机实测不稳定，可关闭 role assignment、formation 或 kickoff guard，而无需回退整个提交。
