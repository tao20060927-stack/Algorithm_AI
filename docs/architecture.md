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
if (isRewardRouterAlgorithm(algorithm)) {  // "dijkstra"/"astar"/"branch_bound"/"divide_conquer" (AIPlayerEngine.cpp:51)
    -> realtimeGreedyRun(data, algorithm)  // 路径 A — reward选目标, 指定路由器
} else {
    -> planAdventurePath(data, algorithm)  // 路径 B — "smart" 全局最近邻
}
```

`"greedy"` 不经过 `RunAdventure`，直接从 `RunRealtimeGreedy` 进入。

---

# 第三章：程序入口与 JSON→输出 全流程

## 3.1 main() 在哪里

项目有多个可执行入口，各自有 `main()`：

| 入口 | 文件:行 | 用途 |
|---|---|---|
| 桌面 GUI | `DesktopApp.cpp:440` `WinMain` | WebView2 桌面应用，前端通过 `chrome.webview.hostObjects.ai.RunRealtimeGreedy(json)` 调用后端 |
| 冒烟测试 | `SmokeTest.cpp:9` `main(argc,argv)` | 命令行：`ai_player_smoke <input.json>` 跑所有算法 |
| 批量评测 | `GreedyEval.cpp:180` `main(argc,argv)` | 批量跑分输出 CSV |
| Boss 单测 | `BossStrategyTest.cpp:39` `main()` | Boss 求解器单元测试 |
| Pocket 单测 | `PocketAwareGreedyTest.cpp:140` `main()` | Pocket 策略单元测试 |
| Gate 单测 | `ClosedSingletonLookaheadGateTest.cpp:260` `main()` | 闭单例门控单元测试 |
| 掩码单测 | `MaskPoseEstimatorTest.cpp:250` `main()` | 掩码估计单元测试 |

**两个主流程入口**：桌面 GUI（`DesktopApp.cpp:440`）和命令行冒烟测试（`SmokeTest.cpp:9`）。两者都调用 `AIPlayerEngine` 的同一套 API。

## 3.2 从原始 JSON 到最终输出 — 完整数据流

```
┌─────────────────────────────────────────────────────────┐
│ 步骤 1: 接收输入                                         │
├─────────────────────────────────────────────────────────┤
│ 桌面: 用户粘贴 JSON → WebView2 JS → HostObject          │
│       → AiHostObject::Invoke(id=1, params)              │
│       → engine_.RunRealtimeGreedy(jsonText)              │
│       DesktopApp.cpp:216 (Invoke 分发)                   │
│                                                         │
│ 命令行: ai_player_smoke.exe input.json                  │
│       → std::ifstream(argv[1]) → jsonText               │
│       → engine.RunRealtimeGreedy(jsonText)               │
│       SmokeTest.cpp:28                                   │
└─────────────────────────────────────────────────────────┘
                          │
                          ▼
┌─────────────────────────────────────────────────────────┐
│ 步骤 2: 缓存检查 + JSON 解析                            │
│ AIPlayerEngine.cpp:319 (RunRealtimeGreedy)               │
├─────────────────────────────────────────────────────────┤
│ makeRunCacheKey("greedy", jsonText)  // 算法名+规范化JSON │
│ resultCache_.find(key) → 命中? 直接 return              │
│ parseMaze(jsonText) → MazeData                          │
│   AIPlayerEngine.cpp:84                                 │
│   ├─ Json::parse(jsonText)                              │
│   ├─ 校验 source["maze"] 是 15×15 二维数组              │
│   ├─ 遍历 grid[r][c]:                                   │
│   │   tile=="S"→start, "E"→exit, "B"→bosses[], "G"→golds[] │
│   └─ 返回 MazeData { grid, start, exit, bosses, golds, source }│
└─────────────────────────────────────────────────────────┘
                          │
                          ▼
┌─────────────────────────────────────────────────────────┐
│ 步骤 3: 算法执行                                         │
│ AIPlayerEngine.cpp:326                                  │
├─────────────────────────────────────────────────────────┤
│ [路径A] RunRealtimeGreedy / RunAdventure("astar"...):    │
│   GreedyRunResult run = realtimeGreedyRun(data)          │
│   RealtimeGreedyStrategy.cpp:790                         │
│   ├─ MemoryGreedyAgent 构造 → 预计算 Boss 战              │
│   └─ agent.run() → 主循环 900 步上限                     │
│       └─ 每步: updateKnownMap → applyCurrentCell         │
│              → updateAlphaSmooth → selectBestPath         │
│              → 走一步                                     │
│       输出: { path[], debugSteps[], gameOver }           │
│                                                         │
│ [路径B] RunAdventure("smart"):                           │
│   path = planAdventurePath(data, "smart")                │
│   ShortestPathStrategy.cpp:50                            │
│   └─ 全局最近邻贪心循环                                   │
│       输出: path[]                                       │
└─────────────────────────────────────────────────────────┘
                          │
                          ▼
┌─────────────────────────────────────────────────────────┐
│ 步骤 4: 构建输出 JSON                                    │
│ AIPlayerEngine.cpp:258 (buildResult)                     │
├─────────────────────────────────────────────────────────┤
│ boss = runBossBattleJson(data.source) // Boss 战第二次调用│
│ collected[15][15] = false, resource = 0                  │
│                                                         │
│ for each step in path:                                   │
│   ├─ 首次访问? delta = scoreDelta(tile) → resource+=delta │
│   ├─ 是 Boss 触发格? → 判断胜/复活/死 → event 入 events[]│
│   ├─ frame = {step,row,col,tile,delta,resource}          │
│   ├─ path[] ← {row,col}                                  │
│   └─ frames[] ← frame                                    │
│                                                         │
│ [仅路径A] attachGreedyDebug(result, run)                  │
│   AIPlayerEngine.cpp:225                                 │
│   └─ 逐帧附加 debug {candidates[], rejected[],           │
│       closedSingletonGate, pocket}                       │
│                                                         │
│ 返回 JSON: { ok, mode, path[], frames[], events[],       │
│   resource, steps, score_ratio, finished, boss }         │
└─────────────────────────────────────────────────────────┘
                          │
                          ▼
┌─────────────────────────────────────────────────────────┐
│ 步骤 5: 输出                                             │
├─────────────────────────────────────────────────────────┤
│ 缓存: resultCache_[key] = result.dump()                  │
│ 桌面: return result.dump() → HostObject                 │
│       → Variant BSTR → WebView2 → JS JSON.parse()       │
│       → 前端渲染 path[] 和 frames[]                       │
│ 命令行: cout << result.dump() → 终端                     │
└─────────────────────────────────────────────────────────┘
```

## 3.3 调用链总览（一行表示一次函数调用）

```
SmokeTest/DesktopApp
  → AIPlayerEngine::RunRealtimeGreedy(jsonText)  AIPlayerEngine.cpp:319
    → parseMaze(jsonText)                         AIPlayerEngine.cpp:84 → MazeData
    → realtimeGreedyRun(data)                     RealtimeGreedyStrategy.cpp:790
      → MemoryGreedyAgent(data, algo, params)     RealtimeGreedyStrategy.cpp:37 (构造)
        → runBossBattleJson(source)               BossStrategy.cpp:293 ← 第一次
        → poseEstimator_.initialize()             Reward.cpp:329
      → agent.run()                               RealtimeGreedyStrategy.cpp:68
        → [主循环 900步] updateKnownMap → applyCurrentCell →
          poseEstimator.update → updateAlphaSmooth → selectBestPath → 走一步
    → buildResult(data, path, mode)               AIPlayerEngine.cpp:258
      → runBossBattleJson(source)                 BossStrategy.cpp:293 ← 第二次
    → attachGreedyDebug(result, run)              AIPlayerEngine.cpp:225
    → resultCache_[key] = result.dump()
    → return result.dump()                        ← JSON 字符串
```

---

# 第四章：路径 A — Reward 贪心

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
LocalKnownMap localMap_;              // AI 记忆 (仅已观察格) (Reward.h:65)
MapPoseEstimator poseEstimator_;      // 估计 AI 在 15x15 的位置 (Reward.h:100)
PathValueEvaluator evaluator_;        // 评分器 (Reward.h:149)
AgentState state_;                    // {resource, steps, collectedGold, alphaSmooth} (Reward.h:134)

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

## 3.3 主循环：`MemoryGreedyAgent::run()` (RealtimeGreedyStrategy.cpp:68)

### 3.3.1 循环前初始化

```
run() → GreedyRunResult {path, debugSteps, gameOver}  // RealtimeGreedyStrategy.cpp:68
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
│ [步骤 1] updateKnownMap(realCurrent, localCurrent)          §3.3.3  // RealtimeGreedyStrategy.cpp:175
│          扫描真实迷宫 3×3 → 写入 localMap_
├─────────────────────────────────────────────────────────────
│ [步骤 2] applyCurrentCell(localCurrent)                     §3.3.4  // RealtimeGreedyStrategy.cpp:229
│          结算资源、触发 Boss、标记访问
├─────────────────────────────────────────────────────────────
│ [步骤 3] poseEstimator_.update(localMap_, localCurrent)     §3.3.5  // Reward.cpp:365
│          更新入口假设评分，检测掩码激活
├─────────────────────────────────────────────────────────────
│ [步骤 4] state_.alphaSmooth =                               §3.3.6  // Reward.cpp:1024
│           evaluator_.updateAlphaSmooth(prevAlpha,            §3.3.6
│                                        localMap_, est)
├─────────────────────────────────────────────────────────────
│ [步骤 5] if pendingRevive_ || gameOver_:                    §3.3.7
│          处理 Boss 战后果 (复活/结束)
├─────────────────────────────────────────────────────────────
│ [步骤 6] selectedPath = selectBestPath(...)                 §3.4  // RealtimeGreedyStrategy.cpp:521
│          核心决策：reward 评分 → 选最佳目标 → 返回路径
├─────────────────────────────────────────────────────────────
│ [步骤 7] 执行一步
│          nextLocal = selectedPath[1]      (只走 1 步!)
│          localToReal_[nextLocal] → 更新 current
│          path.push_back(realCurrent)
```

### 3.3.3 updateKnownMap — 3×3 视野到局部记忆 (RealtimeGreedyStrategy.cpp:175)

```
updateKnownMap(realCurrent, localCurrent)  // RealtimeGreedyStrategy.cpp:175
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

### 3.3.4 applyCurrentCell — 资源结算 (RealtimeGreedyStrategy.cpp:229)

```
applyCurrentCell(localCurrent)  // RealtimeGreedyStrategy.cpp:229
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

### 3.3.5 poseEstimator_.update — 估计迷宫位置 (Reward.cpp:365)

```
MapPoseEstimator::update(localMap, localCurrent)  // Reward.cpp:365
│
├─ [Phase A] 种子类型检测 (seedKind_)  // Reward.h:120
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

### 3.3.6 updateAlphaSmooth — 动态探索权重 (Reward.cpp:1024)

```
updateAlphaSmooth(prevAlpha, localMap, poseEstimator) → double  // Reward.cpp:1024
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

## 3.4 核心决策：`selectBestPath()` (RealtimeGreedyStrategy.cpp:521)

这是整个系统最复杂的函数，每步调用一次，决定 "下一步往哪里走"。

### 3.4.1 完整调用流程

```
selectBestPath(localCurrent, realCurrent, step, debug) → vector<Position>  // RealtimeGreedyStrategy.cpp:521
│
├──────────────────────────────────────────────────────────────────
│ [Phase 0] 准备评分上下文                                       │
├──────────────────────────────────────────────────────────────────
│
├─ context = buildContext(localCurrent)                      // RealtimeGreedyStrategy.cpp:404
│   │
│   ├─ context.state = state_                        // 拷贝当前资源/步数
│   │
│   ├─ if localExit_ ≠ kInvalid:
│   │   └─ context.exitPath = routePath(localCurrent, localExit_)
│   │       └─ 从当前到出口在 localMap_ 上的路径 (不可达→空)
│   │   └─ else: context.exitPath = {}               // 出口未发现
│   │
│   └─ context.bossGatedAreaMaxTargets = bossGatedAreaMaxTargets(localCurrent)  // RealtimeGreedyStrategy.cpp:426
│       │
│       └─ [子函数] bossGatedAreaMaxTargets:  // RealtimeGreedyStrategy.cpp:426
│           ├─ [Step A] BFS from current, 视所有Boss为墙
│           │   → reachableWithoutBoss = 不穿过Boss即可到达的已知格集合
│           │
│           ├─ [Step B] 对每个"可从当前侧邻接到的 Boss":
│           │   BFS from boss (穿过它) → throughBoss
│           │   gated = throughBoss \ reachableWithoutBoss
│           │
│           └─ return gated  // 必须穿Boss才能到达的区域
│
├─ debug.qEff = evaluator_.computeQEff(context, localMap_)  // Reward.cpp:976
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
│ [Phase 1] 生成候选集 — candidateTargets(localCurrent) (RealtimeGreedyStrategy.cpp:336) │
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
├─ recordRejectedTargets(localCurrent, debug)  // RealtimeGreedyStrategy.cpp:366
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
│   ├─ [3.1] path = routePath(localCurrent, target)  // RealtimeGreedyStrategy.cpp:495
│   │   │
│   │   └─ 路由器分发 (详见第五章):
│   │       ├─ "greedy"/"smart" → BFS on localMap_  // shortestPathOnKnownMap (Reward.cpp:1133)
│   │       ├─ "dijkstra" → priority_queue Dijkstra  // dijkstraPath (DijkstraStrategy.cpp:97)
│   │       ├─ "astar"    → A* with Manhattan heuristic  // astarPath (AStarStrategy.cpp:106)
│   │       ├─ "branch_bound" → DFS + lower bound prune  // branchBoundPath (BranchBoundStrategy.cpp:150)
│   │       └─ "divide_conquer" → bidirectional BFS meet  // divideConquerPath (DivideConquerStrategy.cpp:189)
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
├─ shouldGoExit(context, bestScore, hasWorthwhile, hasNonNegative)?  // RealtimeGreedyStrategy.cpp:807
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

## 3.5 核心评分：`evaluate()` — 完整展开 (Reward.cpp:1079)

### 3.5.1 守卫检查 (任意不通过→返回 −∞)

```
evaluate(path, target, context, localMap, poseEstimator) → double  // Reward.cpp:1079
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

### 3.5.2 pathResourceDelta — 路径资源变化 (Reward.cpp:820)

```
pathResourceDelta(path, localMap) → int  // Reward.cpp:820
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

### 3.5.3 pathKeepsResourceNonNegative — 前缀资源约束 (Reward.cpp:848)

```
pathKeepsResourceNonNegative(path, currentResource, localMap) → bool  // Reward.cpp:848
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

### 3.5.4 informationProxy — 信息价值 (Reward.cpp:882)

```
informationProxy(target, localMap, poseEstimator, areaCap, forceAreaMax)  // Reward.cpp:882
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

### 3.5.5 futureGainMarginal — 边际尾部价值 (Reward.cpp:940)

```
futureGainMarginal(target, path, localMap) → double  // Reward.cpp:940
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

### 3.5.6 computeQEff — 步数代价系数 (Reward.cpp:976)

```
computeQEff(context, localMap) → double  // Reward.cpp:976
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

### 3.5.7 marginPenalty — 安全裕量 barrier (Reward.cpp:1002)

```
marginPenalty(projectedResource) → double  // Reward.cpp:1002
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
choosePocketFirstTarget(localCurrent, context, localMap, poseEstimator, evaluator)  // PocketAwareGreedy.cpp:163
│
├─ [Step 1] findPocket(localCurrent, ...)  // PocketAwareGreedy.cpp:105
│   │
│   ├─ hubCandidates = {localCurrent} ∪ 四方向邻居中 isWalkable 的
│   │
│   └─ for hub in hubCandidates:
│       └─ pocketCoinsForHub(hub, ...)  // PocketAwareGreedy.cpp:74
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

## 3.9 Closed Singleton Lookahead Gate (完整展开) (ClosedSingletonLookaheadGate.cpp:356)

### 3.9.1 触发条件

```
applyClosedSingletonLookaheadGate(request) → ClosedSingletonGateResult  // ClosedSingletonLookaheadGate.cpp:356
│
├─ [Guard 1] request.localMap / poseEstimator / evaluator 均非空
├─ [Guard 2] findTopCandidate(candidates) 存在  // ClosedSingletonLookaheadGate.cpp:315
│   └─ candidateA = 当前 top-1
├─ [Guard 3] isClosedSingletonCandidate(candidateA)  // ClosedSingletonLookaheadGate.cpp:58
│   └─ 判定: unknownComponentSum == 1 && score有效 && path非空 && tile≠"E"
│   └─ 意义: 这个候选走完只能打开 1 个新格子 — 直接收益极低
│
└─ 不满足任一 → return {hasSelection=false} (不干预)
```

### 3.9.2 主逻辑

```
│
├─ findBestNonClosedCandidate(candidates, candidateA) → B  // ClosedSingletonLookaheadGate.cpp:339
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

# 第五章：路由算法详解

## 4.1 BFS on localMap (greedy/smart 默认) (Reward.cpp:1133)

```
PathValueEvaluator::shortestPathOnKnownMap(start, target, localMap) → path  // Reward.cpp:1133
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

## 4.2 A\* on localMap (AStarStrategy.cpp:106)

```
astarPath(localMap, start, target) → path  // AStarStrategy.cpp:106
// 全局重载: astarPath(data, start, target) → path  // AStarStrategy.cpp:26
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

## 4.3 Dijkstra on localMap (DijkstraStrategy.cpp:97)

与 A\* 相同，但 h(pos) = 0 (无启发式)，退化为 Uniform-Cost Search。等权网格上等价 BFS 但使用优先队列。
全局重载: dijkstraPath(data, start, target) → path (DijkstraStrategy.cpp:25)

## 4.4 Branch & Bound on localMap (BranchBoundStrategy.cpp:150)

```
branchBoundPath(localMap, start, target) → path  // BranchBoundStrategy.cpp:150
// 全局重载: branchBoundPath(data, start, target) → path  // BranchBoundStrategy.cpp:131
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

## 4.5 Divide & Conquer (双向 BFS) (DivideConquerStrategy.cpp:189)

```
divideConquerPath(localMap, start, target) → path  // DivideConquerStrategy.cpp:189
// 全局重载: divideConquerPath(data, start, target) → path  // DivideConquerStrategy.cpp:170
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

# 第六章：路径 B — 全局最近邻

## 5.1 planAdventurePath("smart") — 完整流程 (ShortestPathStrategy.cpp:50)

```
planAdventurePath(data, "smart") → vector<Position>  // ShortestPathStrategy.cpp:50
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

# 第七章：Boss 战系统

## 6.1 顶层: runBossBattleJson (BossStrategy.cpp:703)

```
runBossBattleJson(source) → Json  // BossStrategy.cpp:703
│
├─ [校验] source 含 "B" (HP数组) 和 "PlayerSkills" (技能数组)
├─ [解析] bossHPs = B[], skills = [Skill{id, damage, cd}, ...]  // Skill (GameTypes.h:21)
├─ readMinRounds(source) → -1 或整数  // BossStrategy.cpp:40
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

## 6.2 solveCurrentBossByLightCapacity — 单 Boss 分支限界 (BossStrategy.cpp:596)

```
solveCurrentBossByLightCapacity(hp, cooldown, skills, reserveSlack, phaseTurnLimit)  // BossStrategy.cpp:596
│
├─ maxDamage = max(skills[i].damage)
├─ minForcedDamage = minimumForcedDamagePerTurn(skills)  // BossStrategy.cpp:220
│   └─ 冷却=0且伤害>0的技能中取最小伤害 (不能等待时必须出招)
│
├─ lowerBound = ceil(hp / maxDamage)               // 理论最小回合
├─ upperBound = phaseTurnLimit + reserveSlack      // 最大允许回合
│
├─ bestPlan = {}  // BossPlanCandidate (BossStrategy.cpp:12)
│
├─ for turns = lowerBound .. upperBound:            // 逐精确回合枚举
│   │
│   ├─ candidates = enumerateKillExactTurns(hp, cooldown, skills, turns, memo)  // BossStrategy.cpp:245
│   │   │
│   │   └─ [子函数] BFS 逐层枚举:
│   │       ├─ turn 0: states = [{hp, startCooldown, {}}]
│   │       ├─ for t=0..exactTurns-1:
│   │       │   ├─ 本层去重: liveStateKey(hp, cooldown) → 保留最小序列  // BossStrategy.cpp:111
│   │       │   ├─ for state in states:
│   │       │   │   └─ for action in availableBossActions(cooldown):  // BossStrategy.cpp:199
│   │       │   │       ├─ applyBossAction → (nextHp, nextCd)  // BossStrategy.cpp:174
│   │       │   │       ├─ !lastTurn && nextHp≤0? → skip (不能提前死)
│   │       │   │       ├─ lastTurn && nextHp>0? → skip (必须死)
│   │       │   │       └─ 通过 → 记入下一层 (去重: 同 cooldownAfter 保留字典序最小)
│   │       │   └─ states = 下一层
│   │       └─ return 所有合法击杀终态
│   │
│   └─ for cand in candidates:
│       │
│       ├─ lightScore = evaluateSuffixLightCapacity(cand, skills, remainingBossCount, memo)  // BossStrategy.cpp:497
│       │   │
│       │   └─ [子函数] 滚动时域评估:
│       │       ├─ 已知后缀 (remainingBossCount>0):
│       │       │   └─ 用 cand.cooldownAfter 继续枚举后续 Boss 的最小回合
│       │       │   └─ knownCapacity = 剩余回合 − Σ(后续Boss最小回合)
│       │       │
│       │       └─ 未知后缀:
│       │           └─ suffixCapacityThreshold(remainingBossCount)  // BossStrategy.cpp:432
│       │               └─ 均匀分配截止时间片 → 每片 avgDamage
│       │               └─ return min_over_slices(avgDamage)
│       │       │
│       │       └─ lightScore = min(knownCapacity, unknownThreshold)
│       │
│       └─ betterBossPlanByLightCapacity(bestPlan, {turns, lightScore, ...})?  // BossStrategy.cpp:536
│           └─ 比较链: lightScore > turns > cooldownCost > readyDamage > sequence
│           └─ 是? → bestPlan = cand
│
└─ return bestPlan
```

---

# 第八章：Reward 公式详解 — evaluate() 的每一步

本章逐行拆解 `Reward.cpp:1079` 中 `PathValueEvaluator::evaluate()` 的完整计算过程，包括所有守卫、五项子公式的计算细节，以及每个子公式内部的数据流。

## 8.1 主函数骨架 (`Reward.cpp:1079-1112`)

```cpp
double evaluate(path, target, context, localMap, poseEstimator) {
    // [1] 5 个守卫 (任一项不通过 → -1e18)
    // [2] 计算 5 个分项
    // [3] 组装返回
}
```

## 8.2 五项守卫 (`Reward.cpp:1083-1098`)

```
守卫 1: path.size() ≤ 1  →  -1e18
守卫 2: 路径上任一格 !isWalkableForPlanning  →  -1e18
守卫 3: R + pathResourceDelta(path) < 0  →  -1e18
守卫 4: !pathKeepsResourceNonNegative(path, R, map)  →  -1e18
守卫 5: δR≤0 && I_proxy==0  →  -1e18  (零收益零信息)
```

### 守卫 4 详解 — 前缀资源非负 (`Reward.cpp:848-858`)

这是比守卫 3 更强的约束：路径上**每一步累计资源都不能为负**。

```cpp
int r = currentResource;
for (i=1; i<path.size(); i++):
    tile=="G" && !collected? → r += 50
    tile=="T" && !triggered? → r -= 30
    if (r < 0) → return false  // "先踩陷阱再吃金币"被禁止
return true
```

## 8.3 第一项：ΔR — 路径资源变化 (`Reward.cpp:1092`)

调用 `pathResourceDelta()` (`Reward.cpp:820`)：

```cpp
int delta = 0;
for (i=1; i<path.size(); i++):  // 跳过 path[0] (当前位置)
    tile=="G" && !isCollected? → delta += 50
    tile=="T" && !isTriggered? → delta -= 30
return delta
```

**关键**：已拾取的金币和已触发的陷阱返回 0——一次性资源不重复结算。

## 8.4 第二项：ω_I·α·I_proxy (`Reward.cpp:1104-1108`)

### I_proxy (`Reward.cpp:882-930`)

```
Step A: 统计当前 localMap 中已观察区域的金币/陷阱密度
    for pos in observedPositions():
        obsCount++, tile=="G"? goldCount++, tile=="T"? trapCount++
    ρ̂_G = (goldCount+1) / (obsCount+10)
    ρ̂_T = (trapCount+1) / (obsCount+10)
    v_area = max(50×ρ̂_G − 30×ρ̂_T, 0)
    ρ_area = clip(v_area/50, 0.001, 1.0)   ← 未知格"价值密度"

Step B: Boss-gated 特殊路径
    if forceAreaMax && unknownExtensionTouchesMazeEdge:
        return κ_u × bossEdgeAreaBonus × ρ_area
        = 60 × 15 × ρ_area

Step C: 正常 BFS 展开
    sizes = poseEstimator.unknownComponentSizesTouchingView(target, map, ceil(areaCap))
        以 target 为 BFS 起点:
        - observed 格视为障碍
        - 掩码边界视为障碍  
        - 展开到 ceil(areaCap) 个未知格 → 立即返回
    componentVal = Σ min(size, activeCap) × ρ_area
    activeCap = 出口已知? 1.5 : 12

    return κ_u × componentVal = 60 × Σ...
```

### α_smooth (`Reward.cpp:998-1035`)

```
Step 1: 统计已观察格中 goldCount, trapCount
Step 2: ρ̂_G = (goldCount+1)/(obsCount+10), ρ̂_T = (trapCount+1)/(obsCount+10)
Step 3: v_unk = 50×ρ̂_G − 30×ρ̂_T
Step 4: ρ_U = estimatedUnknownCount() / 225
Step 5: raw = 4.0 × (1+1.0×ρ_U+1.0×max(v_unk,0)/50) / (1+2.0×ρ̂_T)
Step 6: clipped = clamp(raw, 1.0, 8.0)
Step 7: return 0.8×prevAlpha + 0.2×clipped
```

### 组装

```cpp
info = informationProxy(target, localMap, poseEstimator, areaCap, forceAreaMax);
// 行 1108: + 0.175 × state_.alphaSmooth × info
```

## 8.5 第三项：β·V_tail (`Reward.cpp:1109`)

调用 `futureGainMarginal()` (`Reward.cpp:934`)：

```cpp
// Step 1: 收集 path 上的金币 → coinsOnPath (这些已被 pathResourceDelta 计入)
for (i=1; i<path.size(); i++):
    tile=="G" && !collected? → coinsOnPath.insert(pos)

// Step 2: 遍历所有已知金币（排除已在路径上的），取最大边际价值
best = 0.0
for coin in localMap.knownCoins():
    if coin in coinsOnPath → skip
    coinPath = BFS(target → coin on localMap)
    if coinPath.empty() → skip
    value = 50.0 / (len(coinPath) + 1)
    best = max(best, value)
return best
```

**含义**：走到 target 后，最值钱的已知金币有多"近"。越近的金币价值越高（分母小）。

## 8.6 第四项：−η_q·q_eff·len (`Reward.cpp:1110`)

### q_eff (`Reward.cpp:966-980`)

```cpp
q_ref = R / (L + 1e-6)                     // 当前 ratio 估计
if exitPath 非空:
    exitDelta = pathResourceDelta(exitPath) // 走出口还能捡多少
    q_ref = (R + exitDelta) / (L + len(exitPath) + 1e-6)  // 出口 ratio

return max(q_ref, 1.0)                     // q_min=1.0 保证代价下限
```

### len (`Reward.cpp:15-18`)

```cpp
return path.empty() ? 0 : path.size() - 1  // 步数 = 节点数 - 1
```

### 组装

```cpp
// 行 1110: - 0.82 × qEff × len(path)
```

## 8.7 第五项：−φ_margin (`Reward.cpp:1111`)

调用 `marginPenalty()` (`Reward.cpp:985`)：

```cpp
if projectedResource >= 30 → return 0.0
margin = (30.0 - projectedResource) / 30.0
return 8.0 × margin × margin
```

| R' | φ_margin |
|---|---|
| 30+ | 0 |
| 20 | 8×(10/30)² = 0.89 |
| 10 | 8×(20/30)² = 3.56 |
| 0 | 8×(30/30)² = 8.0 |

## 8.8 最终组装 (`Reward.cpp:1112`)

```cpp
return delta                                          // ΔR
     + 0.175 × context.state.alphaSmooth × info       // ω_I·α·I_proxy
     + 1.23  × tail                                   // β·V_tail
     − 0.82  × qEff × len                             // η_q·q_eff·len
     − margin;                                         // φ_margin
```

## 8.9 数值追踪示例

假设某步 AI 看到一枚金币 (1,1)=G，当前位置 (0,1)=S，R=0，L=0：

```
path = [(0,1), (1,1)]  →  len = 1
delta = pathResourceDelta(path) = 50  // 金币未拾取
projectedR = 0 + 50 = 30  →  φ_margin = 0  (≥30)
info = I_proxy(target) = 60 × (展开的未知格数量) × ρ_area
     开局 ρ̂_G≈0.05, ρ̂_T≈0.05, ρ_area≈(50×0.05-30×0.05)/50=0.02
     假设展开 3 个未知格: info = 60 × 3 × 0.02 = 3.6
α_smooth = 4.0 (初始值)
qEff = max(0/1e-6, 1.0) = 1.0
tail = 0 (没有其他已知金币)

Score = 50 + 0.175×4.0×3.6 + 1.23×0 − 0.82×1.0×1 − 0
      = 50 + 2.52 + 0 − 0.82 − 0
      = 51.70
```

---

# 第九章：15×15 掩码估计系统 (MapPoseEstimator)

## 9.0 函数速查表

全部实现在 `Reward.cpp`，类定义在 `Reward.h:100-125`。

| 方法 | Reward.cpp 行号 | 重要性 | 作用 |
|---|---|---|---|
| `MapPoseEstimator()` | 329 | 构造 | 调用 `initialize()` |
| `initialize()` | 343 | ★★ | 创建默认 `{7,7}` 入口，`maskActive_=false` |
| `update(map,cur)` | 365 | ★★★ 关键 | 5 阶段：种子检测→包围盒→假设排除→掩码激活→重建预估观察集 |
| `best()` | 546 | 工具 | getter，返回最优假设 |
| `isMaskActive()` | 560 | ★★ | getter，掩码是否生效 |
| `localToEstimatedGlobal(p)` | 574 | ★★ | 局部→15×15 坐标变换，调用 `mapWithHypothesis()` |
| `isInsideEstimatedMaze(p)` | 588 | ★★ | 掩码激活? `insideEstimated(mapped)` : `true` |
| `estimatedUnknownCount()` | 603 | ★★ | `225 − observedEstimated_.size()`，供 α 计算用 |
| `estimatedObservedCount()` | 617 | 工具 | 去重已观察计数，供停止规则用 |
| `unknownComponentSizesTouchingView(t,map,cap)` | 635 | ★★★ 关键 | BFS 展开未知连通块，受掩码边界裁剪 |
| `unknownExtensionTouchesMazeEdge(t,map)` | 711 | ★ | Boss-gated 判定：未知延伸是否触达边缘 |
| `mapWithHypothesis(p,hp)` | 770 | ★★ | 按假设旋转+平移局部坐标 |

### 掩码种子类型 (`Reward.h` 私有)

```cpp
enum class MaskSeedKind { Internal, Top, Bottom, Left, Right,
                          TopLeft, TopRight, BottomLeft, BottomRight };
```

### 成员变量 (`Reward.h:115-121`)

```cpp
vector<MapEmbeddingHypothesis> hypotheses_;  // 候选入口假设 (1~13个)
MapEmbeddingHypothesis best_;                // 最优假设
set<Position> observedEstimated_;            // 映射去重后的已观察估计坐标
bool maskSeeded_ = false;                    // 出生边缘种子是否已检测
bool maskActive_ = false;                    // 掩码是否生效
MaskSeedKind seedKind_ = Internal;           // 出生边缘类型
```

---

## 9.1 问题背景

AI 只通过 3×3 视野观察迷宫，不知道自己在 15×15 坐标系中的绝对位置。但 `informationProxy()` 需要知道 "走到某个候选目标后能展开多少未知格"——如果不限制 BFS 范围，会把整个迷宫未探索区域都算进去，严重高估 I_proxy。

**掩码系统的目标**：仅凭局部观察，推断 AI 在 15×15 迷宫中的大致位置和朝向，从而用一个 **15×15 的掩码框**去裁剪未知连通块的 BFS 展开范围。

## 9.2 核心数据结构

### 9.2.1 Direction 枚举 (Reward.h:13)

```cpp
enum class Direction { Up, Down, Left, Right };  // Reward.h:13
// 表示 AI 进入迷宫后面对的方向（从入口往迷宫内部看）
// 这是局部坐标到估计 15×15 坐标之间做旋转变换的关键参数
```

### 9.2.2 MapEmbeddingHypothesis — 入口假设 (Reward.h:92)

```cpp
struct MapEmbeddingHypothesis {  // Reward.h:92
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

### 9.2.3 MapPoseEstimator — 估计器本体 (Reward.h:100)

```cpp
class MapPoseEstimator {  // Reward.h:100
    static constexpr int kEstimatedSize = 15;           // 固定 15×15 (Reward.h:118)

    enum class MaskSeedKind {  // Reward.h:120
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

## 9.3 掩码生命周期

掩码经历四个阶段：**初始化 → 种子检测 → 候选排除 → 激活裁剪**

## 9.4 阶段一：初始化 (`initialize()`) (Reward.cpp:343)

```
initialize()  // Reward.cpp:343
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

## 9.5 阶段二：种子检测 (`update()`, 首次调用)

首次调用 `update()` 时，通过出生点周围 3×3 视野中的 **outside 标记**判断 AI 从迷宫哪条边进入。

### 9.5.1 outside 标记的来源

`updateKnownMap()` 扫描 3×3 时，超出 `realInBounds()` 的格子调用 `localMap_.setOutside(localPos)`：

```cpp
// cells_[localPos] = {observed=true, outside=true, tile="#"}
```

outside 格的意义：这个方向在迷宫**外部**——说明 AI 靠近迷宫的这一侧边界。

### 9.5.2 种子检测逻辑

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

### 9.5.3 坐标映射函数 (`mapWithHypothesis`) (Reward.cpp:770)

```
mapWithHypothesis(localPos, hypothesis) → Position (15×15 坐标)  // Reward.cpp:770
│   // 声明: Reward.h:129
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

## 9.6 阶段三：候选假设排除 (`update()`, 后续调用)

每次 `update()` 执行后，用**新观察到**的格子信息排除不兼容的入口假设。

### 9.6.1 排除时机

```
hasFullHorizontal = (maxCol − minCol + 1) ≥ 15   // 水平跨度覆盖全宽
hasFullVertical   = (maxRow − minRow + 1) ≥ 15   // 垂直跨度覆盖全高
canPruneHorizontal = horizSpan ≥ 14               // 水平跨度够大
canPruneVertical   = vertSpan ≥ 14                // 垂直跨度够大

canPrune = (Top/Bottom && canPruneHorizontal) ||
           (Left/Right && canPruneVertical)
```

只有观察跨度 ≥ 14 格时才做排除——此前信息不足，强行排除可能把正确假设也删掉。

### 9.6.2 三种检查

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

## 9.7 阶段四：掩码激活判定

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

## 9.8 阶段五：重建预估观察集

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

## 9.9 掩码在 I_proxy 中的作用

### 9.9.1 裁剪未知连通块 BFS

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

### 9.9.2 动态 α 中的未知比例

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

### 9.9.3 isInsideEstimatedMaze 的双重角色 (Reward.cpp:588)

```
isInsideEstimatedMaze(localPos) → bool  // Reward.cpp:588
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

## 9.10 完整生命周期示例

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

## 9.11 关键设计原则

1. **不读真实坐标**：掩码完全从局部观察推断，AI 不应知道自己在迷宫中的真实 (row, col)
2. **逐步收敛**：从无掩码 → 部分边界阻挡 → 完整 15×15 掩码。早期宁可高估未知面积也不做过度裁剪
3. **安全回退**：掩码激活后发现映射矛盾 → 立即 `maskActive_ = false`，回到无掩码状态
4. **角落优先**：角出生 (TopLeft 等) 从第一步就激活掩码——因为入口位置唯一确定
5. **边界墙校验**：映射到 15×15 边界的格必须是墙类型 → 如果不能满足，该入口假设被排除

---

# 第十章：基础数据结构

本章列出所有核心类型，按文件归属排列，标注精确 `file:line`。

## 10.1 GameTypes.h — 基础类型与常量

**Position & 常量** (`GameTypes.h:12-18`)：
```cpp
using Position = pair<int,int>;  // first=行, second=列
using Json = nlohmann::json;
constexpr int kGoldValue=50, kTrapValue=-30;
constexpr Position kInvalid{-1000000000,-1000000000};  // 无效坐标哨兵
constexpr array<Position,4> kDirs={{{1,0},{-1,0},{0,1},{0,-1}}}; // 下上右左
```

**Skill** (`GameTypes.h:21-25`)：`{int id, damage, cooldown}` — 冷却>0 时不可使用

**MazeData** (`GameTypes.h:28-35`)：`{Json source, grid[15][15], start, exit, bosses[], golds[]}`。仅路径B读grid；路径A只用start和source

**passable()** (`GameTypes.h:38`) 界内&&≠"#"&&≠"B"；**scoreDelta()** (`GameTypes.h:45`) G→+50, T→-30

## 10.2 Reward.h — Reward 系统

### Direction (`Reward.h:13-18`) `{Up, Down, Left, Right}` — 决定局部→15×15旋转变换

### RewardParameters (`Reward.h:21-50`) 32个可调参数，默认读`RewardConfig.h`

### LocalCell (`Reward.h:54-63`) `{tile, observed, outside, visited, collected, triggered, bossTrigger}` — 单格AI记忆，存于`LocalKnownMap::cells_`(Reward.h:89)

### LocalKnownMap (`Reward.h:65-88`, impl `Reward.cpp:31-317`)
| 方法 | cpp行 | 作用 |
|---|---|---|
| setObserved | 31 | 记录观察格 |
| setOutside | 48 | 标记越界 |
| markBossTriggers | 65 | 标记四邻触发区 |
| clearBoss | 81 | 击败后变空地 |
| markVisited/Collected/Triggered | 97/111/127 | 状态标记 |
| isWalkableForPlanning | 210 | observed&&≠"#"&&≠"B" |
| tile/observedPositions/knownCoins | 226/241/311 | 查询 |

### MapEmbeddingHypothesis (`Reward.h:92-97`) `{entry, inwardDirection, score, feasible}`

### MapPoseEstimator (`Reward.h:100-125`, impl `Reward.cpp:329-783`)
`hypotheses_[]`, `best_`, `observedEstimated_`, `maskSeeded_`, `maskActive_`, `seedKind_`
方法: `initialize(329)`, `update(365)`, `isMaskActive(560)`, `localToEstimatedGlobal(574)`, `isInsideEstimatedMaze(588)`, `estimatedUnknownCount(603)`, `unknownComponentSizesTouchingView(636)`

### AgentState (`Reward.h:134-139`) `{resource, steps, collectedGold, alphaSmooth}`

### PathValueContext (`Reward.h:142-147`) `{AgentState state, exitPath, bossGatedAreaMaxTargets}`

### PathValueEvaluator (`Reward.h:149-163`, impl `Reward.cpp:785-1167`)
`parameters_`
方法(cpp行): `pathResourceDelta(820)`, `pathKeepsResourceNonNegative(848)`, `informationProxy(882)`, `futureGainMarginal(934)`, `computeQEff(966)`, `marginPenalty(985)`, `updateAlphaSmooth(998)`, `evaluate(1079)`, `shortestPathOnKnownMap(1165)`

## 10.3 RealtimeGreedyStrategy.h — 调试结构体

- **GreedyCandidateDebug** (`RealtimeGreedyStrategy.h:14-29`) `{score,deltaR,I_proxy,tail,qEff,len,margin,pR,selected,...}`
- **GreedyRejectedDebug** (`RealtimeGreedyStrategy.h:32-38`) `{target,tile,reason,pathLength}`
- **GreedyStepDebug** (`RealtimeGreedyStrategy.h:41-55`) `{step,alpha,qEff,decision,candidates[],rejected[],closedSingletonGate,pocket}`
- **GreedyRunResult** (`RealtimeGreedyStrategy.h:58-63`) `{path[],debugSteps[],gameOver}`

## 10.4 ClosedSingletonLookaheadGate.h

- **ClosedSingletonGateCandidate** (`ClosedSingletonLookaheadGate.h:12-24`)
- **ClosedSingletonGateDebug** (`ClosedSingletonLookaheadGate.h:27-44`) `{checked,triggered,candidateA,rewardA,bestNonClosedB,rewardB,combinedA,allowed,reason}`
- **ClosedSingletonGateResult** (`ClosedSingletonLookaheadGate.h:53-58`) `{hasSelection,changed,selectedTarget/Path/Score}`
- **ClosedSingletonGateRequest** (`ClosedSingletonLookaheadGate.h:59-69`) `{localCurrent,exit,context,localMap,poseEstimator,evaluator,candidates[]}`

## 10.5 PocketAwareGreedy.h

- **PocketCandidateDebug** (`PocketAwareGreedy.h:12-26`) `{baseScore,ownIproxy,remainI,scoreFirst,...}`
- **PocketDebug** (`PocketAwareGreedy.h:28-39`)
- **PocketDecision** (`PocketAwareGreedy.h:41-47`) `{enabled,target,path,score,debug}`

## 10.6 BossStrategy.cpp — Boss 战

- **BossPlanCandidate** (`BossStrategy.cpp:12-22`) `{ok,turns,sequence,cooldownAfter,lightScore,remainingTurnsAfter,...}`
- **PlannerMemo** (`BossStrategy.cpp:24-26`) `{killExact: map<string,vector<BossPlanCandidate>>}`

## 10.7 生命周期

```
JSON→MazeData(GameTypes.h:28)
  Agent: Skill(GameTypes.h:21), BossPlanCandidate(BossStrategy.cpp:12), LocalKnownMap(Reward.h:65)←空
  每步: LocalCell(Reward.h:54)←3×3, AgentState(Reward.h:134)←更新
        PathValueContext(Reward.h:142), evaluate()→double
        GreedyCandidateDebug(RealtimeGreedyStrategy.h:14)
        ClosedSingletonGateResult(ClosedSingletonLookaheadGate.h:53), PocketDecision(PocketAwareGreedy.h:41)
  输出: GreedyStepDebug→JSON frames[].debug
```

---

# 第十一章：参数总表

| 参数 | 值 | 位置 | 含义 |
|---|---|---|---|
| ω_I | 0.175 | ω_I·α·I_proxy (RewardConfig.h:39) | 信息价值折扣 |
| α_0~min~max | 4.0/1.0/8.0 | α_raw / clip (RewardConfig.h:42) | 动态探索权重 |
| θ | 0.8 | α_smooth EMA (RewardConfig.h:49) | 平滑系数 |
| β | 1.23 | β·V_tail (RewardConfig.h:52) | 尾部金币权重 |
| κ_u | 60 | I_proxy (RewardConfig.h:55) | 连通块面积缩放 |
| A_max | 12 | 出口未知面积上限 (RewardConfig.h:58) | |
| knownExitAreaCap | 1.5 | 出口已知面积上限 (RewardConfig.h:64) | |
| bossEdgeAreaBonus | 15 | Boss-gated 面积加成 (RewardConfig.h:61) | |
| η_q | 0.82 | η_q·q_eff·len (RewardConfig.h:73) | 长度代价缩放 |
| q_min | 1.0 | max(q_ref,1.0) (RewardConfig.h:70) | 步数代价下限 |
| m_safe | 30 | φ_margin 安全线 (RewardConfig.h:76) | |
| λ_m | 8.0 | φ_margin 强度 (RewardConfig.h:79) | |
| τ | 5.0 | shouldGoExit (RewardConfig.h:100) | 停止探索 |
| switchMargin | 5.0 | 目标保持 (RewardConfig.h:82) | 切换阈值 |
| γ_closed | 1.0 | combinedA=reward+γ×c_A (RewardConfig.h:85) | |
| margin_closed | -20 | combinedA>reward(B)+m (RewardConfig.h:88) | |
| pocketRadius | 2 | 金币到hub距离 (RewardConfig.h:91) | |
| μ | 0.2 | remainI 折扣 (RewardConfig.h:94) | |
| λ_remain | 0.2 | remainI 权重 (RewardConfig.h:97) | |
| λ | 10.0 | 密度估计平滑 (RewardConfig.h:112) | |
| λ_G/λ_T | 1.0/1.0 | 金币/陷阱先验 (RewardConfig.h:115) | |
| ρ_area_min | 0.001 | ρ_area clip 下界 (RewardConfig.h:67) | |
| ε | 1e-6 | 防除零 (RewardConfig.h:121) | |

---

# 第十二章：Reward.cpp 文件全览

本章列出 `Reward.cpp`（1167 行）中**所有函数**，按所属类分组，标注重要程度和相互调用关系。

## 12.1 匿名 namespace 工具函数 (`Reward.cpp:9-18`)

| 函数 | 行号 | 重要性 | 作用 |
|---|---|---|---|
| `insideEstimated(pos)` | 10 | 工具 | 判断 15×15 坐标是否在 `[0,14]×[0,14]` 内 |
| `pathLength(path)` | 15 | 工具 | 路径步数 = `path.size()-1`，空路径返回 0 |

这两个函数是纯粹的数值/坐标工具，不调用任何其他函数，被多个类的方法复用。

## 12.2 LocalKnownMap 方法 (`Reward.cpp:31-317`)

共 14 个方法。全部是 `LocalKnownMap` (`Reward.h:65`) 的成员函数。

### 写入类（修改 cells_ 状态）

| 方法 | 行号 | 重要性 | 作用 | 调用关系 |
|---|---|---|---|---|
| `setObserved(pos,tile)` | 31 | ★★ 核心 | 记录 3×3 观察到的格子 | 被 `updateKnownMap()` 调用 |
| `setOutside(pos)` | 48 | ★ 重要 | 标记越界格（outside=true） | 被 `updateKnownMap()` 调用 |
| `markBossTriggers(boss)` | 65 | ★ 重要 | 标记 Boss 四邻为触发区 | 被 `updateKnownMap()` 调用 |
| `clearBoss(boss)` | 81 | ★ 重要 | Boss 击败后 tile 变 `" "` | 被 `triggerAdjacentBoss()` 调用 |
| `markVisited(pos)` | 97 | ★★ 核心 | 标记已踩过 | 被 `applyCurrentCell()` 调用 |
| `markCollected(pos)` | 111 | ★★ 核心 | 标记金币已拾取 | 被 `applyCurrentCell()` 调用 |
| `markTriggered(pos)` | 125 | ★★ 核心 | 标记陷阱已触发 | 被 `applyCurrentCell()` 调用 |

### 查询类（只读 cells_ 状态）

| 方法 | 行号 | 重要性 | 作用 |
|---|---|---|---|
| `has(pos)` | 139 | 工具 | 格子是否在 cells_ map 中 |
| `isObserved(pos)` | 153 | ★★ 核心 | 是否已被 3×3 观察 |
| `isOutside(pos)` | 168 | 工具 | 是否越界 |
| `isVisited(pos)` | 183 | ★★ 核心 | 是否已踩过（决定能否成为候选目标） |
| `isCollected(pos)` | 198 | ★★ 核心 | 金币是否已被拾取 |
| `isTriggered(pos)` | 213 | ★★ 核心 | 陷阱是否已被触发 |
| `isBossTrigger(pos)` | 228 | ★ 重要 | 是否为 Boss 触发区 |
| `isWalkableForPlanning(pos)` | 243 | ★★★ 关键 | 可通行=observed && ≠"#" && ≠"B"，路径搜索的核心条件 |
| `tile(pos)` | 260 | ★★ 核心 | 读取已知类型，未观察返回 "U" |
| `observedPositions()` | 275 | ★★★ 关键 | 返回所有已观察格坐标，候选生成和 α 统计的唯一数据源 |
| `outsidePositions()` | 293 | 工具 | 返回越界格，掩码种子检测用 |
| `knownCoins()` | 311 | ★★ 核心 | 返回已知未拾取金币，`futureGainMarginal()` 用 |

## 12.3 MapPoseEstimator 方法 (`Reward.cpp:329-783`)

| 方法 | 行号 | 重要性 | 作用 | 调用关系 |
|---|---|---|---|---|
| `MapPoseEstimator()` | 329 | 构造 | 调用 `initialize()` | → `initialize()` |
| `initialize()` | 343 | ★★ | 创建默认 `{7,7}` 入口假设 | 被构造函数调用 |
| `update(map,cur)` | 365 | ★★★ 关键 | 5 阶段：种子检测→包围盒→假设排除→掩码激活→重建预估观察集 | → `mapWithHypothesis()`, `insideEstimated()`, `observedPositions()`, `outsidePositions()` |
| `best()` | 546 | 工具 | 返回最优假设 | getter |
| `isMaskActive()` | 560 | ★★ | 掩码是否已激活 | getter |
| `localToEstimatedGlobal(p)` | 574 | ★★ | 局部→15×15 坐标变换 | → `mapWithHypothesis()` |
| `isInsideEstimatedMaze(p)` | 588 | ★★ | 掩码激活? `insideEstimated(mapped)` : true | → `localToEstimatedGlobal()` |
| `estimatedUnknownCount()` | 603 | ★★ | 225 − `observedEstimated_.size()` | 被 `updateAlphaSmooth()` 调用 |
| `estimatedObservedCount()` | 617 | 工具 | 已观察格去重计数 | 被 `shouldGoExit()` 调用 |
| `unknownComponentSizesTouchingView(t,map,cap)` | 635 | ★★★ 关键 | BFS 展开未知连通块 | → `localToEstimatedGlobal()`, `insideEstimated()`, `observedPositions()` |
| `unknownExtensionTouchesMazeEdge(t,map)` | 711 | ★ | 未知延伸是否触达边界（Boss-gated 判定用） | → `localToEstimatedGlobal()` |
| `mapWithHypothesis(p,hp)` | 770 | ★★ | 按假设旋转+平移局部坐标 | 被 `update()` 和 `localToEstimatedGlobal()` 调用 |

## 12.4 PathValueEvaluator 方法 (`Reward.cpp:794-1167`)

| 方法 | 行号 | 重要性 | 作用 | 被谁调用 |
|---|---|---|---|---|
| `PathValueEvaluator(params)` | 794 | 构造 | 存储参数副本 | |
| `parameters()` | 805 | 工具 | 返回参数只读引用 | `selectBestPath()` 等 |
| `pathResourceDelta(path,map)` | 820 | ★★★ 关键 | 路径资源变化 Σ(50G-30T)，跳过 path[0] | `evaluate()`, `computeQEff()`, `buildResult()` |
| `pathKeepsResourceNonNegative(path,R,map)` | 848 | ★★★ 关键 | 前缀资源非负检查 | `evaluate()` |
| `informationProxy(t,map,est,cap,force)` | 882 | ★★★ 关键 | I_proxy = 60×Σmin(|C|,cap)×ρ_area | `evaluate()`, `fallbackPath()` |
| `futureGainMarginal(t,path,map)` | 934 | ★★ 核心 | V_tail = max 50/(dist+1) 排除路径上金币 | `evaluate()` |
| `computeQEff(ctx,map)` | 966 | ★★★ 关键 | q_eff = max(q_ref, 1.0) | `evaluate()`, `selectBestPath()`, Pocket |
| `marginPenalty(r)` | 985 | ★★ 核心 | φ = 8×((30-r)/30)² | `evaluate()` |
| `updateAlphaSmooth(prev,map,est)` | 998 | ★★★ 关键 | 动态 α 更新 (密度统计+EMA平滑) | `run()` 每步 |
| `evaluate(path,t,ctx,map,est)` | 1079 | ★★★ 关键 | 主评分函数，组装 5 项 | `selectBestPath()`, `shouldGoExit()`, Gate, Pocket |
| `shortestPathOnKnownMap(s,t,map)` | 1133 | ★★★ 关键 | BFS 在 localMap_ 上求最短路 | `routePath()` (greedy/smart) |

## 12.5 函数调用关系图

```
run() 每步:
  ├→ updateAlphaSmooth() → observedPositions(), estimatedUnknownCount(), insideEstimated()
  └→ selectBestPath()
       ├→ candidateTargets() → routePath() → shortestPathOnKnownMap()
       └→ evaluate() [每个候选]
            ├→ pathResourceDelta() → isCollected(), isTriggered(), tile()
            ├→ pathKeepsResourceNonNegative() → tile()
            ├→ informationProxy() → observedPositions(),
            │     unknownComponentSizesTouchingView() → mapWithHypothesis(), insideEstimated()
            ├→ futureGainMarginal() → knownCoins(), shortestPathOnKnownMap()
            ├→ computeQEff() → pathResourceDelta()
            └→ marginPenalty()
```

---

# 第十三章：ResourcePickupStrategy.cpp 文件全览

## 13.1 概述

`ResourcePickupStrategy.cpp` (485 行) 实现 PDF 第一问的资源拾取策略。与 Reward 贪心系统**完全独立**——使用自己的评分逻辑、自己的候选生成、自己的 3×3 局部视野规则。

入口函数是 `solveResourcePickupJson(source)` (`ResourcePickupStrategy.cpp:373`)，由 `AIPlayerEngine::RunResourcePickup()` 调用。

## 13.2 全部函数列表

### 重要函数（算法核心）

| 函数 | 行号 | 作用 |
|---|---|---|
| `solveResourcePickupJson(source)` | 373 | **入口**：解析 JSON → 运行贪心 → 构建输出 |
| `allShortestPaths(grid, targets)` | 98 | 预计算所有目标之间的最短路径 (Floyd/BFS) |
| `chooseGreedyTarget(...)` | 容器内 | 每轮选择比值最高的候选目标 |
| `parseResourceGrid(source, ...)` | 47 | 解析 3×3 网格 (P/G/T/.) |
| `buildFrames(grid, path, ...)` | 310 | 构建输出 JSON 的 frames[] 数组 |

### 工具函数

| 函数 | 行号 | 作用 |
|---|---|---|
| `isTriggered(mask, index)` | 155 | 位掩码检查：该陷阱是否已触发 |
| `pathDelta(path, grid, trapMask, collected)` | 171 | 计算路径资源变化（G→+1 值, T→-1 值） |
| `adjacentGoldCount(grid, trap)` | 200 | 统计陷阱周围的金币数（用于风险评估） |
| `cleanupScore(grid, path, trapMask, ...)` | 223 | 评估"清理"某个路径的价值 |
| `betterCandidate(best, cand)` | 251 | 比较两个候选：优先比值高，同比值选步数少 |
| `candidatesJson(cands, selected)` | 279 | 候选列表转 JSON 调试信息 |
| `attachFrameDebug(frames, greedyRounds)` | 359 | 将贪心轮次信息附加到 frame 的 debug 字段 |

## 13.3 调用关系

```
solveResourcePickupJson(source)  ← 入口
  → parseResourceGrid(source, start, targets, traps)
  → allShortestPaths(grid, targets+traps)  // 预计算全源最短路
  → [主循环] 每轮:
      → pathDelta(path, grid, mask, collected)  // 计算候选路径收益
      → cleanupScore(grid, path, mask, ...)     // 评估路径价值
      → adjacentGoldCount(grid, trap)           // 陷阱风险评估
      → betterCandidate(best, cand)             // 选最优候选
  → buildFrames(grid, path, ...)
      → attachFrameDebug(frames, greedyRounds)
```

## 13.4 与 Reward 系统的区别

| | ResourcePickupStrategy | Reward 系统 |
|---|---|---|
| 评分 | 投影比值 `gain/len`，比值低于当前 ratio 就停止 | Dinkelbach 加性 surrogate |
| 视野 | 3×3 全局可见（一次性给定整个 grid） | 3×3 逐步观察（localMap 累积） |
| 探索 | 无 I_proxy 概念，纯资源拾取 | 有完整的 I_proxy + α 动态调节 |
| 停止 | 比值无法提高时立即停止 | 多条件综合判断（τ/ρ_min/worthwhile） |
| 用途 | PDF 第一问（简单资源拾取） | 主迷宫探索算法 |

---

# 第十四章：RealtimeGreedyStrategy.cpp 文件全览

本章列出 `RealtimeGreedyStrategy.cpp`（~930 行）中**所有函数**，标注重要程度和相互调用关系。该文件是路径 A（Reward 贪心）的核心——`MemoryGreedyAgent` 类的完整实现。

## 14.1 MemoryGreedyAgent 成员变量 (`RealtimeGreedyStrategy.cpp:37-147`)

| 变量 | 行号 | 作用 |
|---|---|---|
| `maze_` | 37 前 | 真实迷宫引用（3×3 视野的真相来源） |
| `routeAlgorithm_` | 130 | 路由器选择："greedy"/"dijkstra"/"astar"/"branch_bound"/"divide_conquer" |
| `localMap_` | 131 前 | AI 局部记忆地图 |
| `poseEstimator_` | 132 前 | 掩码姿态估计器 |
| `evaluator_` | 133 前 | 评分器（含全部 reward 参数） |
| `state_` | 134 前 | `{resource, steps, collectedGold, alphaSmooth}` |
| `localToReal_` | 135 | 局部→真实坐标映射 |
| `realToLocal_` | 136 | 真实→局部坐标映射 |
| `knownBosses_` | 137 | 已发现 Boss 的局部坐标 |
| `defeatedBosses_` | 138 | 对应 Boss 是否已击败 |
| `bossBattleResult_` | 139 前 | Boss 战预计算结果 (JSON) |
| `bossBattleCanWin_` | 140 | 能否在 minRounds 内击败所有 Boss |
| `coinConsumption_` | 141 | 复活所需金币数（来自输入 JSON） |
| `pendingRevive_` | 142 | 是否待复活（下帧传送回起点） |
| `gameOver_` | 143 | Boss 战失败且钱不够 → 游戏结束 |
| `localExit_` | 144 | 出口的局部坐标（观察到后记录） |
| `currentTarget_` | 145 | 当前保持的目标（目标保持机制） |
| `currentTargetScore_` | 146 | 该目标的分数 |
| `currentTargetFromPocket_` | 147 | 当前目标是否来自 Pocket |

## 14.2 全部函数列表

### ★★★ 关键函数

| 函数 | 行号 | 作用 | 调用关系 |
|---|---|---|---|
| `run()` | 58 | **主循环**：6 步流水线 × 900 次 | → 所有其他方法 |
| `selectBestPath(...)` | 551 | **核心决策**：评分→Gate→Pocket→Hold→Fallback | → `evaluate()`, `routePath()`, `shouldGoExit()`, ClosedSingletonGate, Pocket |
| `updateKnownMap(r,l)` | 175 | 3×3 视野 → `localMap_` | → `realInBounds()`, `setObserved()`, `markBossTriggers()` |
| `applyCurrentCell(l)` | 229 | 资源结算 + Boss 触发 | → `markVisited()`, `markCollected()`, `triggerAdjacentBoss()` |

### ★★ 核心函数

| 函数 | 行号 | 作用 | 调用关系 |
|---|---|---|---|
| `triggerAdjacentBoss(l)` | 276 | Boss 战斗/复活/GameOver 三分支 | → `clearBoss()` |
| `shouldGoExit(...)` | 837 | 停止探索判定 (τ/R<1 保护/worthwhile) | |
| `fallbackPath(l)` | 869 | 无候选时的脱困路径 | → `routePath()`, `informationProxy()` |
| `candidateTargets(l)` | 336 | 生成候选目标集 | → `routePath()` |
| `bossGatedAreaMaxTargets(l)` | 433 | Boss-gated 区域判定 | → BFS on `localMap_` |
| `routePath(s,t)` | 525 | 路由器分发 (5 种算法) | → `shortestPathOnKnownMap()` / `astarPath()` / `dijkstraPath()` 等 |
| `buildContext(l)` | 400 | 构建评分上下文 | → `routePath()`, `bossGatedAreaMaxTargets()` |

### ★ 重要工具函数

| 函数 | 行号 | 作用 |
|---|---|---|
| `realInBounds(r,c)` | 159 | 真实坐标是否在 15×15 内 |
| `bossIndexAt(l)` | 259 | 查 Boss 在 `knownBosses_` 中的下标 |
| `fillStatusDebug(...)` | 314 | 复活/GameOver 帧的调试信息填充 |
| `recordRejectedTargets(l,debug)` | 366 | 记录被过滤掉的格子及原因 |
| `attachPocketRealPositions(pocket)` | 781 | Pocket 调试信息中局部坐标→真实坐标 |
| `markSelectedDebug(t,debug)` | 810 | 标记本步实际选中的目标 |

## 14.3 主循环调用关系图

```
run()  [每步]
  ├─ ① updateKnownMap(real, local)
  │     └─ realInBounds() → localMap_.setObserved() / setOutside()
  │        → bossIndexAt() → markBossTriggers()
  │
  ├─ ② applyCurrentCell(localCurrent)
  │     └─ markVisited() → markCollected() / markTriggered()
  │        → triggerAdjacentBoss() → clearBoss() / pendingRevive_ / gameOver_
  │
  ├─ ③ poseEstimator_.update(localMap_, localCurrent)
  │
  ├─ ④ updateAlphaSmooth(prevAlpha, localMap_, poseEstimator_)
  │
  ├─ ⑤ if pendingRevive_ → 传送回起点, continue
  │     if gameOver_ → break
  │
  ├─ ⑥ selectBestPath(localCurrent, realCurrent, step, debug)
  │     ├─ buildContext(localCurrent)
  │     │    └─ routePath() → exitPath
  │     │    └─ bossGatedAreaMaxTargets() → bossGatedTargets
  │     │
  │     ├─ computeQEff(context, localMap_) → debug.qEff
  │     ├─ recordRejectedTargets(localCurrent, debug)
  │     ├─ candidateTargets(localCurrent)
  │     │    └─ for each observed: isVisited? walkable? routePath empty? → targets[]
  │     │
  │     ├─ for each target in targets:
  │     │    ├─ routePath(current, target) → path
  │     │    ├─ evaluate(path, target, context, localMap_, poseEstimator_) → score
  │     │    ├─ pathResourceDelta(path), informationProxy(...), futureGainMarginal(...)
  │     │    ├─ computeQEff, marginPenalty
  │     │    └─ detourCost = len + len(target→exit) − len(exit)  → hasWorthwhile
  │     │
  │     ├─ ClosedSingletonGate → 可能改选 A 或 B
  │     ├─ [决策链]
  │     │    ├─ shouldGoExit? → return exitPath
  │     │    ├─ heldTarget still valid? → Pocket? → hold or switch
  │     │    ├─ bestPath non-empty? → return bestPath
  │     │    └─ return fallbackPath()
  │     │
  │     └─ attachPocketRealPositions, markSelectedDebug
  │
  └─ ⑦ localToReal_[nextLocal] → 更新 current, path.push
```

## 14.4 关键逻辑速查

| 逻辑 | 位置 | 速查 |
|---|---|---|
| 目标保持 | 行 700-735 | `heldScore` vs `bestScore + switchMargin(5.0)` |
| Pocket 插入点 | 行 684 | 在目标保持判断**之前** |
| ClosedSingleton 插入点 | 行 620-660 | 在候选评分循环**之后**、决策链**之前** |
| worthwhile 计算 | 行 600-605 | `score > qEff × detourCost`，满足即 `hasWorthwhile=true` |
| detourCost 计算 | 行 603-605 | `pathLen + (target→exit len) − (current→exit len)` |
| 绕路重算 (Gate 改选) | 行 643-655 | Gate 改选后只对非 A 候选重算 worthwhile |
| 低 ratio 保护 | 行 850-852 | `R/L<1 && collectedGold<3` → 禁止走出口 |
| 零资源保护 | 行 847 | `R==0 && hasNonNegativeTarget` → 禁止空手离场 |
| 掩码未知比例 | 行 534 | `estimatedObservedCount() / 225` → `debug.observedRatio` |
| 面积上限选择 | 行 543-544 | `exitPath 空? areaMax=12 : knownExitAreaCap=1.5` |

---

# 第十五章：PocketAwareGreedy.cpp 文件全览

## 15.1 概述

`PocketAwareGreedy.cpp` (297 行) 实现**局部金币簇优先策略**。当 AI 当前位置附近有 ≥2 个已知未拾取金币在半径 2 步以内时，触发 Pocket 模式——优先收割这些金币中 `Score_first` 最高的那个，把 I_proxy 最高的留到最后吃。

全部实现在 `ai_player` namespace 下。入口是 `choosePocketFirstTarget()` (`PocketAwareGreedy.cpp:139`)。

## 15.2 全部函数列表

### ★★★ 关键函数

| 函数 | 行号 | 作用 | 调用关系 |
|---|---|---|---|
| `choosePocketFirstTarget(...)` | 139 | **入口**：Pocket 决策主逻辑 | → `findPocket()`, `computeQEff()`, `shortestPathOnKnownMap()`, `pathResourceDelta()`, `informationProxy()` |

### ★★ 核心函数

| 函数 | 行号 | 作用 | 调用关系 |
|---|---|---|---|
| `findPocket(current, map, eval, radius, &hub)` | 95 | 识别最优 Pocket：遍历 hub 候选 → 统计半径内金币数 | → `hubCandidates()`, `pocketCoinsForHub()` |
| `pocketCoinsForHub(hub, map, eval, radius)` | 74 | 统计指定 hub 半径内所有未拾取金币 | → `knownCoins()`, `shortestPathOnKnownMap()` |
| `betterPocketCandidate(best, cand)` | 124 | 比较两个候选：`scoreFirst > eps → pathLen < → coord <` | |

### ★ 工具函数

| 函数 | 行号 | 作用 |
|---|---|---|
| `pathLength(path)` | 18 | 路径步数 = `path.size()-1` |
| `closerHub(cur, left, right, eval, map)` | 34 | 两个 hub 谁离当前位置更近 (BFS 距离) |
| `hubCandidates(current, map)` | 52 | 返回 {当前位置} ∪ {四方向可通行邻居} |

## 15.3 调用关系图

```
choosePocketFirstTarget(current, context, map, poseEst, evaluator)
  │
  ├─ findPocket(current, map, evaluator, pocketRadius=2, hub)
  │   ├─ hubCandidates(current, map)
  │   │   └─ isWalkableForPlanning() × 4 方向
  │   ├─ for each hub:
  │   │   └─ pocketCoinsForHub(hub, map, evaluator, 2)
  │   │       └─ knownCoins() → for each coin:
  │   │           └─ shortestPathOnKnownMap(hub→coin) → len ≤ 2? 加入
  │   └─ 选最优: 金币数最多 → 距离当前最近
  │
  ├─ pocketCoins.size() < 2? → return disabled
  │
  ├─ qEff = computeQEff(context, map)
  │
  └─ for coin in pocketCoins:
      ├─ path = shortestPathOnKnownMap(current→coin)
      ├─ baseScore = deltaR − 0.82 × qEff × len
      ├─ for otherCoin in pocketCoins (other≠coin):
      │   └─ remainI = max( Iproxy(other) / (1 + 0.2 × dist(coin, other)) )
      ├─ scoreFirst = baseScore + 0.2 × remainI
      └─ 选 scoreFirst 最高 → return PocketDecision
```

## 15.4 关键逻辑速查

| 逻辑 | 行号 | 说明 |
|---|---|---|
| Pocket 阈值 | 102 | `coins.size() < 2` → 不触发（单个金币无"留哪个"的决策空间） |
| hub 候选 | 52-59 | 当前位置 + 四方向可通行邻居 = 最多 5 个 |
| 金币半径 | 80 | `pathLength(path) ≤ pocketRadius(2)` |
| 选择 hub | 103-105 | 金币数优先 → BFS 距离优先 |
| baseScore | 175 | `deltaR − ηq × qEff × len`（不含 I_proxy） |
| remainI | 186 | `Iproxy(other) / (1 + 0.2 × dist)`（距离折扣） |
| scoreFirst | 194 | `baseScore + 0.2 × remainI` |
| 参 pocketRadius | RewardConfig.h:91 | 默认 2 |
| 参 pocketMu | RewardConfig.h:94 | 默认 0.2（remainI 距离折扣） |
| 参 pocketLambdaRemain | RewardConfig.h:97 | 默认 0.2（remainI 在 scoreFirst 中的权重） |

---

# 第十六章：ClosedSingletonLookaheadGate.cpp 文件全览

## 16.1 概述

`ClosedSingletonLookaheadGate.cpp` (495 行) 实现**闭单例前瞻门控**。当 top-1 候选的 `|C|=1`（走完它只能打开 1 个未知格），直接收益接近零——但走完它之后可能到达更好的后继目标。本模块做 memory-only 模拟：假设走了 A，不更新 3×3 视野，在模拟后的地图上重新评价所有候选，找出最优后继 `c_A`，然后用非对称比较 `reward(A) + γ×c_A > reward(B) + margin` 决定是否选 A。

入口是 `applyClosedSingletonLookaheadGate()` (`ClosedSingletonLookaheadGate.cpp:356`)。

## 16.2 全部函数列表

### ★★★ 关键函数

| 函数 | 行号 | 作用 |
|---|---|---|
| `applyClosedSingletonLookaheadGate(request)` | 356 | **入口**：Gate 完整决策流 |
| `computeMemoryOnlyContinuationAfterA(A, request)` | 213 | 模拟走 A 后，在 `localMapAfter` 上重新评分所有候选 → 取 max = `c_A` |
| `simulateExecuteAWithoutNewVision(A, request)` | 125 | 沿 A 的路径逐格走，结算资源，**不模拟 3×3 视野更新** |

### ★★ 核心函数

| 函数 | 行号 | 作用 |
|---|---|---|
| `isClosedSingletonCandidate(cand)` | 58 | 判定：`\|C\|=1` && score有效 && path非空 && tile≠"E" |
| `isMeaningfulNonClosedCandidate(cand, A)` | 82 | 候选 B 的过滤：排除 A、排除死节点、排除其他 closed singleton |
| `findTopCandidate(cands)` | 315 | 找当前最高分候选下标 |
| `findBestNonClosedCandidate(cands, A)` | 339 | 找最高分"有意义非封闭"候选 |

### ★ 工具函数

| 函数 | 行号 | 作用 |
|---|---|---|
| `validScore(score)` | 29 | score > -1e17 && isfinite(score) |
| `insideThreeByThree(center, pos)` | 44 | pos 是否在 center 的 3×3 范围内 |
| `validContinuationCandidate(score, delta, info, tail, path, pR)` | 195 | 后继候选过滤：非 -∞、可达、资源非负、非死节点 |

## 16.3 完整决策流程

```
applyClosedSingletonLookaheadGate(request)
  │
  ├─ [Guard] findTopCandidate(candidates) → A
  │   不存在有效候选? → return disabled
  │
  ├─ [Gate 触发] isClosedSingletonCandidate(A)?
  │   |C|≠1 或分数无效或 tile=="E"? → return (不干预)
  │
  ├─ [找 B] findBestNonClosedCandidate(candidates, A)
  │   └─ 遍历 candidates:
  │       排除 A, 排除 dead node (|C|=0 && dR≤0 && tail≤0 && ≠E)
  │       排除其他 closed singleton
  │   B 不存在? → A 直接通过 (return A)
  │
  ├─ [Phase 2] simulateExecuteAWithoutNewVision(A)
  │   └─ 深拷贝 localMap + state
  │       沿 A.path 逐格走:
  │         isWalkable? → 否→中止
  │         isBossTrigger? → 中止（不模拟 Boss 战）
  │         markVisited → 结算 G(+50)/T(-30)
  │         resource<0? → 中止
  │       ⚠️ 不调用 3×3 视野更新
  │   → (localMapAfter, stateAfter)
  │
  ├─ [Phase 3] computeMemoryOnlyContinuationAfterA(A)
  │   └─ 在 localMapAfter 上:
  │       计算 exitPath（如出口已知）
  │       for pos in observedPositions:
  │         排除 A 自身、出口、A 周围 3×3、已访问、不可通行
  │         evaluate(path, pos, contextAfter, localMapAfter, estimator)
  │         过滤: |C|=0 && dR≤0 && tail≤0 → skip
  │         score > bestReward? → c_A = score
  │   → c_A (A 之后的最优后继 reward)
  │
  ├─ [Phase 4] 非对称比较
  │   combinedA = reward(A) + 1.0 × c_A
  │   threshold = reward(B) + (-20)
  │
  │   combinedA > threshold?
  │     YES → 选 A (A 通过门控)
  │     NO  → 选 B (A 被拒绝，changed=true)
  │
  └─ return ClosedSingletonGateResult
```

## 16.4 关键逻辑速查

| 逻辑 | 行号 | 说明 |
|---|---|---|
| Closed Singleton 判定 | 58-61 | `\|C\|=1`, score有效, path非空, tile≠"E" |
| 死节点过滤 | 82-87 | `\|C\|=0 && dR≤0 && tail≤0 && tile≠"E"` → 排除 |
| Boss 触发格中止 | 111 | 不模拟 Boss 战 — 太复杂，保守跳过 |
| 3×3 视野不模拟 | — | `simulateExecuteAWithoutNewVision` 全程不调 `updateKnownMap` |
| A 周围 3×3 排除 | 185-186 | `insideThreeByThree(A.target, pos)` — 视野没模拟，不能用 |
| 非对称设计 | 325-326 | 只算 c_A，不算 c_B（B 是开放候选，模拟不可靠） |
| γ (gamma) | RewardConfig.h:85 | 默认 1.0 |
| margin | RewardConfig.h:88 | 默认 -20（负值=宽松：combinedA 可比 reward(B) 低最多 20 分仍通过） |
| combinedA 公式 | 322-323 | `reward(A) + γ × c_A` |
| Gate 插入点 | RealtimeGreedyStrategy.cpp:620 | 候选评分循环**之后**、shouldGoExit **之前** |
