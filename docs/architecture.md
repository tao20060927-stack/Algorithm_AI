# AI Player Desktop 架构文档

本文档面向**从未阅读过此代码**的开发者，目标是仅凭本文档即可完整复现项目。

---

# 第一章：项目文件树与职责

```
AIPlayerDesktop/
├── src/
│   ├── GameTypes.h           基础类型 (Position (GameTypes.h:13), MazeData (GameTypes.h:28), Skill (GameTypes.h:21), passable (GameTypes.h:38), kDirs (GameTypes.h:18))
│   ├── RewardConfig.h         全部 22 个可调参数集中管理 (RewardConfig.h:1)
│   ├── Reward.h/.cpp          奖励函数全系统 (LocalCell (Reward.h:54), LocalKnownMap (Reward.h:65),
│   │                           MapPoseEstimator (Reward.h:100), PathValueEvaluator (Reward.h:149))
│   ├── RealtimeGreedyStrategy.h/.cpp  贪心 AI 本体 (MemoryGreedyAgent (RealtimeGreedyStrategy.cpp:22))
│   ├── AIPlayerEngine.h/.cpp  顶层引擎 (解析 JSON, 调度算法, 构建输出)
│   ├── BossStrategy.h/.cpp     Boss 战分支限界求解
│   ├── PocketAwareGreedy.h/.cpp      局部金币簇优先策略
│   ├── ClosedSingletonLookaheadGate.h/.cpp  闭单例前瞻门控
│   ├── ShortestPathStrategy.h/.cpp  算法调度 + smart 全局贪心探险
│   ├── AStarStrategy.h/.cpp         A* (两个重载: astarPath (AStarStrategy.cpp:26), astarPath (AStarStrategy.cpp:106))
│   ├── DijkstraStrategy.h/.cpp      Dijkstra (两个重载: dijkstraPath (DijkstraStrategy.cpp:25), dijkstraPath (DijkstraStrategy.cpp:97))
│   ├── BranchBoundStrategy.h/.cpp   分支限界路由 (两个重载: branchBoundPath (BranchBoundStrategy.cpp:131), branchBoundPath (BranchBoundStrategy.cpp:150))
│   ├── DivideConquerStrategy.h/.cpp 分治双向 BFS (两个重载: divideConquerPath (DivideConquerStrategy.cpp:170), divideConquerPath (DivideConquerStrategy.cpp:189))
│   ├── ResourcePickupStrategy.h/.cpp PDF 第一问 3x3 拾取
│   ├── DesktopApp.cpp              WebView2 桌面 GUI
│   ├── SmokeTest.cpp               命令行冒烟测试
│   └── GreedyEval.cpp              批量评测入口
├── docs/                            文档
├── tools/                           批处理/调试脚本
├── vendor/                          第三方 (EventToken.h)
├── data/                            迷宫数据
└── build/                           CMake 编译输出
```

---

# 第二章：两套独立的算法路径

项目同时维护两套**互不调用、互不共享状态**的路径规划系统：

## 2.1 路径对比

| | 路径 A：Reward 贪心 | 路径 B：全局最近邻 |
|---|---|---|
| 引擎入口 | `RunRealtimeGreedy(json)` (AIPlayerEngine.cpp:319) 或 `RunAdventure(json, "astar")` (AIPlayerEngine.cpp:350) | `RunAdventure(json, "smart")` (AIPlayerEngine.cpp:350) |
| 主循环 | `MemoryGreedyAgent::run()` (RealtimeGreedyStrategy.cpp:68) | `planAdventurePath()` (ShortestPathStrategy.cpp:50) |
| 信息获取 | 3×3 局部视野逐步观察 → `localMap_` | 直接读取 `data.grid` 15×15 全量 |
| 目标选择 | reward 公式评分 | 曼哈顿距离最近 |
| 路径规划 | `routePath()` (RealtimeGreedyStrategy.cpp:495) 在 `localMap_` 上 BFS | `astarPath()` (AStarStrategy.cpp:26) 在 `data.grid` 上 A\* |
| 是否使用 reward | 是，全套 `evaluate()` (Reward.cpp:1079) | 否 |

## 2.2 路由分发

两套路径在 `AIPlayerEngine::RunAdventure()` 中根据 `algorithm` 参数分叉：

```cpp
// AIPlayerEngine.cpp:361-368
if (isRewardRouterAlgorithm(algorithm)) {  // "dijkstra"/"astar"/"branch_bound"/"divide_conquer"
    -> realtimeGreedyRun(data, algorithm)  // 路径 A — reward选目标, 指定路由器
} else {
    -> planAdventurePath(data, algorithm)  // 路径 B — "smart" 全局最近邻
}
```

`"greedy"` 不经过 `RunAdventure`，直接从 `RunRealtimeGreedy` 进入。

---

# 第三章：路径 A — Reward 贪心

## 3.1 引擎层：`AIPlayerEngine::RunRealtimeGreedy`

### 3.1.1 入口函数完整流程

```
RunRealtimeGreedy(inputJson: string) → string (JSON)    // AIPlayerEngine.cpp:319
│
├─ [步骤1] makeRunCacheKey("greedy", inputJson)         // AIPlayerEngine.cpp:67
│   │  将算法名 + 规范化 JSON (先 parse 再 dump) 拼接为缓存键
│   │  规范化消除空白差异，同一输入必定命中缓存
│   │  调用: Json::parse(inputJson).dump()
│   │
│   ├─ 查 resultCache_ (AIPlayerEngine.h:27) → 命中? return 缓存值 ────────→ 函数返回
│   │
│   └─ 未命中? 继续 ↓
│
├─ [步骤2] parseMaze(inputJson) → MazeData             // AIPlayerEngine.cpp:72
│   │
│   ├─ Json::parse(inputJson) → json source
│   ├─ 校验: source["maze"] 存在且是非空 2D 数组
│   │
│   ├─ 获取 rows=15, cols=15
│   ├─ grid.assign(15, vector<string>(15))
│   │
│   └─ for r=0..14, c=0..14:
│       ├─ tile = maze[r][c].get<string>()
│       ├─ 合法性校验: tile 是否 ∈ {"#"," ","S","E","G","T","L","B"}
│       ├─ grid[r][c] = tile
│       ├─ tile=="S" → start = {r,c}
│       ├─ tile=="E" → exit  = {r,c}
│       ├─ tile=="B" → bosses.push_back({r,c})
│       └─ tile=="G" → golds.push_back({r,c})
│       └─ source 指向原始 JSON (Boss 战等子模块从这里读取 B/PlayerSkills)
│
├─ [步骤3] realtimeGreedyRun(data) → GreedyRunResult {path, debugSteps, gameOver}  // RealtimeGreedyStrategy.cpp:916
│   │  (详见 §3.2)
│   │
├─ [步骤4] buildResult(data, run.path, "realtime-greedy") → Json    // AIPlayerEngine.cpp:258
│   │  (详见 §3.6)
│   │
├─ [步骤5] attachGreedyDebug(result, run) → void                    // AIPlayerEngine.cpp:225
│   │  (详见 §3.7)
│   │
└─ [步骤6] resultCache_[key] = result.dump()
    return result.dump()
```

## 3.2 Agent 构造：`MemoryGreedyAgent::MemoryGreedyAgent` (RealtimeGreedyStrategy.cpp:37)

### 3.2.1 构造函数执行步骤

```
MemoryGreedyAgent(data, routeAlgorithm="greedy", parameters=默认)  // RealtimeGreedyStrategy.cpp:37
│
├─ [1] maze_(data)                     // 持有 MazeData 引用
├─ [2] routeAlgorithm_ = "greedy"      // 默认 BFS 路由
├─ [3] evaluator_(parameters)          // 用默认 RewardParameters 初始化评分器 (Reward.h:21)
│
├─ [4] poseEstimator_.initialize()     // Reward.cpp:343
│   └─ hypotheses_.clear()
│   └─ for offset in 5..9:             // 每条边中心 5 个偏移
│       ├─ {entry={0,offset}, inwardDirection=Down, score=0, feasible=true}
│       ├─ {entry={14,offset}, inwardDirection=Up, score=0, feasible=true}
│       ├─ {entry={offset,0}, inwardDirection=Right, score=0, feasible=true}
│       └─ {entry={offset,14}, inwardDirection=Left, score=0, feasible=true}
│   └─ best_ = hypotheses_[0]          // 默认取第一个
│   └─ observedEstimated_.clear()
│   └─ maskSeeded_ = false, maskActive_ = false
│
├─ [5] bossBattleResult_ = runBossBattleJson(maze_.source)  // BossStrategy.cpp:703 (详见 §第八章)
│   ├─ 解析 B[] (Boss HP), PlayerSkills[][2], minRounds, CoinConsumption
│   ├─ 顺序揭示 + 分支限界 → 返回 {ok, sequence, turns, withinMinRounds, ...}
│   └─ 存入 bossBattleResult_ (Json 对象)
│
├─ [6] bossBattleCanWin_ = bossBattleResult_["ok"]
│   └─ 如果有 "withinMinRounds" 字段 → 需同时满足 withinMinRounds==true
│
└─ [7] coinConsumption_ = bossBattleResult_["CoinConsumption"]
    └─ 默认 0 (无复活机制)
```

### 3.2.2 Agent 成员变量清单

```
const MazeData& maze_;                // 真实迷宫 (3x3 视野的真相来源)
string routeAlgorithm_;               // 路由器选择
LocalKnownMap localMap_;              // AI 记忆 (仅已观察格)
MapPoseEstimator poseEstimator_;      // 估计 AI 在 15x15 的位置
PathValueEvaluator evaluator_;        // 评分器
AgentState state_;                    // {resource, steps, collectedGold, alphaSmooth}

map<Position,Position> localToReal_;  // 局部→真实
map<Position,Position> realToLocal_;  // 真实→局部
vector<Position> knownBosses_;        // 已发现的 Boss 局部坐标
vector<bool> defeatedBosses_;         // 对应 Boss 是否已击败
Json bossBattleResult_;               // Boss 战预计算结果
bool bossBattleCanWin_;               // 能否在限定回合内击败
int coinConsumption_;                 // 复活金币消耗
bool pendingRevive_ = false;          // 是否待复活
bool gameOver_ = false;               // 是否 GAME OVER
Position localExit_ = kInvalid;       // 出口局部坐标
Position currentTarget_;              // 当前保持的目标
double currentTargetScore_;           // 该目标的分数
bool currentTargetFromPocket_;        // 当前目标是否来自 Pocket
```

## 3.3 主循环：`MemoryGreedyAgent::run()`

### 3.3.1 循环前初始化

```
run() → GreedyRunResult {path, debugSteps, gameOver}
│
├─ realCurrent = maze_.start       // 真实坐标从迷宫 S 开始
├─ localCurrent = {0, 0}          // 局部坐标始终 (0,0) 为起点
├─ localToReal_[{0,0}] = start    // 建立 (0,0)→真实→(0,0) 映射
├─ realToLocal_[start] = {0,0}
├─ result.path.push_back(start)   // 路径以起点开始
└─ maxSteps = 15 × 15 × 4 = 900   // 安全上限
```

### 3.3.2 单步循环体 (900 次上限)

```
for step=0; step<900 && realCurrent≠exit; step++:
│
├─────────────────────────────────────────────────────────────
│ [步骤 1] updateKnownMap(realCurrent, localCurrent)          §3.3.3
│          扫描真实迷宫 3×3 → 写入 localMap_
├─────────────────────────────────────────────────────────────
│ [步骤 2] applyCurrentCell(localCurrent)                     §3.3.4
│          结算资源、触发 Boss、标记访问
├─────────────────────────────────────────────────────────────
│ [步骤 3] poseEstimator_.update(localMap_, localCurrent)     §3.3.5
│          更新入口假设评分，检测掩码激活
├─────────────────────────────────────────────────────────────
│ [步骤 4] state_.alphaSmooth =                               §3.3.6
│           evaluator_.updateAlphaSmooth(prevAlpha,            §3.3.6
│                                        localMap_, est)
├─────────────────────────────────────────────────────────────
│ [步骤 5] if pendingRevive_ || gameOver_:                    §3.3.7
│          处理 Boss 战后果 (复活/结束)
├─────────────────────────────────────────────────────────────
│ [步骤 6] selectedPath = selectBestPath(...)                 §3.4
│          核心决策：reward 评分 → 选最佳目标 → 返回路径
├─────────────────────────────────────────────────────────────
│ [步骤 7] 执行一步
│          nextLocal = selectedPath[1]      (只走 1 步!)
│          localToReal_[nextLocal] → 更新 current
│          path.push_back(realCurrent)
```

### 3.3.3 updateKnownMap — 3×3 视野到局部记忆

```
updateKnownMap(realCurrent, localCurrent)
│
├─ for dr = -1..1, dc = -1..1:                          // 3×3 = 9 格
│   │
│   ├─ realPos = (real.r+dr, real.c+dc)
│   ├─ localPos = (local.r+dr, local.c+dc)
│   │
│   ├─ realInBounds(realPos)?                            // 检查是否在 15×15 内
│   │   │
│   │   ├─ YES (在迷宫内):
│   │   │   ├─ tile = maze_.grid[realPos]               // 读真实迷宫
│   │   │   │
│   │   │   ├─ bossIndex = bossIndexAt(localPos)         // 查已知Boss列表
│   │   │   │   └─ 遍历 knownBosses_: pos匹配? return index; 否则 -1
│   │   │   │
│   │   │   ├─ tile=="B" && bossIndex>=0 && defeatedBosses_[idx]?
│   │   │   │   └─ tile = " "                           // 已击败Boss→空地
│   │   │   │
│   │   │   ├─ localMap_.setObserved(localPos, tile)
│   │   │   │   └─ cells_[localPos] = {observed=true, outside=false, tile=tile}
│   │   │   │
│   │   │   ├─ tile=="B" && bossIndex<0?                // 新Boss
│   │   │   │   ├─ knownBosses_.push_back(localPos)
│   │   │   │   ├─ defeatedBosses_.push_back(false)
│   │   │   │   └─ localMap_.markBossTriggers(localPos)
│   │   │   │       └─ for {dr,dc} in kDirs:
│   │   │   │           cells_[{boss.r+dr,boss.c+dc}].bossTrigger = true
│   │   │   │
│   │   │   └─ tile=="E" → localExit_ = localPos         // 记录出口位置
│   │   │
│   │   └─ NO (超出边界):
│   │       └─ localMap_.setOutside(localPos)
│   │           └─ cells_[localPos] = {observed=true, outside=true, tile="#"}
│   │
│   ├─ localToReal_[localPos] = realPos                  // 始终更新双射
│   └─ realToLocal_[realPos] = localPos
```

### 3.3.4 applyCurrentCell — 资源结算

```
applyCurrentCell(localCurrent)
│
├─ localMap_.markVisited(localCurrent)
│   └─ cells_[localCurrent].visited = true
│
├─ if state_.steps > 0:                                   // 第0步起点不结算
│   │
│   ├─ tile = localMap_.tile(localCurrent)                // 从记忆读类型
│   │
│   ├─ tile=="G" && !localMap_.isCollected(localCurrent)?
│   │   ├─ state_.resource += 50
│   │   ├─ state_.collectedGold++
│   │   └─ localMap_.markCollected(localCurrent)          // cells_[pos].collected=true
│   │
│   └─ tile=="T" && !localMap_.isTriggered(localCurrent)?
│       ├─ state_.resource -= 30                          // (kTrapValue = -30)
│       └─ localMap_.markTriggered(localCurrent)          // cells_[pos].triggered=true
│
├─ triggerAdjacentBoss(localCurrent)
│   └─ for i=0..knownBosses_.size()-1:
│       ├─ dist = |current.r-boss.r| + |current.c-boss.c|
│       └─ if dist==1 && !defeatedBosses_[i]:              // 正邻接且未击败
│           ├─ if bossBattleCanWin_:
│           │   ├─ defeatedBosses_[i] = true
│           │   └─ localMap_.clearBoss(knownBosses_[i])
│           │       └─ cells_[boss] = {observed=true, tile=" "} // 变空地
│           ├─ else if state_.resource >= coinConsumption_:
│           │   ├─ state_.resource -= coinConsumption_
│           │   └─ pendingRevive_ = true
│           └─ else:
│               └─ gameOver_ = true
│
└─ state_.steps++
```

### 3.3.5 poseEstimator_.update — 估计迷宫位置

```
MapPoseEstimator::update(localMap, localCurrent)
│
├─ [Phase A] 种子类型检测 (seedKind_)
│   若 maskSeeded_==false:
│   │
│   ├─ 检查出生点 3×3 的 setOutside 标记
│   ├─ 上方三格都是 outside? → seedKind_ = Top
│   ├─ 下方三格都是 outside? → seedKind_ = Bottom
│   ├─ 左侧三格都是 outside? → seedKind_ = Left
│   ├─ 右侧三格都是 outside? → seedKind_ = Right
│   ├─ Top+Left? → TopLeft (依此类推组合)
│   └─ 否则 → Internal (迷宫内部出生)
│   └─ maskSeeded_ = true
│
├─ [Phase B] 边界一致性校验
│   对每个 hypothesis:
│   │
│   ├─ for pos in localMap.observedPositions():
│   │   └─ mapped = mapWithHypothesis(pos, hyp)
│   │       │
│   │       ├─ 掩码活跃 (maskActive_):
│   │       │   └─ mapped 不在 15×15 内? → hypothesis.feasible=false, break
│   │       │
│   │       └─ 掩码未活跃:
│   │           └─ 根据 seedKind_ 做边界检查:
│   │               ├─ Top/Bottom: mapped.row 越界? → infeasible
│   │               └─ Left/Right:  mapped.col 越界? → infeasible
│   │
│   ├─ 如果 feasible 仍为 true:
│   │   ├─ edgeTouches = 统计 mapped 触及边缘的格子数
│   │   ├─ centerDist = |mappedCurrent.row-7|+|mappedCurrent.col-7|
│   │   └─ hypothesis.score = -0.15 × edgeTouches − 0.05 × centerDist
│   │       (越靠近中心、越少触边 → 分数越高)
│   └─ if hypothesis.score > bestScore:
│       └─ best_ = hypothesis, observedEstimated_ = estimatedSet
│
├─ [Phase C] 候选掩码排除 (边缘出生 + 记忆跨度≥14)
│   └─ 用已观察到的墙/空地模式排除不可能入口位置 (候选只剩 1 时提前激活掩码)
│
└─ [Phase D] 掩码激活检测
    └─ 局部记忆横向或纵向 span ≥ 15? → maskActive_ = true
```

### 3.3.6 updateAlphaSmooth — 动态探索权重

```
updateAlphaSmooth(prevAlpha, localMap, poseEstimator) → double
│
├─ [1] 统计已观察区域资源分布
│   for pos in localMap.observedPositions():
│   │   ├─ observedCount++
│   │   ├─ tile=="G" → goldCount++
│   │   └─ tile=="T" → trapCount++
│   │
├─ [2] 贝叶斯平滑密度估计
│   ├─ ρ̂_G = (goldCount + 1.0) / (observedCount + 10.0)
│   └─ ρ̂_T = (trapCount + 1.0) / (observedCount + 10.0)
│   └─ 分子+1 / 分母+10 保证早期(观察少)密度不被极端值主导
│
├─ [3] 未知区域占比
│   ├─ N_unknown = poseEstimator.estimatedUnknownCount()
│   │   └─ 掩码活跃? 225 - observedEstimated_.size()
│   │   └─ 否则? 返回偏大估计 (鼓励早期探索)
│   └─ ρ_U = N_unknown / 225
│
├─ [4] 未知区期望净值
│   └─ v_unk = 50 × ρ̂_G − 30 × ρ̂_T
│       └─ 正数 → 金币密度高于陷阱密度 → 值得探索
│       └─ 负数 → 陷阱风险大 → 抑制探索
│
├─ [5] 计算 α_raw
│   └─ raw = 4.0 × (1 + 1.0×ρ_U + 1.0×max(v_unk,0)/50)
│            / (1 + 2.0×ρ̂_T)
│   │
│   │   解释:
│   │   ├─ ρ_U 大 → 分子大 → α高 → 鼓励探索
│   │   ├─ v_unk 正 → 分子大 → α高 → 鼓励探索
│   │   └─ ρ̂_T 大 → 分母大 → α低 → 抑制探索
│   │
├─ [6] Clip + EMA 平滑
│   ├─ clipped = clamp(raw, 1.0, 8.0)
│   └─ return 0.8 × prevAlpha + 0.2 × clipped
│       └─ θ=0.8 保证 α 缓慢变化 (单步最多变 ±20%)
```

### 3.3.7 Boss 特殊状态处理

```
if pendingRevive_ || gameOver_:
│
├─ fillStatusDebug(stepDebug)  // "boss-revive-reset" 或 "boss-game-over"
│
├─ if pendingRevive_:
│   ├─ pendingRevive_ = false        // 重置标志
│   ├─ localCurrent = {0,0}          // 传回起点
│   ├─ realCurrent = maze_.start
│   ├─ currentTarget_ = kInvalid     // 清当前目标
│   ├─ currentTargetScore_ = -∞
│   ├─ currentTargetFromPocket_ = false
│   ├─ result.path.push_back(start)  // 新一段从起点开始
│   └─ continue                      // 跳过本帧→下轮重新决策
│
└─ if gameOver_:
    ├─ result.gameOver = true
    └─ break                          // 退出主循环
```

---

## 3.4 核心决策：`selectBestPath()`

这是整个系统最复杂的函数，每步调用一次，决定 "下一步往哪里走"。

### 3.4.1 完整调用流程

```
selectBestPath(localCurrent, realCurrent, step, debug) → vector<Position>
│
├──────────────────────────────────────────────────────────────────
│ [Phase 0] 准备评分上下文                                       │
├──────────────────────────────────────────────────────────────────
│
├─ context = buildContext(localCurrent)
│   │
│   ├─ context.state = state_                        // 拷贝当前资源/步数
│   │
│   ├─ if localExit_ ≠ kInvalid:
│   │   └─ context.exitPath = routePath(localCurrent, localExit_)
│   │       └─ 从当前到出口在 localMap_ 上的路径 (不可达→空)
│   │   └─ else: context.exitPath = {}               // 出口未发现
│   │
│   └─ context.bossGatedAreaMaxTargets = bossGatedAreaMaxTargets(localCurrent)
│       │
│       └─ [子函数] bossGatedAreaMaxTargets:
│           ├─ [Step A] BFS from current, 视所有Boss为墙
│           │   → reachableWithoutBoss = 不穿过Boss即可到达的已知格集合
│           │
│           ├─ [Step B] 对每个"可从当前侧邻接到的 Boss":
│           │   BFS from boss (穿过它) → throughBoss
│           │   gated = throughBoss \ reachableWithoutBoss
│           │
│           └─ return gated  // 必须穿Boss才能到达的区域
│
├─ debug.qEff = evaluator_.computeQEff(context, localMap_)
│   │
│   ├─ q_ref = 出口可达? (R + exitΔR) / (L + exitLen + ε)
│   │          : R / (L + ε)
│   │   └─ exitΔR = pathResourceDelta(exitPath) — 路上能捡多少资源
│   │
│   └─ return max(q_ref, 1.0)    // 保证 R=0 时也有基础代价
│
├─ debug.observedRatio = poseEstimator_.estimatedObservedCount() / 225.0
│
├──────────────────────────────────────────────────────────────────
│ [Phase 1] 生成候选集 — candidateTargets(localCurrent)           │
├──────────────────────────────────────────────────────────────────
│
│   for pos in localMap_.observedPositions():
│   │
│   ├─ pos == localCurrent? ────────────→ skip
│   ├─ localMap_.isVisited(pos)? ───────→ skip  (已站过 → 不能作为目标)
│   ├─ !localMap_.isWalkableForPlanning(pos)? → skip
│   │   └─ 定义: observed=true && tile≠"#" && tile≠"B"
│   └─ routePath(localCurrent, pos).empty()? → skip (不可达)
│   └─ 全部通过 → targets.push_back(pos)
│
│   → targets = 所有当前可到达的未访问已观察可通行格
│
├──────────────────────────────────────────────────────────────────
│ [Phase 2] 记录被拒绝候选                                        │
├──────────────────────────────────────────────────────────────────
│
├─ recordRejectedTargets(localCurrent, debug)
│   │
│   └─ for pos in observedPositions() (全部已观察格):
│       ├─ pos == localCurrent?
│       │   └─ rejected { reason="current" }          // 就是自己
│       ├─ isVisited(pos)?
│       │   └─ rejected { reason="visited" }          // 已踩过
│       ├─ !isWalkableForPlanning(pos)?
│       │   └─ rejected { reason="blocked" }          // 墙/Boss
│       ├─ routePath(current, pos).empty()?
│       │   └─ rejected { reason="unreachable" }      // 无路可达
│       └─ 通过? → 此格已在 candidates 中, 不进入 rejected
│
├──────────────────────────────────────────────────────────────────
│ [Phase 3] 对每个候选独立评分                                    │
├──────────────────────────────────────────────────────────────────
│
│ for target in targets:
│   │
│   ├─ [3.1] path = routePath(localCurrent, target)
│   │   │
│   │   └─ 路由器分发 (详见第五章):
│   │       ├─ "greedy"/"smart" → BFS on localMap_
│   │       ├─ "dijkstra" → priority_queue Dijkstra
│   │       ├─ "astar"    → A* with Manhattan heuristic
│   │       ├─ "branch_bound" → DFS + lower bound prune
│   │       └─ "divide_conquer" → bidirectional BFS meet
│   │
│   ├─ [3.2] score = evaluator_.evaluate(path, target, context,
│   │                                     localMap_, poseEstimator_)
│   │   │                                 (详见 §3.5)
│   │   │
│   ├─ [3.3] 提取各分项填充 debug:
│   │   ├─ item.deltaR     = pathResourceDelta(path, localMap)
│   │   ├─ item.informationProxy = informationProxy(target, ...)
│   │   ├─ item.tailGain   = futureGainMarginal(target, path, localMap)
│   │   ├─ item.projectedResource = state_.resource + item.deltaR
│   │   ├─ item.marginPenalty = marginPenalty(projectedResource)
│   │   └─ item.unknownComponents = unknownComponentSizesTouchingView(...)
│   │
│   ├─ [3.4] 更新最佳:
│   │   ├─ score > bestScore → bestScore=score, bestTarget=target
│   │   │
│   │   ├─ !context.exitPath.empty()? → 检查 worthwhileTarget:
│   │   │   ├─ targetToExit = routePath(target, localExit_)
│   │   │   ├─ detourCost = len(current→target)
│   │   │   │            + len(target→exit)
│   │   │   │            − len(current→exit)
│   │   │   └─ score > debug.qEff × detourCost?
│   │   │       └─ hasWorthwhileTarget = true
│   │   │
│   │   └─ projectedR ≥ 0? → hasNonNegativeTarget = true
│   │
│   └─ [3.5] 构建 ClosedSingletonGate 候选:
│       └─ gateCandidates.push_back({target, score, deltaR, Iproxy, ...})
│
├──────────────────────────────────────────────────────────────────
│ [Phase 4] Closed Singleton Gate                                  │
├──────────────────────────────────────────────────────────────────
│
├─ gateResult = applyClosedSingletonLookaheadGate(request)
│   │                                          (详见 §3.9)
│   │
│   └─ gateResult.hasSelection?
│       ├─ bestTarget = gateResult.selectedTarget
│       ├─ bestPath   = gateResult.selectedPath
│       ├─ bestScore  = gateResult.selectedScore
│       │
│       └─ gateResult.changed? (Gate 改选了 B)
│           └─ 用非 A 候选重新计算 hasWorthwhileTarget
│               └─ for candidate in gateCandidates (排除 A):
│                   ├─ targetToExit = routePath(candidate.target, exit)
│                   ├─ detourCost = len + len(targetToExit) − exitLen
│                   └─ score > qEff × detourCost? → hasWorthwhile=true
│
├──────────────────────────────────────────────────────────────────
│ [Phase 5] 决策优先级链                                          │
├──────────────────────────────────────────────────────────────────
│
│ ╔══════════════════════════════════════════════════════════════╗
│ ║ 优先级 1: 停止探索 → 转向出口                               ║
│ ╚══════════════════════════════════════════════════════════════╝
│
├─ shouldGoExit(context, bestScore, hasWorthwhile, hasNonNegative)?
│   │
│   ├─ exitPath 空或长度≤1? → return false     // 出口还没找到
│   │
│   ├─ R==0 && hasNonNegativeTarget? → false    // 禁止空手离场
│   │   └─ "至少有一个候选走完资源不为负, 不能空手走"
│   │
│   ├─ ratio = R / (L+ε)
│   │   └─ ratio < 1.0 && collectedGold < 3 && bestScore > −∞?
│   │       └─ return false                     // 早期的低 ratio 保护
│   │
│   ├─ bestScore ≤ 5.0? → return true           // tau=5.0 阈值
│   │   └─ "没有候选能带来显著收益"
│   │
│   └─ !hasWorthwhileTarget? → return true
│       └─ "所有候选都不值得绕路"
│
│   └─ 是 → 设置决策为 "exit", 返回 context.exitPath
│
│ ╔══════════════════════════════════════════════════════════════╗
│ ║ 优先级 2: 目标保持 (Hold Target)                            ║
│ ╚══════════════════════════════════════════════════════════════╝
│
├─ 当前有 heldTarget 且仍可达?
│   ├─ heldPath = routePath(current, heldTarget)
│   ├─ heldScore = evaluate(heldPath, heldTarget, context, ...)
│   │
│   ├─ Pocket 第一目标? → §3.8
│   │   └─ pocket enabled? → 设置决策为 "pocket-first-target"
│   │       └─ return pocket.path ──────────→ 函数返回
│   │
│   ├─ bestScore > heldScore + 5.0?            // switchMargin
│   │   ├─ 否 → 保持旧目标
│   │   │   └─ 设置决策为 "hold-target", return heldPath
│   │   │
│   │   └─ 是 → 切换!
│   │       └─ 设置决策为 "best-target", return bestPath
│
│ ╔══════════════════════════════════════════════════════════════╗
│ ║ 优先级 3: 新目标 (无 heldTarget 或切换条件满足)            ║
│ ╚══════════════════════════════════════════════════════════════╝
│
├─ bestPath 非空?
│   └─ currentTarget_ = bestTarget
│   └─ currentTargetScore_ = bestScore
│   └─ return bestPath
│
│ ╔══════════════════════════════════════════════════════════════╗
│ ║ 优先级 4: Fallback (完全无路可走)                          ║
│ ╚══════════════════════════════════════════════════════════════╝
│
└─ return fallbackPath(localCurrent)
    │
    ├─ ratio = R/(L+ε)
    ├─ delayExit = (ratio<1.0 && collectedGold<3)  // 低 ratio 保护
    │
    ├─ localExit_≠kInvalid && !delayExit?
    │   └─ return routePath(current, exit)          // 走向出口
    │
    └─ else:
        └─ for target in observedPositions():
            ├─ target==current → skip
            ├─ delayExit && target==exit → skip     // 低 ratio 时禁止走出口
            ├─ !isWalkableForPlanning → skip
            ├─ path = routePath(current, target)
            ├─ if path.empty() → skip
            ├─ info = informationProxy(target, ...)
            └─ 选 info 最高的; 同 info 选路径短的
```

---

## 3.5 核心评分：`evaluate()` — 完整展开

### 3.5.1 守卫检查 (任意不通过→返回 −∞)

```
evaluate(path, target, context, localMap, poseEstimator) → double
│
├─ [Guard 1] path.size() ≤ 1? → return -1e18
│   └─ 路径仅包含当前位置，无意义
│
├─ [Guard 2] for pos in path:
│   └─ !localMap.isWalkableForPlanning(pos)? → return -1e18
│   └─ 路径上每个格子必须: observed && tile≠"#" && tile≠"B"
│
├─ [Guard 3] delta = pathResourceDelta(path, localMap)
│   │  (详见 3.5.2)
│   ├─ projectedR = state_.resource + delta
│   └─ projectedR < 0? → return -1e18
│   └─ 终点资源非负
│
├─ [Guard 4] !pathKeepsResourceNonNegative(path, R, localMap)?
│   └─ return -1e18
│   └─ 路径上每一步累计资源都 ≥ 0 (不能先踩陷阱再吃金币)
│
├─ [Guard 5] delta ≤ 0 && info == 0? → return -1e18
│   └─ 零收益 + 零信息 = 死节点，直接否决
│
└─ 全部通过 → 进入主评分
```

### 3.5.2 pathResourceDelta — 路径资源变化

```
pathResourceDelta(path, localMap) → int
│
├─ delta = 0
├─ for i = 1 to path.size()-1:  // 跳过 path[0] (当前位置)
│   ├─ pos = path[i]
│   ├─ tile = localMap.tile(pos)
│   ├─ tile=="G" && !localMap.isCollected(pos)? → delta += 50
│   └─ tile=="T" && !localMap.isTriggered(pos)? → delta -= 30
│                                      // 等价于 += kTrapValue (= -30)
└─ return delta
```

### 3.5.3 pathKeepsResourceNonNegative — 前缀资源约束

```
pathKeepsResourceNonNegative(path, currentResource, localMap) → bool
│
├─ resource = currentResource
├─ for i = 1 to path.size()-1:
│   ├─ tile = localMap.tile(pos)
│   ├─ tile=="G" && !isCollected? → resource += 50
│   ├─ tile=="T" && !isTriggered? → resource -= 30
│   └─ if resource < 0: return false
│       └─ 任何一步资源变负 → 非法
└─ return true
```

### 3.5.4 informationProxy — 信息价值

```
informationProxy(target, localMap, poseEstimator, areaCap, forceAreaMax)
│
├─ [Step 1] 统计已观察区域的资源密度
│   for pos in localMap.observedPositions():
│   │   ├─ obsCount++
│   │   ├─ tile=="G" → goldCount++
│   │   └─ tile=="T" → trapCount++
│   │
│   ├─ ρ̂_G = (goldCount+1) / (obsCount+10)
│   ├─ ρ̂_T = (trapCount+1) / (obsCount+10)
│   ├─ areaVal = max(50×ρ̂_G − 30×ρ̂_T, 0)
│   └─ ρ_area = clamp(areaVal/50, 0.001, 1.0)
│       └─ 金币多→ρ_area 高→每个未知格"值钱"
│       └─ 陷阱多→ρ_area 低→未知格贬值
│
├─ [Step 2] Boss-gated 特殊路径
│   └─ forceAreaMax && poseEstimator.unknownExtensionTouchesMazeEdge?
│       └─ return κ_u × bossEdgeAreaBonus × ρ_area
│           = 60 × 15 × ρ_area
│       └─ Boss 阻挡的通关推进区域, 按 bossEdgeAreaBonus 计
│
├─ [Step 3] 正常路径: BFS 展开未知连通块
│   └─ activeCap = areaCap>0? min(areaCap, areaMax) : areaMax
│   │   └─ 出口已知? activeCap=1.5 : 12
│   │
│   └─ sizes = poseEstimator.unknownComponentSizesTouchingView(
│               target, localMap, ceil(activeCap))
│       │
│       └─ [子函数] 以 target 为 BFS 起点
│           ├─ observed格 视为障碍 → 只展开未知格
│           ├─ 掩码边界也视为障碍
│           ├─ 若展开到 ceil(activeCap) 个未知格 → 立即返回该数
│           └─ 否则返回实际数
│       │
│   └─ componentVal = Σ min(size, activeCap) × ρ_area
│       └─ 每个连通块按 capped size × ρ_area 累加
│
└─ return κ_u × componentVal
    = 60 × Σ capped_size × ρ_area
```

### 3.5.5 futureGainMarginal — 边际尾部价值

```
futureGainMarginal(target, path, localMap) → double
│
├─ coinsOnPath = ∅
├─ for i=1..path.size()-1:
│   └─ path[i] 是G且未拾取? → coinsOnPath.insert(pos)
│       └─ 路径上会捡的金币 — 不计入后续机会
│
├─ best = 0.0
├─ for coin in localMap.knownCoins():
│   ├─ coin ∈ coinsOnPath? → skip         // 已计入 pathResourceDelta
│   ├─ coinPath = BFS(target → coin, on localMap)
│   ├─ coinPath 空? → skip                // 不可达
│   └─ value = 50.0 / (len(coinPath) + 1)
│       └─ best = max(best, value)
│
└─ return best
    └─ 最值钱的已知金币的边际价值上界
```

### 3.5.6 computeQEff — 步数代价系数

```
computeQEff(context, localMap) → double
│
├─ q_ref = context.state.resource
│        / (context.state.steps + epsilon)   // epsilon = 1e-6
│
├─ if !context.exitPath.empty():
│   ├─ exitDelta = pathResourceDelta(exitPath, localMap)
│   │   └─ 走向出口的路上还能捡多少资源
│   └─ q_ref = (R + exitDelta) / (L + len(exitPath) + ε)
│       └─ 如果现在就走出口, 最终 ratio 是多少
│
└─ return max(q_ref, 1.0)
    └─ q_min=1.0 保证 R=0 时步数有基本代价
```

### 3.5.7 marginPenalty — 安全裕量 barrier

```
marginPenalty(projectedResource) → double
│
├─ projectedResource ≥ 30? → return 0.0
│   └─ 资源充足, 无惩罚
│
└─ ratio = (30 − projectedResource) / 30.0    // 距离安全线的比例
    └─ return 8.0 × ratio²
        └─ 平方形式: 轻微不足影响小, 严重不足快速增大
        └─ projectedR=20: 8×(10/30)² = 0.89
        └─ projectedR=10: 8×(20/30)² = 3.56
        └─ projectedR=0:  8×(30/30)² = 8.0
```

### 3.5.8 主评分组装

```
return delta
     + 0.175 × state_.alphaSmooth × info       // ω_I·α·I_proxy
     + 1.23  × tail                             // β·V_tail^marg
     − 0.82  × qEff × len(path)                 // η_q·q_eff·len
     − margin;                                   // φ_margin
```

---

## 3.6 buildResult — 构建输出 JSON

```
buildResult(data, path, mode) → Json
│
├─ boss = runBossBattleJson(data.source)        // Boss 第二次调用
├─ bossCanWin = boss["ok"] && (!hasMinRounds || withinMinRounds)
├─ reviveCost = boss["CoinConsumption"]
│
├─ collected[15][15] = all false
├─ resource = 0, bossCleared = false, gameOver = false
│
├─ for step=0..path.size()-1:
│   │
│   ├─ tile = data.grid[row][col]
│   │
│   ├─ if !collected[row][col]:                  // 首次访问
│   │   ├─ delta = scoreDelta(tile)              // G→+50, T→-30, 其他→0
│   │   ├─ resource += delta
│   │   └─ collected[row][col] = true
│   │
│   ├─ if isBossTriggerCell(data, pos) && !bossCleared:
│   │   │   └─ 判断: 当前pos是否与任何Boss曼哈顿距==1?
│   │   │
│   │   ├─ bossCanWin? → event="boss", bossCleared=true
│   │   ├─ resource ≥ reviveCost? → event="boss_revive", resource-=reviveCost
│   │   └─ else → event="boss_game_over", gameOver=true
│   │
│   │   └─ result["events"].push_back({step, type, result: boss})
│   │
│   ├─ result["path"].push_back({row, col})
│   ├─ result["frames"].push_back({step, row, col, tile, delta, resource})
│   │
│   └─ gameOver? → break                          // 截断
│
├─ result["resource"] = resource
├─ result["steps"] = path.size()-1
├─ result["score_ratio"] = resource / steps
├─ result["finished"] = !gameOver && path.back()==exit
├─ result["game_over"] = gameOver
└─ result["boss"] = boss
```

---

## 3.7 attachGreedyDebug — 附加调试信息

```
attachGreedyDebug(result, run)
│
└─ count = min(run.debugSteps.size(), result["frames"].size())
└─ for i=0..count-1:
    └─ frames[i]["debug"] = greedyStepDebugJson(debugSteps[i])
        │
        ├─ step, localCurrent, realCurrent
        ├─ alpha, observedRatio, qEff
        ├─ decision ("best-target"/"hold-target"/"exit"/"pocket-first-target"/"fallback")
        ├─ selectedLocal, selectedReal
        │
        ├─ candidates[] — 每个候选:
        │   └─ { localTarget, realTarget, tile, score, deltaR,
        │        informationProxy, tailGain, qEff, pathLength,
        │        marginPenalty, projectedResource, selected }
        │
        ├─ rejected[] — 每个被拒格:
        │   └─ { localTarget, realTarget, tile, reason, pathLength }
        │
        ├─ closedSingletonGate:
        │   └─ { checked, triggered, candidateA, rewardA,
        │        bestNonClosedB, rewardB, simulatedAfterA,
        │        bestAfterATarget, bestAfterAReward,
        │        combinedA, allowed, reason }
        │
        └─ pocket:
            └─ { enabled, pocketHub, pocketResources[],
                 candidates[]{ baseScore, ownIproxy, remainI, scoreFirst },
                 chosenPocketTarget, reason }
```

---

## 3.8 Pocket-Aware Greedy (完整展开)

### 3.8.1 触发条件

```
choosePocketFirstTarget(localCurrent, context, localMap, poseEstimator, evaluator)
│
├─ [Step 1] findPocket(localCurrent, ...)
│   │
│   ├─ hubCandidates = {localCurrent} ∪ 四方向邻居中 isWalkable 的
│   │
│   └─ for hub in hubCandidates:
│       └─ pocketCoinsForHub(hub, ...)
│           └─ for coin in localMap.knownCoins():
│               └─ BFS(hub→coin) 长度 ≤ pocketRadius(=2)?
│                   └─ 加入 pocket
│       └─ coins.size() ≥ 2?
│           ├─ > bestSize? → 更新 bestHub
│           ├─ == bestSize && closerHub? → 更新 bestHub
│           └─ < bestSize → 忽略
│   │
│   └─ bestSize < 2 → return (不触发)
│
├─ [Step 2] qEff = evaluator.computeQEff(context, localMap)
│
├─ [Step 3] for coin in pocketCoins:
│   │
│   ├─ path = BFS(current→coin)
│   ├─ !pathKeepsResourceNonNegative? → score=-∞, 跳过
│   │
│   ├─ baseScore = deltaR − 0.82 × qEff × len
│   │   └─ 走到这个金币的基础收益 (不含 I_proxy)
│   │
│   ├─ remainI = 0.0
│   ├─ for otherCoin in pocketCoins (other≠coin):
│   │   ├─ otherPath = BFS(coin→otherCoin)
│   │   ├─ otherIproxy = informationProxy(otherCoin, ...)
│   │   ├─ remainI_other = otherIproxy / (1 + 0.2 × len(otherPath))
│   │   │   └─ μ=0.2 折扣: 越远越不值钱
│   │   └─ remainI = max(remainI, remainI_other)
│   │       └─ "如果先吃 coin, 剩下最高 I_proxy 的金币能保留多少价值"
│   │
│   └─ scoreFirst = baseScore + 0.2 × remainI
│       └─ λ_remain=0.2 控制"保留 I_proxy"在决策中的权重
│
├─ [Step 4] 选 scoreFirst 最高的 coin
│   └─ 同分 → 选路径短的 → 同路径 → 坐标排序
│
└─ decision = {enabled=true, target, path, score, debug}
```

### 3.8.2 设计动机

当一个 hub 附近有 ≥2 个金币时, 问题变成 "先吃哪一个"。
- 如果先吃 I_proxy 高的 → 剩下 I_proxy 低的 → AI 吃完后没有探索动力
- 如果先吃 I_proxy 低的 → 剩下 I_proxy 高的 → 吃完最后一个仍有探索动力
- `remainI` 衡量 "留到最后的价值", `scoreFirst` 倾向先吃低 I_proxy 的

---

## 3.9 Closed Singleton Lookahead Gate (完整展开)

### 3.9.1 触发条件

```
applyClosedSingletonLookaheadGate(request) → ClosedSingletonGateResult
│
├─ [Guard 1] request.localMap / poseEstimator / evaluator 均非空
├─ [Guard 2] findTopCandidate(candidates) 存在
│   └─ candidateA = 当前 top-1
├─ [Guard 3] isClosedSingletonCandidate(candidateA)
│   └─ 判定: unknownComponentSum == 1 && score有效 && path非空 && tile≠"E"
│   └─ 意义: 这个候选走完只能打开 1 个新格子 — 直接收益极低
│
└─ 不满足任一 → return {hasSelection=false} (不干预)
```

### 3.9.2 主逻辑

```
│
├─ findBestNonClosedCandidate(candidates, candidateA) → B
│   │
│   └─ 遍历 candidates:
│       ├─ candidate == A? → skip
│       ├─ !validScore? → skip
│       ├─ projectedResource < 0? → skip
│       ├─ isClosedSingleton? → skip  (其他 closed singleton 也不作为 B)
│       ├─ |C|=0 && dR≤0 && tail≤0 && tile≠"E"? → skip  (死节点)
│       └─ 通过 → 选 score 最高的作为 B
│
├─ if B 不存在:
│   └─ return {hasSelection=true, selectedTarget=A.target}
│       └─ "没有其他可比较的候选, A 自动通过"
│
├─ [Phase 2] simulateExecuteAWithoutNewVision(A)
│   │
│   ├─ 深拷贝 localMap → localMapAfter
│   ├─ 深拷贝 state → stateAfter
│   │
│   └─ for i=1..A.path.size()-1:                       // 沿 A.path 逐格走
│       ├─ pos = A.path[i]
│       ├─ !localMapAfter.isWalkableForPlanning(pos)? → 中止
│       ├─ localMapAfter.isBossTrigger(pos)? → 中止      // 不模拟Boss战
│       ├─ localMapAfter.markVisited(pos)
│       ├─ tile=="G" && !isCollected?                       // 结算资源
│       │   ├─ stateAfter.resource += 50                     // (新增)
│       │   ├─ stateAfter.collectedGold++                   // (新增)
│       │   └─ markCollected(pos)
│       ├─ tile=="T" && !isTriggered?
│       │   ├─ stateAfter.resource -= 30                    // (新增)
│       │   └─ markTriggered(pos)
│       └─ stateAfter.resource < 0? → 中止
│
│   ⚠️ 关键: 不执行 3×3 视野更新 — 不假设 A 会看到什么新格子
│
├─ [Phase 3] computeMemoryOnlyContinuationAfterA(A)
│   │
│   ├─ 在 localMapAfter 上计算出口路径
│   │
│   └─ for pos in localMapAfter.observedPositions():
│       ├─ pos==A? → skip
│       ├─ pos==exit? → skip
│       ├─ isInsideThreeByThreeArea(A.target, pos)? → skip
│       │   └─ A 周围 3×3 的视野没模拟, 不能使用这些候选
│       ├─ isVisited(pos)? → skip
│       ├─ !isWalkable? → skip
│       │
│       ├─ path = BFS(A.target→pos, localMapAfter)
│       ├─ score = evaluate(path, pos, contextAfter, localMapAfter, poseEstimator)
│       ├─ delta, info, tail 按正常流程
│       ├─ |C|=0 && dR≤0 && tail≤0? → skip  (死节点)
│       └─ score > bestReward? → bestReward=score, c_A_target=pos
│       │
│       └─ return {simulated=true, safe=true, bestTarget, bestReward=c_A}
│
├─ [Phase 4] 非对称决策
│   │
│   ├─ combinedA = reward(A) + 1.0 × c_A          // γ=1.0
│   ├─ threshold = reward(B) + (−20)               // margin=−20
│   │
│   ├─ combinedA > threshold?
│   │   ├─ YES → A 通过
│   │   │   └─ return {hasSelection=true, selectedTarget=A, ...}
│   │   │       └─ reason: "reward(A)+γ×c_A beats best non-closed candidate"
│   │   │
│   │   └─ NO → A 被拒绝, 改选 B
│   │       └─ return {hasSelection=true, changed=true,
│   │                   selectedTarget=B, ...}
│   │       └─ reason: "after-A best reward insufficient"
│
│   └─ 非对称原理: 只算 A 的 continuation (c_A), 不算 B 的 (c_B)
│       因为 A 是 closed singleton (环境已知), B 是开放节点 (模拟不可靠)
```

---

# 第四章：路由算法详解

## 4.1 BFS on localMap (greedy/smart 默认)

```
PathValueEvaluator::shortestPathOnKnownMap(start, target, localMap) → path
│
├─ !isWalkableForPlanning(start) || !isWalkableForPlanning(target)? → {}
│
├─ visited: set<Position>, parent: map<Position,Position>
├─ queue: queue<Position>
│
├─ queue.push(start), visited.insert(start), parent[start]=kInvalid
│
├─ while queue 非空:
│   ├─ current = queue.front(); queue.pop()
│   ├─ current == target? → break
│   └─ for {dr,dc} in kDirs:
│       ├─ next = {current.r+dr, current.c+dc}
│       ├─ visited 含 next? → skip
│       ├─ !isWalkableForPlanning(next)? → skip
│       ├─ visited.insert(next), parent[next]=current
│       └─ queue.push(next)
│
├─ visited 不含 target? → {}
└─ 回溯 parent 链 → reverse → return path
```

## 4.2 A\* on localMap

```
astarPath(localMap, start, target) → path
│
├─ !isWalkableForPlanning(start/target)? → {}
│
├─ dist: map<Position,int>, parent: map<Position,Position>
├─ h(pos) = |pos.r−target.r| + |pos.c−target.c|   // 曼哈顿距离
│
├─ priority_queue<{f=g+h, pos}, min-heap>
├─ dist[start]=0, push({h(start), start})
│
├─ while queue 非空:
│   ├─ {curDist, cur} = top(); pop()
│   ├─ dist[cur] != curDist? → skip (lazy deletion)
│   ├─ cur == target? → break
│   └─ for 四方向:
│       └─ isWalkableForPlanning(next)?
│           ├─ nextDist = curDist+1
│           ├─ next 不在 dist 或 nextDist<dist[next]?
│           │   ├─ dist[next]=nextDist, parent[next]=cur
│           │   └─ push({nextDist+h(next), next})
│
├─ dist 不含 target? → {}
└─ 回溯 parent → reverse → return path
```

## 4.3 Dijkstra on localMap

与 A\* 相同，但 h(pos) = 0 (无启发式)，退化为 Uniform-Cost Search。等权网格上等价 BFS 但使用优先队列。

## 4.4 Branch & Bound on localMap

```
branchBoundPath(localMap, start, target) → path
│
├─ bestPath = {}, upperBound = ∞
│
├─ DFS recursion:
│   ├─ cur == target?
│   │   └─ len(curPath) < upperBound? → bestPath=curPath, upperBound=len
│   │
│   ├─ lb = len(curPath) + |cur.r−target.r| + |cur.c−target.c|   // 曼哈顿下界
│   ├─ lb ≥ upperBound? → prune (剪枝)
│   │
│   └─ for 四方向 (按距 target 曼哈顿距排序, 先扩展更近的):
│       ├─ isWalkable && 未在 curPath 中?
│       ├─ curPath.push(next)
│       └─ DFS(next) → 回溯 curPath.pop()
│
└─ return bestPath
```

## 4.5 Divide & Conquer (双向 BFS)

```
divideConquerPath(localMap, start, target) → path
│
├─ frontierA = {start}, frontierB = {target}
├─ parentA[start]=kInvalid, parentB[target]=kInvalid
│
├─ while frontierA 和 frontierB 都非空:
│   │
│   ├─ 选较小的 frontier 扩展 (负载均衡)
│   │
│   ├─ newFrontier = {}
│   ├─ for pos in 选中的 frontier:
│   │   └─ for 四方向:
│   │       └─ isWalkable && 未访问?
│   │           ├─ 记录 parent
│   │           └─ newFrontier.insert(next)
│   │
│   ├─ 检测相遇: newFrontier ∩ 对方已访问集?
│   │   └─ meetPoint = 第一个相遇点
│   │   └─ 回溯: start→meetPoint (A侧) + meetPoint→target (B侧反转)
│   │   └─ return 合并路径
│   │
│   └─ frontier = newFrontier
│
└─ return {} (不可达)
```

---

# 第五章：路径 B — 全局最近邻

## 5.1 planAdventurePath("smart") — 完整流程

```
planAdventurePath(data, "smart") → vector<Position>
│
├─ 路由型算法禁止独立调用:
│   dijkstra/astar/branch_bound/divide_conquer? → throw
│
├─ planningData = data (拷贝)
├─ remaining = Set(所有 data.golds)
├─ bossTriggered[i] = false × data.bosses.size()
│
├─ current = data.start
├─ fullPath = {current}
│
├─ while remaining非空 或 存在未击败Boss:
│   │
│   ├─ best = kInvalid, bestPath = {}, bestBossIdx = -1
│   │
│   ├─ [扫描金币] for target in remaining:
│   │   └─ try:
│   │       candidate = shortestPath(planningData, current, target, "smart")
│   │       │
│   │       └─ [子调用] shortestPath 分发到 astarPath(MazeData):
│   │           └─ A* 在 data.grid 上从 current 到 target
│   │           └─ passable() 检查: 界内 && ≠"#" && ≠"B"
│   │           └─ 不可达? → throw "unreachable" (try-catch 跳过)
│   │       │
│   │       └─ best==kInvalid 或 candidate.size() < bestPath.size()?
│   │           └─ 更新 best=target, bestPath=candidate, bestBossIdx=-1
│   │
│   ├─ [扫描 Boss 触发点] for i in 0..bosses.size()-1:
│   │   ├─ bossTriggered[i]? → skip
│   │   └─ for {dr,dc} in 四方向:
│   │       ├─ trigger = boss ± (dr,dc)
│   │       ├─ passable(planningData, trigger)? → skip
│   │       └─ try:
│   │           candidate = shortestPath(planningData, current, trigger, "smart")
│   │           └─ best==kInvalid 或 candidate.size() < bestPath.size()?
│   │               └─ 更新 best=trigger, bestPath=candidate, bestBossIdx=i
│   │
│   ├─ best==kInvalid? → break                    // 无路可达
│   │
│   ├─ fullPath += bestPath[1..]                   // 跳过重复起点
│   ├─ current = best
│   │
│   ├─ bestBossIdx >= 0?
│   │   ├─ bossTriggered[bestBossIdx] = true
│   │   └─ planningData.grid[boss.r][boss.c] = " " // Boss 击败后变空地
│   └─ else:
│       └─ remaining.erase(best)                    // 金币已收集
│
└─ exitPath = shortestPath(planningData, current, exit, "smart")
    └─ fullPath += exitPath[1..]
    └─ return fullPath
```

---

# 第六章：Boss 战系统

## 6.1 顶层: runBossBattleJson

```
runBossBattleJson(source) → Json
│
├─ [校验] source 含 "B" (HP数组) 和 "PlayerSkills" (技能数组)
├─ [解析] bossHPs = B[], skills = [Skill{id, damage, cd}, ...]
├─ readMinRounds(source) → -1 或整数
│
├─ [主循环] for bossIndex=0..bossHPs.size()-1:   // 顺序揭示
│   │
│   ├─ phaseTurnLimit = minRounds − usedTurns − remainingBossCount
│   │   └─ 负值 = 不限制
│   │
│   ├─ reserveSlack = (首Boss && 还有后续)? 1 : 0
│   │
│   ├─ plan = solveCurrentBossByLightCapacity(hp, cooldown, skills,
│   │                                          reserveSlack, phaseTurnLimit)
│   │   │
│   │   └─ [子函数] 枚举回合+分支限界 (详见 §6.2)
│   │
│   ├─ !plan.ok? → return {ok:false, error:"no solution"}
│   │
│   ├─ 记录 phase {bossIndex, turns, sequence, cooldownAfter, ...}
│   ├─ usedTurns += plan.turns
│   └─ cooldown = plan.cooldownAfter
│   │
├─ 收集 phase 信息到 result
├─ 设置 reviveRule {coinCost, restartPosition, knownHpPersistence}
├─ minRounds≥0? → 设置 withinMinRounds, matchesMinRounds
└─ return result
```

## 6.2 solveCurrentBossByLightCapacity — 单 Boss 分支限界

```
solveCurrentBossByLightCapacity(hp, cooldown, skills, reserveSlack, phaseTurnLimit)
│
├─ maxDamage = max(skills[i].damage)
├─ minForcedDamage = minimumForcedDamagePerTurn(skills)
│   └─ 冷却=0且伤害>0的技能中取最小伤害 (不能等待时必须出招)
│
├─ lowerBound = ceil(hp / maxDamage)               // 理论最小回合
├─ upperBound = phaseTurnLimit + reserveSlack      // 最大允许回合
│
├─ bestPlan = {}
│
├─ for turns = lowerBound .. upperBound:            // 逐精确回合枚举
│   │
│   ├─ candidates = enumerateKillExactTurns(hp, cooldown, skills, turns, memo)
│   │   │
│   │   └─ [子函数] BFS 逐层枚举:
│   │       ├─ turn 0: states = [{hp, startCooldown, {}}]
│   │       ├─ for t=0..exactTurns-1:
│   │       │   ├─ 本层去重: liveStateKey(hp, cooldown) → 保留最小序列
│   │       │   ├─ for state in states:
│   │       │   │   └─ for action in availableBossActions(cooldown):
│   │       │   │       ├─ applyBossAction → (nextHp, nextCd)
│   │       │   │       ├─ !lastTurn && nextHp≤0? → skip (不能提前死)
│   │       │   │       ├─ lastTurn && nextHp>0? → skip (必须死)
│   │       │   │       └─ 通过 → 记入下一层 (去重: 同 cooldownAfter 保留字典序最小)
│   │       │   └─ states = 下一层
│   │       └─ return 所有合法击杀终态
│   │
│   └─ for cand in candidates:
│       │
│       ├─ lightScore = evaluateSuffixLightCapacity(cand, skills, remainingBossCount, memo)
│       │   │
│       │   └─ [子函数] 滚动时域评估:
│       │       ├─ 已知后缀 (remainingBossCount>0):
│       │       │   └─ 用 cand.cooldownAfter 继续枚举后续 Boss 的最小回合
│       │       │   └─ knownCapacity = 剩余回合 − Σ(后续Boss最小回合)
│       │       │
│       │       └─ 未知后缀:
│       │           └─ suffixCapacityThreshold(remainingBossCount)
│       │               └─ 均匀分配截止时间片 → 每片 avgDamage
│       │               └─ return min_over_slices(avgDamage)
│       │       │
│       │       └─ lightScore = min(knownCapacity, unknownThreshold)
│       │
│       └─ betterBossPlanByLightCapacity(bestPlan, {turns, lightScore, ...})?
│           └─ 比较链: lightScore > turns > cooldownCost > readyDamage > sequence
│           └─ 是? → bestPlan = cand
│
└─ return bestPlan
```

---

# 第七章：15×15 掩码估计系统 (MapPoseEstimator)

## 7.1 问题背景

AI 只通过 3×3 视野观察迷宫，不知道自己在 15×15 坐标系中的绝对位置。但 `informationProxy()` 需要知道 "走到某个候选目标后能展开多少未知格"——如果不限制 BFS 范围，会把整个迷宫未探索区域都算进去，严重高估 I_proxy。

**掩码系统的目标**：仅凭局部观察，推断 AI 在 15×15 迷宫中的大致位置和朝向，从而用一个 **15×15 的掩码框**去裁剪未知连通块的 BFS 展开范围。

## 7.2 核心数据结构

### 7.2.1 Direction 枚举

```cpp
enum class Direction { Up, Down, Left, Right };
// 表示 AI 进入迷宫后面对的方向（从入口往迷宫内部看）
// 这是局部坐标到估计 15×15 坐标之间做旋转变换的关键参数
```

### 7.2.2 MapEmbeddingHypothesis — 入口假设

```cpp
struct MapEmbeddingHypothesis {
    Position entry{kInvalid};              // AI 入口在 15×15 坐标系中的估计位置
    Direction inwardDirection;             // 从入口进入后朝迷宫内部的方向
    double score = 0.0;                    // 评分（越高越可信）
    bool feasible = false;                 // 当前观察是否兼容这个假设
};
```

**含义**：一个假设描述了 "AI 的局部原点 (0,0) 对应 15×15 迷宫的 `entry` 坐标，且 AI 面向 `inwardDirection` 方向进入迷宫"。

**示例**：
- `{entry={0,7}, inwardDirection=Down}` → AI 出生在上边界中央，面向下，局部坐标 (r,c) 映射为 (0+r, 7+c)
- `{entry={7,14}, inwardDirection=Left}` → AI 出生在右边界中央，面向左，局部坐标 (r,c) 映射为 (7+c, 14-r)

### 7.2.3 MapPoseEstimator — 估计器本体

```cpp
class MapPoseEstimator {
    static constexpr int kEstimatedSize = 15;           // 固定 15×15

    enum class MaskSeedKind {
        Internal, Top, Bottom, Left, Right,
        TopLeft, TopRight, BottomLeft, BottomRight
    };

    vector<MapEmbeddingHypothesis> hypotheses_;  // 候选入口假设 (1~13 个)
    MapEmbeddingHypothesis best_;                // 当前最高分假设
    set<Position> observedEstimated_;            // 映射去重后的已观察估计坐标
    bool maskSeeded_ = false;                    // 是否已根据出生观察确定 seedKind_
    bool maskActive_ = false;                    // 掩码是否生效 (BFS 是否裁剪)
    MaskSeedKind seedKind_;                      // 出生边缘类型
};
```

## 7.3 掩码生命周期

掩码经历四个阶段：**初始化 → 种子检测 → 候选排除 → 激活裁剪**

## 7.4 阶段一：初始化 (`initialize()`)

```
initialize()
│
├─ hypotheses_.clear()
├─ 创建单一默认假设:
│   └─ entry = {7, 7}           // 15×15 中心
│   └─ inwardDirection = Down   // 默认向下
│   └─ score = 0.0, feasible = true
│
├─ best_ = 该默认假设
├─ observedEstimated_.clear()
├─ maskSeeded_ = false           // 等待首次 update()
├─ maskActive_ = false           // 掩码暂不生效
└─ seedKind_ = Internal          // 默认内部出生
```

此时掩码不存在——I_proxy 的 BFS 不剪裁，所有方向的未知区域都会被计入。这是保守的早期行为。

## 7.5 阶段二：种子检测 (`update()`, 首次调用)

首次调用 `update()` 时，通过出生点周围 3×3 视野中的 **outside 标记**判断 AI 从迷宫哪条边进入。

### 7.5.1 outside 标记的来源

`updateKnownMap()` 扫描 3×3 时，超出 `realInBounds()` 的格子调用 `localMap_.setOutside(localPos)`：

```cpp
// cells_[localPos] = {observed=true, outside=true, tile="#"}
```

outside 格的意义：这个方向在迷宫**外部**——说明 AI 靠近迷宫的这一侧边界。

### 7.5.2 种子检测逻辑

```
首次 update(localMap, current):
│
├─ outside = localMap.outsidePositions()
│   └─ 返回所有 outside=true 的局部坐标
│
├─ 检查出生点(0,0)周围的 3×3 区域:
│   ├─ topOutside    = (−1,−1), (−1,0), (−1,1) 都是 outside?
│   ├─ bottomOutside = (+1,−1), (+1,0), (+1,1) 都是 outside?
│   ├─ leftOutside   = (−1,−1), (0,−1), (+1,−1) 都是 outside?
│   └─ rightOutside  = (−1,+1), (0,+1), (+1,+1) 都是 outside?
│
├─ 根据越界方向组合设置 seedKind_:
│   │
│   ├─ topOutside && leftOutside:
│   │   └─ seedKind_ = TopLeft
│   │   └─ 入口唯一: {0, 0}
│   │   └─ AI 出生在左上角，坐标映射为直接平移
│   │
│   ├─ topOutside (仅上方越界):
│   │   └─ seedKind_ = Top
│   │   └─ 候选入口 13 个: {0,1} ~ {0,13}
│   │   └─ AI 在上边界但不知水平偏移
│   │
│   ├─ topOutside && rightOutside:
│   │   └─ seedKind_ = TopRight
│   │   └─ 入口唯一: {0, 14}
│   │
│   └─ ... (Bottom, Left, Right, 组合角同理)
│   │
│   └─ 全无越界 → Internal (出生在内部)
│       └─ 入口: {7, 7}, 掩码暂不激活
│
└─ maskSeeded_ = true
```

### 7.5.3 坐标映射函数 (`mapWithHypothesis`)

```
mapWithHypothesis(localPos, hypothesis) → Position (15×15 坐标)
│
├─ 根据 hypothesis.inwardDirection 旋转+平移:
│
├─ Direction::Down:    // 入口在上边，面向下 → 局部 (r,c) = 估计 (entry.r+r, entry.c+c)
│   └─ return {entry.r + localPos.r, entry.c + localPos.c}
│
├─ Direction::Up:      // 入口在下边，面向上 → 局部 (r,c) = 估计 (entry.r−r, entry.c+c)
│   └─ return {entry.r − localPos.r, entry.c + localPos.c}
│       └─ r 轴反转：AI 往下走时，估计坐标往上移
│
├─ Direction::Right:   // 入口在左边，面向右 → (r,c) 交换
│   └─ return {entry.r + localPos.c, entry.c + localPos.r}
│       └─ 局部向右走 = 估计向下，局部向下走 = 估计向右
│
└─ Direction::Left:    // 入口在右边，面向左 → (r,c) 交换且 c 反转
    └─ return {entry.r + localPos.c, entry.c − localPos.r}
```

## 7.6 阶段三：候选假设排除 (`update()`, 后续调用)

每次 `update()` 执行后，用**新观察到**的格子信息排除不兼容的入口假设。

### 7.6.1 排除时机

```
hasFullHorizontal = (maxCol − minCol + 1) ≥ 15   // 水平跨度覆盖全宽
hasFullVertical   = (maxRow − minRow + 1) ≥ 15   // 垂直跨度覆盖全高
canPruneHorizontal = horizSpan ≥ 14               // 水平跨度够大
canPruneVertical   = vertSpan ≥ 14                // 垂直跨度够大

canPrune = (Top/Bottom && canPruneHorizontal) ||
           (Left/Right && canPruneVertical)
```

只有观察跨度 ≥ 14 格时才做排除——此前信息不足，强行排除可能把正确假设也删掉。

### 7.6.2 三种检查

对每个候选假设执行三项检查，任一不通过则排除：

```
检查 1: 所有已观察格映射后在 15×15 范围内
│
│   for pos in observed:
│       mapped = mapWithHypothesis(pos, hypothesis)
│       └─ mapped 超出 [0,14]×[0,14]? → 假设矛盾, 排除

检查 2: 映射到边界格的局部格必须是墙
│
│   for pos in observed:
│       mapped = mapWithHypothesis(pos, hypothesis)
│       boundary = (mapped.row==0 || mapped.row==14 ||
│                   mapped.col==0 || mapped.col==14)
│       └─ boundary && localMap.tile(pos) ≠ "#"?
│           └─ 除非 pos=={0,0} && tile=="S" (起点的入口位置)
│           └─ 否则 → 假设矛盾, 排除
│
│   原理: 迷宫最外一圈全是墙, 映射到边界的格必须是墙类型。
│   如果不是, 说明这个入口假设把内部格错误旋转到了边界上。

检查 3: outside 格映射后不能在 15×15 范围内
│
│   for pos in localMap.outsidePositions():
│       mapped = mapWithHypothesis(pos, hypothesis)
│       └─ insideEstimated(mapped)? → 假设矛盾, 排除
│
│   原理: outside 是迷宫外部, 映射后在 15×15 内说明假设把外部格当成了内部。
```

只有至少保留一个候选时才替换假设集合；全部被排除 → 保留旧的（宁可保守也不删光）。

## 7.7 阶段四：掩码激活判定

```
switch (seedKind_):
│
├─ Top / Bottom (单边界):
│   └─ maskActive_ = (hypotheses_.size() == 1) || hasFullHorizontal
│   └─ 假设唯一（列偏移已确定）→ 激活
│   └─ 水平满 15 格（能看到完整宽度）→ 激活
│   └─ 激活后精修 entry: best_.entry.col = −minCol
│
├─ Left / Right (单边界):
│   └─ maskActive_ = (hypotheses_.size() == 1) || hasFullVertical
│   └─ 激活后精修 entry: best_.entry.row = −minRow
│
├─ TopLeft / TopRight / BottomLeft / BottomRight (角落):
│   └─ maskActive_ = true  (出生即确定)
│   └─ 因为角落只有一个入口位置，无需等待任何跨度
│
└─ Internal (内部出生):
    └─ maskActive_ = hasFullHorizontal && hasFullVertical
    └─ 水平和垂直都满 15 格 → 激活
    └─ 激活后精修 entry: best_.entry = {−minRow, −minCol}
    └─ 无越界信息，只能等看到迷宫两端的墙后才确定位置
```

## 7.8 阶段五：重建预估观察集

```
observedEstimated_.clear()
for pos in observed:
    mapped = mapWithHypothesis(pos, best_)
    │
    ├─ maskActive_ && !insideEstimated(mapped)?
    │   └─ maskActive_ = false     // 安全回退
    │   └─ observedEstimated_.clear()
    │   └─ break
    │
    └─ insideEstimated(mapped)?
        └─ observedEstimated_.insert(mapped)
```

## 7.9 掩码在 I_proxy 中的作用

### 7.9.1 裁剪未知连通块 BFS

```
unknownComponentSizesTouchingView(target, localMap, areaMax)
│
├─ [Guard] !maskActive_? → areaMax = areaMax (不做掩码裁剪)
│   掩码未激活 → BFS 可以扩展到所有未知格
│
├─ [Guard] maskActive_ && !isInsideEstimatedMaze(target)?
│   └─ return {0}                                    // 目标在掩码外, 无价值
│
├─ [BFS] 以 target 为起点:
│   ├─ blockedByMaskBoundary(pos):
│   │   └─ maskActive_?
│   │       └─ mapped 在 15×15 边界 (row/col==0||14)? → 阻挡
│   │       └─ mapped 在 15×15 外? → 阻挡
│   │   └─ !maskActive_?
│   │       └─ 根据 seedKind_ 做部分边界阻挡:
│   │           ├─ Top/Bottom: row≤0 || row≥14 → 阻挡
│   │           ├─ Left/Right: col≤0 || col≥14 → 阻挡
│   │           └─ Internal: 不阻挡
│   │
│   ├─ observed 视为障碍
│   └─ 遇到 blockedByMaskBoundary → 当作墙, BFS 不穿过
│
└─ 返回展开到的未知格数量 (上限 areaMax)
```

### 7.9.2 动态 α 中的未知比例

```
estimatedUnknownCount()
│
└─ maskActive_?
    ├─ 是 → return 225 − observedEstimated_.size()
    │       精确统计: 总格子 − 已映射已观察格
    │
    └─ 否 → return 225 − observedEstimated_.size()
            同样公式, 但 observedEstimated_ 在掩码未激活时也可能偏少
            (早期探索动力由 ρ_U 的偏大值自然增强)
```

### 7.9.3 isInsideEstimatedMaze 的双重角色

```
isInsideEstimatedMaze(localPos) → bool
│
├─ maskActive_==false? → return true (不裁剪)
│   └─ 掩码未激活时不限制, 所有方向都视为有效
│
└─ maskActive_==true? → return insideEstimated(localToEstimatedGlobal(localPos))
    └─ 掩码激活后, 超出 15×15 的局部格 → 视为迷宫外部, 无探索价值
```

此函数在两个地方被调用:
1. `informationProxy()` 中统计 `N_new` — 只有落在估计迷宫内的未知格才计入
2. `unknownComponentSizesTouchingView()` 中判断 BFS 起点 — 掩码外的候选无价值

## 7.10 完整生命周期示例

```
步 0: Agent 构造
  → initialize(): entry={7,7}, maskActive=false, maskSeeded=false

步 1: 第一次 updateKnownMap + update
  → 出生点 3×3 发现上方 outside → seedKind=Top
  → 生成 13 个候选 {0,1}~{0,13}
  → maskActive= false (只有 9 格观察, 水平跨度不够)
  → 此时 I_proxy BFS 不裁剪: 所有方向未知格都算

步 20: 观察到 14 格跨度
  → canPruneHorizontal = true
  → 用墙/边界检查排除 11 个候选, 剩 2 个
  → 但仍不足以唯一确定 → maskActive 仍 false

步 35: 观察到 15 格完整宽度
  → hasFullHorizontal = true
  → maskActive_ = true ← 掩码激活
  → 精修 entry.col = -minCol
  → 此后 I_proxy BFS 被 15×15 边框裁剪, 不会高估未知面积

步 140: 出口已知
  → informationProxy 用 knownExitAreaCap=1.5 替代 A_max=12
  → 削弱开阔区域奖励, 鼓励收尾
```

## 7.11 关键设计原则

1. **不读真实坐标**：掩码完全从局部观察推断，AI 不应知道自己在迷宫中的真实 (row, col)
2. **逐步收敛**：从无掩码 → 部分边界阻挡 → 完整 15×15 掩码。早期宁可高估未知面积也不做过度裁剪
3. **安全回退**：掩码激活后发现映射矛盾 → 立即 `maskActive_ = false`，回到无掩码状态
4. **角落优先**：角出生 (TopLeft 等) 从第一步就激活掩码——因为入口位置唯一确定
5. **边界墙校验**：映射到 15×15 边界的格必须是墙类型 → 如果不能满足，该入口假设被排除

---

# 第八章：辅助数据结构

## 7.1 LocalCell — 单个格子的 AI 记忆

```
LocalCell {
    string tile = "U"       // 格子类型 (G/T/B/E/#/ /U)
    bool observed = false    // 是否被 3×3 视野点亮
    bool outside = false     // 确认在迷宫边界外
    bool visited = false     // AI 实际踩过
    bool collected = false   // 金币已拾取
    bool triggered = false   // 陷阱已触发
    bool bossTrigger = false // Boss 四邻触发区
}
```

## 7.2 LocalKnownMap — AI 的记忆地图

```
LocalKnownMap {
    map<Position, LocalCell> cells_   // 只有观察过的格子才在 map 中

    setObserved(pos, tile)    → cells_[pos] = {observed, tile}
    setOutside(pos)           → cells_[pos] = {observed, outside, tile="#"}
    markBossTriggers(boss)    → 四邻 cells_[].bossTrigger = true
    clearBoss(boss)           → cells_[boss] = {observed, tile=" "}
    markVisited(pos)          → cells_[pos].visited = true
    markCollected(pos)        → cells_[pos].collected = true
    markTriggered(pos)        → cells_[pos].triggered = true
    isWalkableForPlanning(pos)
        → cells_含pos && observed && tile≠"#" && tile≠"B"
    knownCoins()
        → 遍历 cells_: tile=="G" && !collected → 返回列表
}
```

## 7.3 AgentState

```
AgentState {
    int resource = 0          // 累计资源
    int steps = 0             // 已走步数
    int collectedGold = 0     // 已拾取金币数
    double alphaSmooth = 4.0  // EMA 平滑后的 α
}
```

## 7.4 PathValueContext

```
PathValueContext {
    AgentState state                          // 当前状态快照
    vector<Position> exitPath                 // 当前→出口路径 (不可达则空)
    set<Position> bossGatedAreaMaxTargets     // Boss-gated 候选集合
}
```

---

# 第八章：参数总表

| 参数 | 值 | 位置 | 含义 |
|---|---|---|---|
| ω_I | 0.175 | ω_I·α·I_proxy | 信息价值折扣 |
| α_0~min~max | 4.0/1.0/8.0 | α_raw / clip | 动态探索权重 |
| θ | 0.8 | α_smooth EMA | 平滑系数 |
| β | 1.23 | β·V_tail | 尾部金币权重 |
| κ_u | 60 | I_proxy | 连通块面积缩放 |
| A_max | 12 | 出口未知面积上限 | |
| knownExitAreaCap | 1.5 | 出口已知面积上限 | |
| bossEdgeAreaBonus | 15 | Boss-gated 面积加成 | |
| η_q | 0.82 | η_q·q_eff·len | 长度代价缩放 |
| q_min | 1.0 | max(q_ref,1.0) | 步数代价下限 |
| m_safe | 30 | φ_margin 安全线 | |
| λ_m | 8.0 | φ_margin 强度 | |
| τ | 5.0 | shouldGoExit | 停止探索 |
| switchMargin | 5.0 | 目标保持 | 切换阈值 |
| γ_closed | 1.0 | combinedA=reward+γ×c_A | |
| margin_closed | -20 | combinedA>reward(B)+m | |
| pocketRadius | 2 | 金币到hub距离 | |
| μ | 0.2 | remainI 折扣 | |
| λ_remain | 0.2 | remainI 权重 | |
| λ | 10.0 | 密度估计平滑 | |
| λ_G/λ_T | 1.0/1.0 | 金币/陷阱先验 | |
| ρ_area_min | 0.001 | ρ_area clip 下界 | |
| ε | 1e-6 | 防除零 | |
