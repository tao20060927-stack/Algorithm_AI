# AI Player 规则与约束文档

本文档汇总了贪心 AI 中所有**非显而易见的规则、约束、特殊机制和边界条件**，按功能模块编排。每个条目注明对应代码位置。

---

## 一、路径可行性与硬约束

### 1.1 路径前缀资源非负（Prefix Resource Constraint）

**规则**：路径上**每一步的累计资源都不能变负**，不只是终点。

```
对 path = (v0,...vk)，对每个 i ∈ [1,k]:
  Ri = R + Σ(j=1..i) r(vj)  ≥ 0
```

如果任何中间步骤的资源变为负数，整条路径判非法（返回 $-\infty$）。这意味着 AI 不能选"先踩陷阱导致资源为负，再吃金币补回来"的路径。

**代码**：`Reward.cpp:796-808` (`pathKeepsResourceNonNegative`)

---

### 1.2 终点资源非负

**规则**：除前缀约束外，终点资源也必须非负。这是前缀约束的自然推论。

**代码**：`Reward.cpp:986`

---

### 1.3 路径上所有格子必须可通行

**规则**：路径上每个格子都必须满足 `isWalkableForPlanning`，即：
- 已观察（`observed=true`）
- 不是墙 `#`
- 不是未击败的 Boss 本体 `B`

Boss 正邻接触发格**可通行**（走到即触发战斗）；Boss 本体只有在击败后才变成可通行。

**代码**：`Reward.cpp:210-215`, `Reward.cpp:980-981`

---

### 1.4 零收益零信息路径直接否决

**规则**：如果一条路径既没有资源收益（dR≤0），也没有信息收益（I_proxy=0），直接返回 $-\infty$。防止 AI 为了"走到某个空格看看"而浪费步数。

**代码**：`Reward.cpp:994`

---

### 1.5 不可通行格直接否决候选

**规则**：`candidateTargets()` 排除已访问格和不可通行格。已访问格允许作为路径中转，但不能作为目标。

**代码**：`RealtimeGreedyStrategy.cpp:209-218`

---

## 二、Boss 战机制

### 2.1 顺序揭示

**规则**：Boss 战按 B 数组顺序进行。只有击败当前 Boss 后才揭示下一个 Boss 的血量。每个阶段的分支限界只能使用已揭示 Boss 的信息。

**代码**：`BossStrategy.cpp:317-345`

---

### 2.2 首 Boss 预留弹性（Opening Reserve Slack）

**规则**：第一只 Boss 允许在最短回合基础上**多花 1 回合**（reserveSlack=1），目的是保留开局技能节奏（冷却状态更有利于后续 Boss）。后续 Boss 只能在最短回合方案内选择（reserveSlack=0），仅通过冷却 tie-break 做选择。

**代码**：`BossStrategy.cpp:319`, `148-149`

---

### 2.3 阶段回合上限（Phase Turn Limit）

**规则**：如果任务 JSON 指定了 `minRounds`，每个 Boss 阶段的可用回合上限 = `minRounds - 当前已用回合 - 剩余 Boss 数量`。确保总回合不超过 minRounds。

**代码**：`BossStrategy.cpp:321-322`

---

### 2.4 Boss 战结果判定

**规则**：`bossBattleCanWinWithinLimit` 判定：
- 如果求解本身失败 → 不可击败
- 如果有 `minRounds` 字段但 `withinMinRounds=false` → 不可击败
- 否则 → 可击败

**代码**：`AIPlayerEngine.cpp:251-256`

---

### 2.5 Boss 战触发时的三种分支

当 AI 走到 Boss 正邻接格时：

| 条件 | 行为 |
|---|---|
| `bossBattleCanWin_ = true` | 击败 Boss，清除 Boss 本体阻挡 |
| `bossBattleCanWin_ = false` 且资源 ≥ coinCost | 扣除复活金币，AI 传送回起点（保持已探索地图和 Boss 揭示信息） |
| `bossBattleCanWin_ = false` 且资源 < coinCost | Game Over |

**代码**：`RealtimeGreedyStrategy.cpp:237-254`, `74-91`

---

### 2.6 复活（Revive）

**规则**：复活后：
- `pendingRevive_ = false`
- 局部坐标和真实坐标同时重置到起点
- 当前目标清空
- 路径中追加起点坐标（新的一段从起点开始）
- Boss 已经被击败的信息不会丢失（`knownBosses_` 和 `defeatedBosses_` 保留）

**代码**：`RealtimeGreedyStrategy.cpp:79-88`

---

### 2.7 Boss 战在构造函数中预计算

**规则**：`MemoryGreedyAgent` 在构造时立即调用 `runBossBattleJson()`，把 Boss 战结果存入成员变量。`run()` 中不再重新计算。

**代码**：`RealtimeGreedyStrategy.cpp:41-47`

---

## 三、停止探索规则

### 3.1 基本停止条件

满足**全部**以下条件时，AI 直接走向出口：

1. 出口已观察且在 `localMap_` 上可达
2. 不是 "R/L=0 且有非负候选" 的情况（禁止空手离场）
3. 最优候选分数 ≤ τ（默认 5.0）**或** 不存在 worthwhile target

**代码**：`RealtimeGreedyStrategy.cpp:700-710`

---

### 3.2 R/L < 1 且金币 < 3 时的强制继续

**规则**：如果当前 ratio 小于 1 且已拾取金币少于 3 枚，只要还有任何候选目标（bestScore > -1e17），**禁止走向出口**。这会同时影响 `shouldGoExit` 和 `fallbackPath`。

**代码**：`RealtimeGreedyStrategy.cpp:707`, `724-730`

---

### 3.3 Worthwhile Target 判定

**规则**：即使 bestScore > τ，也必须存在至少一个候选目标满足：

```
score > qEff × (len(当前位置→target) + len(target→出口) − len(当前位置→出口))
```

即目标 reward 必须超过绕路的步数机会成本。否则所有探索目标都不值得绕路，直接走出口。

**代码**：`RealtimeGreedyStrategy.cpp:555-567`

---

## 四、目标保持与切换

### 4.1 目标保持（Target Persistence）

**规则**：AI 选中目标后不会每步切换。只有新最佳目标分数比当前持有目标分数**高出 switchMargin（默认 5.0）**时才切换。防止频繁横跳。

**代码**：`RealtimeGreedyStrategy.cpp:610-618`

---

## 4.2 Pocket-Aware 优先目标

### 启动条件

**同时满足**才触发：
1. 当前位置及其上下左右邻居中，某个 hub 在半径 ≤ `pocketRadius`（默认 2）范围内存在至少 **2 个已知可达未拾取金币**
2. 多个 hub 竞争时选金币数最多的；平局选距离当前更近的

不触发则 `enabled=false`，上层走正常候选评分。

### 算法流程

**Phase 1：识别 Pocket（`findPocket`）**

```
hubCandidates = {当前位置} ∪ {上下左右可通行邻居}
对每个 hub：
  遍历 localMap 中所有已知未拾取金币
  若 BFS(hub → coin) ≤ pocketRadius → coin 属于此 pocket
选最优 hub：优先金币数最多 → 再优先离当前更近
若金币数 < 2 → 不触发
```

**Phase 2：对 pocket 中每个金币计算 Score_first**

```
对 pocket 中每个金币 coin：
  1. baseScore = ΔR(path_to_coin) − η_q × q_eff × len(path_to_coin)
  2. remainI = 对 pocket 中其他金币 remaining：
       max( I_proxy(remaining) / (1 + μ × dist(coin, remaining)) )
     其中 μ = 0.2
  3. Score_first = baseScore + λ_remain × remainI       (λ_remain = 0.2)
```

**Phase 3：选第一目标**

选 `Score_first` 最高的金币。并列时优先路径短的，再按坐标排序。

### 设计动机

附近有金币簇（2+ 金币聚集在 2 步内）时，问题变成"先吃哪个"。如果把 I_proxy 高的金币先吃了，剩下的金币失去探索引子作用；如果把 I_proxy 高的留到最后（作为 remainI），AI 吃完最后一个时仍有探索动力。`remainI` 衡量"留下这个金币能保留多少探索价值"，Score_first 倾向于先吃 I_proxy 低的。

### 输出

```
PocketDecision { enabled, target, path, score, debug { pocketHub, pocketResources[], candidates[] { baseScore, ownIproxy, remainI, scoreFirst }, chosenPocketTarget } }
```

上层收到 `enabled=true` 后直接用 pocket 目标，跳过正常评分和 target hold。

### 参数

| 参数 | 默认值 | 含义 |
|---|---|---|
| `pocketRadius` | 2 | 金币到 hub 最大步数 |
| `pocketMu` | 0.2 | remainI 的距离折扣系数 |
| `pocketLambdaRemain` | 0.2 | remainI 在 Score_first 中的权重 |

### 代码

`PocketAwareGreedy.cpp:139-215`（`choosePocketFirstTarget`）

---

## 4.3 Closed Singleton Lookahead Gate

### 启动条件

**全部满足**才触发：
1. 当前 top-1 候选是 **closed singleton**：`unknownComponentSum == 1`（I_proxy 展开仅找到 1 个未知格）、分数有效、路径存在、不是出口
2. localMap、poseEstimator、evaluator 均可用

不触发时返回原 top-1。

### 算法流程

**Phase 1：确定 A 和 B**

```
A = top-1 候选（必须是 closed singleton，|C|=1）
B = 最高分"有意义非封闭"候选：
    排除 A 自己、排除 closed singleton
    排除死节点（|C|=0 且 dR≤0 且 tail≤0 且不是出口）
若 B 不存在 → A 直接通过
```

**Phase 2：Memory-Only 模拟执行 A**

```
simulateExecuteAWithoutNewVision(A)：
  复制 current localMap 和 state
  沿 A.path 逐格走：
    必须是 walkableForPlanning
    遇 Boss 触发格 → 中止（不模拟 Boss 战）
    结算金币/陷阱（markVisited/Collected/Triggered）
    任一步 resource<0 → 中止
  ⚠️ 不模拟 3×3 视野更新（保守设计）
  返回 (localMap_after, state_after)
```

**Phase 3：计算 c_A（A 之后的最优后继 reward）**

```
在 localMap_after 上：
  重算出口路径（若出口已知）
  遍历已观察格，排除：
    A 自身、出口、A 周围 3×3 区域（视野未模拟）、已访问格、不可通行格
  对每个候选跑 evaluate() → 取最高分 = c_A
  排除 continuation 的 |C|=0 且 dR≤0 且 tail≤0 的死节点
```

**Phase 4：非对称比较**

```
combinedA = reward(A) + γ × c_A           (γ = 1.0)
threshold = reward(B) + margin             (margin = −20)

若 combinedA > threshold → A 通过，选 A
否则 → A 被拒绝，改选 B
```

> 非对称：只算 A 的 continuation（c_A），不算 B 的（c_B）。A 是 closed singleton（周围已知，可安全模拟），B 是开放候选（模拟会引入不可靠的未知区域预测）。

### 设计动机

Top-1 候选是"封闭小未知块"（走到它只能打开 1 个新格子，直接收益接近零）。只看眼前 score 会跳过它选 B。但 A 可能是一扇门——走到 A 后可以从 A 去更好的后继目标。Gate 模拟"走过 A 之后的世界"，判断 A 是否值得。

### 输出

```
ClosedSingletonGateResult {
    hasSelection, changed, selectedTarget, selectedPath, selectedScore
    debug { checked, triggered, candidateA, componentSizeA(=1), rewardA,
            bestNonClosedB, rewardB, simulatedAfterA, bestAfterATarget,
            bestAfterAReward, combinedA, allowed, reason }
}
```

### 在 selectBestPath 中的位置

主候选评分循环之后、shouldGoExit 之前。若 gate 改变选择（`changed=true`），重算 worthwhileTarget。

### 参数

| 参数 | 默认值 | 含义 |
|---|---|---|
| `γ_closed` | 1.0 | c_A 权重（combinedA = reward(A) + γ×c_A） |
| `margin_closed` | −20 | 负值=宽松：combinedA 可比 reward(B) 最多低 20 分仍通过 |

### 代码

`ClosedSingletonLookaheadGate.cpp:265-343`
调用处：`RealtimeGreedyStrategy.cpp:536-568`

---

## 五、信息价值 I_proxy

### 5.1 面积上限的分阶段控制

**规则**：`activeAreaCap` 根据出口状态变化：

| 出口状态 | areaCap | 含义 |
|---|---|---|
| 未知或不可达 | `A_max=12` | 鼓励探索，开阔区域得到更高面积奖励 |
| 已知可达 | `knownExitAreaCap=1.5` | 削弱开阔区域奖励，不再鼓励大规模探索 |

**代码**：`Reward.cpp:989-991`

---

### 5.2 Boss-Gated 区域特殊处理

**规则**：如果候选目标位于"必须踏过 Boss 才能到达"的区域（Boss-gated），并且该目标的未知延伸能触达迷宫边缘，`I_proxy` 的 |C| 按 `bossEdgeAreaBonus=15`（而不是普通 areaMax=12）计算，以表达"通关推进"的价值。

**代码**：`Reward.cpp:847-848`

---

### 5.3 面积价值密度

**规则**：每个未知格的价值不是常数 1，而是乘以 `ρ_area_value`：

```
ρ_area_value = clip(max(v_area, 0) / 50, 0.001, 1)
v_area = 50 × ρ̂_G − 30 × ρ̂_T
```

如果 AI 观察到的区域里陷阱密度远高于金币密度，未知区域的价值密度会被压低，减少继续探索的动力。

**代码**：`RewardConfig.h:17-21`

---

### 5.4 迷宫外部格不算未知

**规则**：`MapPoseEstimator` 维护一个估计的 15×15 掩码。掩码外部的格子不计入 `I_proxy` 的连通块大小。AI 通过出生点的 3×3 视野判断自己在迷宫边缘还是内部，逐步确定掩码位置。

**规则**：掩码不会无条件完整启用——只有局部记忆的横向或纵向跨度达到 15 格后才启用完整掩码。

**代码**：`Reward.cpp:454-514`, `MapPoseEstimator`

---

## 六、动态探索权重 α

### 6.1 计算公式

```
ρ̂_G = (N_G + 1.0) / (N_obs + 10.0)
ρ̂_T = (N_T + 1.0) / (N_obs + 10.0)
v_unk = 50 × ρ̂_G − 30 × ρ̂_T
ρ_U = N_unknown / 225

α_raw = 4.0 × (1 + 1.0×ρ_U + 1.0×max(v_unk,0)/50) / (1 + 2.0×ρ̂_T)
α_t   = clip(α_raw, 1.0, 8.0)
α_smooth = 0.8 × α_{t−1} + 0.2 × α_t
```

- 未知区域越大 → α 越高
- 观察到的陷阱越多 → α 越低
- Boss 不再作为风险密度（已移出公式）
- EMA 平滑系数 0.8，防止早期样本少时剧烈震荡

**代码**：`Reward.cpp:711-736`, `RewardConfig.h:28-35`

---

## 七、完整评分公式

```
Score(t) = ΔR_real(path_t)
         + ω_I × α_smooth × I_proxy(t)         ; ω_I = 0.175
         + β × V_tail^marg(t)                   ; β = 1.23
         − η_q × q_eff × len(path_t)             ; η_q = 0.82
         − φ_margin(R + ΔR_real)

I_proxy(t) = κ_u × Σ_C min(|C|, activeCap) × ρ_area_value    ; κ_u = 60

φ_margin(r) = 8.0 × (max(0, 30−r) / 30)²

q_eff = max(q_ref, 1.0)
q_ref = 出口已知可达? (R+ΔR_exit)/(L+len_exit+ε) : R/(L+ε)
```

所有参数集中在 `RewardConfig.h`。

---

## 八、路由算法

### 8.1 reward 选目标 + 路由器走路径

**规则**：`MemoryGreedyAgent` 的评分公式选目标，选定后由 `routeAlgorithm_` 指定的算法搜索从当前位置到目标的局部路径。

支持的 routeAlgorithm：
- `"greedy"` / `"smart"` → BFS on localMap（Reward::shortestPathOnKnownMap）
- `"dijkstra"` → dijkstraPath on localMap
- `"astar"` → astarPath on localMap
- `"branch_bound"` → branchBoundPath on localMap
- `"divide_conquer"` → divideConquerPath on localMap

默认使用 `"greedy"`（BFS）。

**代码**：`RealtimeGreedyStrategy.cpp:443-452`

---

## 九、前端结果构建（buildResult）

### 9.1 Boss 战 JSON 内嵌

`buildResult` 在构建输出 JSON 时会**再次调用** `runBossBattleJson`（除了 Agent 构造时的那次）。Boss 战结果被嵌入每个 frame 的 events 数组中。

### 9.2 Game Over 截断

如果 `gameOver=true`，`buildResult` 的 frames 循环会 `break`，路径在 Game Over 处截断。

### 9.3 debug 信息附加

`attachGreedyDebug` 把每一步的候选评分、Closed Singleton Gate、Pocket 信息都写入 frame 的 `debug` 字段。

**代码**：`AIPlayerEngine.cpp:258-316`

---

## 十、MazeData 字段说明

| 字段 | 类型 | 说明 |
|---|---|---|
| `maze` | string[][] | 15×15 迷宫，支持 S/E/G/T/B/#/ /L |
| `B` | int[] | 各 Boss 血量，按顺序揭示 |
| `PlayerSkills` | int[][] | 技能列表 `[damage, cooldown]` |
| `minRounds` / `minRouds` | int | 总回合限制（兼容拼写错误） |
| `CoinConsumption` | int | 复活所需金币，0 表示不可复活 |
| `C` | int[][] | 密码线索（可选） |
| `L` | string | 密码 SHA-256 哈希（可选） |

---

## 十一、参数快速索引

| 参数 | 值 | 位置 | 含义 |
|---|---|---|---|
| `ω_I` | 0.175 | Score 主式 | 探索信息折扣 |
| `α_0` | 4.0 | α_raw 分子 | 基准探索强度 |
| `α_min/max` | 1.0 / 8.0 | clip | 探索权重边界 |
| `β` | 1.23 | Score 主式 | 尾部金币权重 |
| `κ_u` | 60 | I_proxy | 连通块面积权重 |
| `A_max` | 12 | I_proxy | 出口未知时面积上限 |
| `knownExitAreaCap` | 1.5 | I_proxy | 出口已知后面积上限 |
| `bossEdgeAreaBonus` | 15 | I_proxy | Boss-gated 区域面积加成 |
| `η_q` | 0.82 | 路径长度项 | 长度代价缩放 |
| `q_min` | 1.0 | q_eff | 路径代价下限 |
| `m_safe` | 30 | φ_margin | 安全资源裕量 |
| `λ_m` | 8.0 | φ_margin | barrier 强度 |
| `τ` | 5.0 | 停止探索 | 停止阈值 |
| `switchMargin` | 5.0 | 目标保持 | 切换阈值 |
| `θ` | 0.8 | α 平滑 | EMA 系数 |
| `γ_closed` | 1.0 | ClosedSingleton | 前瞻权重 |
| `margin_closed` | −20 | ClosedSingleton | 拒绝保守度 |
| `pocketRadius` | 2 | Pocket | 金币簇半径 |
| `λ` | 10.0 | 密度估计 | 平滑伪计数 |
