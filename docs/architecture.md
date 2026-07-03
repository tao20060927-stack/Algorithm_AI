# AI Player Desktop 代码架构：完整调用链

## 一、两条顶层路径

```
输入 JSON 字符串
  │
  ├──→ AIPlayerEngine::RunRealtimeGreedy(json)     ← 路径一：Reward 贪心
  │    (或 RunAdventure(json, "astar"/"dijkstra"/"branch_bound"/"divide_conquer"))
  │
  └──→ AIPlayerEngine::RunAdventure(json, "smart")  ← 路径二：全局最近邻
```

---

## 二、路径一：Reward 贪心 — 完整调用链

### 第 0 层：引擎入口

```
AIPlayerEngine::RunRealtimeGreedy(jsonText)
  │
  ├─ makeRunCacheKey("greedy", jsonText)              // 算法名+规范化JSON → 缓存键
  │   └─ Json::parse(inputJson).dump()                  // nlohmann/json 解析+重新序列化
  │
  ├─ resultCache_.find(cacheKey)                       // 命中缓存直接返回
  │
  ├─ parseMaze(inputJson)                              // ==================== 见 2.1
  │   └─ 返回 MazeData { grid, start, exit, bosses, golds, source }
  │
  ├─ realtimeGreedyRun(data)                           // ==================== 见 2.2
  │   └─ 返回 GreedyRunResult { path, debugSteps, gameOver }
  │
  ├─ buildResult(data, path, "realtime-greedy")        // ==================== 见 2.3
  │   ├─ runBossBattleJson(data.source)                  // Boss 战结果（第二次调用）
  │   ├─ bossBattleCanWinWithinLimit(boss)               // 判定是否可击败
  │   ├─ 遍历 path 逐帧构建:
  │   │   ├─ scoreDelta(tile) → +50 / -30 / 0
  │   │   ├─ isBossTriggerCell(data, pos) → 曼哈顿距离==1?
  │   │   └─ 生成 frame { step, row, col, tile, delta, resource }
  │   └─ 返回 JSON { ok, path, frames, events, resource, steps, score_ratio }
  │
  ├─ attachGreedyDebug(result, run)                    // ==================== 见 2.4
  │   └─ 对每个 frame 附加 greedyStepDebugJson(step)
  │       └─ 每个候选: greedyCandidateDebugJson(c)
  │
  └─ resultCache_[key] = result.dump()                  // 写入缓存，返回 JSON 字符串
```

### 2.1 parseMaze(inputJson)

```
parseMaze(jsonText)
  │
  ├─ Json::parse(inputJson)                             // 解析 JSON
  │
  ├─ 校验 source["maze"] 是 2D 数组
  │
  ├─ 遍历 grid[r][c]:
  │   ├─ 存 tile 到 grid[r][c]
  │   ├─ tile=="S" → data.start = {r,c}
  │   ├─ tile=="E" → data.exit  = {r,c}
  │   ├─ tile=="B" → data.bosses.push_back({r,c})
  │   └─ tile=="G" → data.golds.push_back({r,c})
  │
  └─ 返回 MazeData
```

### 2.2 realtimeGreedyRun(data) — 创建 Agent 并执行

```
realtimeGreedyRun(data, routeAlgorithm, parameters)
  │
  └─ MemoryGreedyAgent agent(data, routeAlgorithm, parameters)
  │   │                                            // ====== 构造函数 ======
  │   ├─ poseEstimator_.initialize()                // MapPoseEstimator::initialize()
  │   │   └─ 创建 20 个入口假设（四边各5个偏移×4方向）
  │   │
  │   ├─ runBossBattleJson(maze_.source)             // BossStrategy.cpp — 第一次调用
  │   │   └─ 返回 { ok, sequence, turns, withinMinRounds, CoinConsumption, reviveRule }
  │   │
  │   ├─ bossBattleCanWin_ = result["ok"] && result["withinMinRounds"]
  │   └─ coinConsumption_ = result["CoinConsumption"]
  │
  └─ agent.run()                                     // ============ 主循环 ============
```

### 2.2.1 agent.run() — 主循环体

```
for step in 0..(15×15×4=900):

  ╔══════════════════════════════════════════════════╗
  ║ ① updateKnownMap(real, local)                    ║
  ╚══════════════════════════════════════════════════╝
  │
  ├─ for dr=-1..1, dc=-1..1:                         // 扫描 3×3 九宫格
  │   ├─ realPos = (real.r+dr, real.c+dc)
  │   ├─ realInBounds(realPos)?                       // 检查是否在迷宫范围内
  │   │   ├─ 是 → localMap_.setObserved(localPos, tile)
  │   │   │        cells_[localPos] = { observed=true, outside=false, tile=... }
  │   │   │
  │   │   │   tile=="B" && bossIndex < 0 ?
  │   │   │   ├─ knownBosses_.push_back(localPos)
  │   │   │   ├─ defeatedBosses_.push_back(false)
  │   │   │   └─ localMap_.markBossTriggers(localPos)
  │   │   │        └─ cells_[boss±(dr,dc)].bossTrigger = true   // 标记四邻触发区
  │   │   │
  │   │   │   tile=="E" → localExit_ = localPos
  │   │   │
  │   │   └─ 否 → localMap_.setOutside(localPos)
  │   │            cells_[localPos] = { observed=true, outside=true, tile="#" }
  │   │
  │   └─ localToReal_[localPos] = realPos            // 维护双射映射
  │       realToLocal_[realPos] = localPos
  │
  ╔══════════════════════════════════════════════════╗
  ║ ② applyCurrentCell(localCurrent)                 ║
  ╚══════════════════════════════════════════════════╝
  │
  ├─ localMap_.markVisited(localCurrent)              // cells_[pos].visited = true
  │
  ├─ state_.steps > 0 ?
  │   ├─ tile=="G" && !isCollected?
  │   │   ├─ state_.resource += 50
  │   │   ├─ state_.collectedGold++
  │   │   └─ localMap_.markCollected(pos)              // cells_[pos].collected = true
  │   │
  │   └─ tile=="T" && !isTriggered?
  │       ├─ state_.resource -= 30
  │       └─ localMap_.markTriggered(pos)              // cells_[pos].triggered = true
  │
  ├─ triggerAdjacentBoss(localCurrent)
  │   └─ for each knownBosses_[i]:
  │       if dist(current, boss)==1 && !defeatedBosses_[i]:
  │         ├─ bossBattleCanWin_?
  │         │   ├─ defeatedBosses_[i] = true
  │         │   └─ localMap_.clearBoss(boss)           // cells_[boss] = { tile=" ", observed=true }
  │         ├─ resource >= coinConsumption_?
  │         │   └─ pendingRevive_ = true
  │         └─ else: gameOver_ = true
  │
  └─ state_.steps++
  │
  ╔══════════════════════════════════════════════════╗
  ║ ③ poseEstimator_.update(localMap_, localCurrent) ║
  ╚══════════════════════════════════════════════════╝
  │
  ├─ 检测 seedKind_: 出生点 3×3 视野是否触达边界？
  │
  ├─ for each hypothesis in hypotheses_ (20 个):
  │   ├─ 将每个已观察格用 mapWithHypothesis() 映射到估计 15×15
  │   │   └─ 根据 hypothesis.inwardDirection 旋转+平移 localPos
  │   ├─ 映射后超出 15×15 范围? → hypothesis.feasible = false
  │   ├─ 计算 hypothesis.score:
  │   │   └─ -0.15 × edgeTouches − 0.05 × centerDistance
  │   └─ 选 score 最高的 feasible hypothesis 作为 best_
  │
  ├─ 候选掩码排除: 边缘出生+记忆跨度≥14 → 排除不可能的入口假设
  │
  └─ 掩码激活检测: 横向/纵向 span ≥ 15? → maskActive_ = true
  │
  ╔══════════════════════════════════════════════════╗
  ║ ④ updateAlphaSmooth(prevAlpha, localMap, est.)   ║
  ╚══════════════════════════════════════════════════╝
  │
  ├─ for pos in localMap_.observedPositions():
  │   ├─ observedCount++
  │   ├─ tile=="G" → goldCount++
  │   └─ tile=="T" → trapCount++
  │
  ├─ ρ̂_G = (goldCount + 1.0) / (observedCount + 10.0)
  ├─ ρ̂_T = (trapCount + 1.0) / (observedCount + 10.0)
  ├─ v_unk = 50×ρ̂_G − 30×ρ̂_T
  ├─ ρ_U = poseEstimator.estimatedUnknownCount() / 225
  │
  ├─ raw = 4.0 × (1+1.0×ρ_U+1.0×max(v_unk,0)/50) / (1+2.0×ρ̂_T)
  ├─ clipped = clamp(raw, 1.0, 8.0)
  └─ return 0.8×prevAlpha + 0.2×clipped              // EMA 平滑
  │
  ╔══════════════════════════════════════════════════╗
  ║ ⑤ selectBestPath(localCurrent, realCurrent, step)║
  ╚══════════════════════════════════════════════════╝
  │
  │  (见下方 2.2.2 完整展开)
  │
  ╔══════════════════════════════════════════════════╗
  ║ ⑥ 执行一步                                        ║
  ╚══════════════════════════════════════════════════╝
  │
  ├─ nextLocal = selectedPath[1]                     // 只走下一步，非整条路径
  ├─ localToReal_.find(nextLocal) → realCurrent
  └─ result.path.push_back(realCurrent)
```

### 2.2.2 selectBestPath() — 核心决策（完整展开）

```
selectBestPath(localCurrent, realCurrent, step, debug)
  │
  ├─ buildContext(localCurrent)                       // ====== 构建评分上下文 ======
  │   ├─ context.state = state_
  │   ├─ context.exitPath = localExit_≠kInvalid ? routePath(localCurrent, localExit_) : {}
  │   └─ context.bossGatedAreaMaxTargets = bossGatedAreaMaxTargets(localCurrent)
  │       └─ BFS from current: 无Boss可到区域 vs 穿Boss后可达区域 → 差集
  │
  ├─ debug.qEff = evaluator_.computeQEff(context, localMap_)
  │   ├─ q_ref = 出口可达? (R+exitΔR)/(L+exitLen) : R/(L+ε)
  │   └─ return max(q_ref, 1.0)
  │
  ├─ recordRejectedTargets(localCurrent, debug)        // 记录被过滤的格子和原因
  │   └─ for pos in observedPositions:
  │       ├─ pos==current → "current"
  │       ├─ isVisited(pos) → "visited"
  │       ├─ !isWalkableForPlanning(pos) → "blocked"
  │       └─ routePath(current,pos).empty() → "unreachable"
  │
  ├─ activeAreaCap = exitPath非空? 1.5 : 12            // 出口已知后面积奖励大幅缩减
  │
  ├─ candidateTargets(localCurrent)                    // ====== 生成候选集 ======
  │   └─ for pos in localMap_.observedPositions():
  │       ├─ pos==current → skip
  │       ├─ isVisited(pos) → skip
  │       ├─ !isWalkableForPlanning(pos) → skip
  │       └─ routePath(current, pos).empty() → skip
  │       └─ 通过 → 加入 targets
  │
  ├╌╌╌╌╌╌╌╌╌ 对所有候选独立评分 ╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌
  │ for each target in targets:
  │   │
  │   ├─ path = routePath(localCurrent, target)        // BFS/A*/Dijkstra/...
  │   │
  │   ├─ score = evaluator_.evaluate(path, target, context, localMap_, poseEstimator_)
  │   │   │                                         // ===== 核心评分 =====
  │   │   ├─ path.size()≤1 → return -∞               // 路径过短（仅当前位置）
  │   │   ├─ for pos in path:                         // 每格必须可通行
  │   │   │   └─ !localMap.isWalkableForPlanning(pos) → return -∞
  │   │   │
  │   │   ├─ delta = pathResourceDelta(path, localMap)
  │   │   │   └─ for i=1..path.size-1:
  │   │   │       ├─ tile=="G" && !isCollected(pos) → delta += 50
  │   │   │       └─ tile=="T" && !isTriggered(pos) → delta -= 30
  │   │   │
  │   │   ├─ projectedR = state_.resource + delta
  │   │   ├─ projectedR < 0 → return -∞               // 终点资源非负
  │   │   │
  │   │   ├─ !pathKeepsResourceNonNegative(path, R, localMap) → return -∞
  │   │   │   └─ for i=1..path.size-1:                 // 前缀资源非负
  │   │   │       ├─ 按序累计 resource
  │   │   │       └─ 任一步 resource<0 → false
  │   │   │
  │   │   ├─ info = informationProxy(target, localMap, poseEstimator, areaCap, isBossGated)
  │   │   │   │                                         // ===== I_proxy =====
  │   │   │   ├─ 统计 observedCount, goldCount, trapCount
  │   │   │   ├─ ρ̂_G = (goldCount+1)/(obsCount+10)
  │   │   │   ├─ ρ̂_T = (trapCount+1)/(obsCount+10)
  │   │   │   ├─ areaVal = max(50×ρ̂_G−30×ρ̂_T, 0)
  │   │   │   ├─ ρ_area = clamp(areaVal/50, 0.001, 1)
  │   │   │   │
  │   │   │   ├─ isBossGated && touchesEdge?
  │   │   │   │   └─ return κ_u × bossEdgeAreaBonus × ρ_area     // 15×2×ρ_area
  │   │   │   │
  │   │   │   └─ else:
  │   │   │       └─ for size in poseEstimator.unknownComponentSizesTouchingView(target, localMap, areaCap):
  │   │   │           └─ componentVal += min(size, activeAreaCap) × ρ_area
  │   │   │           └─ return κ_u × componentVal                // 60 × Σ...
  │   │   │
  │   │   ├─ tail = futureGainMarginal(target, path, localMap)
  │   │   │   └─ 收集 path 上会拾取的金币 → coinsOnPath
  │   │   │   └─ for gold in knownCoins() \ coinsOnPath:
  │   │   │       └─ BFS(target, gold) → 50/(dist+1) → 取 max
  │   │   │
  │   │   ├─ delta≤0 && info==0 → return -∞           // 零收益零信息否决
  │   │   │
  │   │   ├─ qEff = computeQEff(context, localMap)
  │   │   ├─ margin = marginPenalty(projectedR)
  │   │   │   └─ R'≥30? 0 : 8.0×((30−R')/30)²
  │   │   │
  │   │   └─ return delta
  │   │        + 0.175 × state_.alphaSmooth × info     // ω_I·α·I_proxy
  │   │        + 1.23 × tail                           // β·V_tail
  │   │        − 0.82 × qEff × path.len()              // η_q·q_eff·len
  │   │        − margin                                // φ_margin
  │   │
  │   ├─ 填充 GreedyCandidateDebug item:
  │   │   ├─ item.deltaR = evaluator_.pathResourceDelta(path, localMap)
  │   │   ├─ item.informationProxy = evaluator_.informationProxy(...)
  │   │   ├─ item.tailGain = evaluator_.futureGainMarginal(...)
  │   │   ├─ item.projectedResource = R + deltaR
  │   │   ├─ item.marginPenalty = evaluator_.marginPenalty(projectedR)
  │   │   └─ item.unknownComponents = poseEstimator_.unknownComponentSizesTouchingView(...)
  │   │
  │   └─ 更新 bestScore / bestTarget / bestPath
  │
  ├╌╌╌╌╌╌╌╌╌ Closed Singleton Gate ╌╌╌╌╌╌╌╌╌╌
  │
  ├─ applyClosedSingletonLookaheadGate(request)
  │   ├─ top-1 是 closed singleton (|C|=1)?
  │   │   ├─ findBestNonClosedCandidate → 找 B
  │   │   ├─ simulateExecuteAWithoutNewVision(A)
  │   │   │   └─ 沿 A.path 逐格: markVisited, 结算G/T, check resource≥0
  │   │   │       不更新 3×3 视野, 遇Boss触发格中止
  │   │   ├─ computeMemoryOnlyContinuationAfterA(A)
  │   │   │   └─ 在 localMap_after 上对已观察格重评分 → 取 max = c_A
  │   │   │       排除 A 周围 3×3（视野未模拟）, 排除死节点
  │   │   ├─ combinedA = reward(A) + 1.0 × c_A
  │   │   └─ combinedA > reward(B) + (−20)?
  │   │       ├─ 是 → 选 A
  │   │       └─ 否 → 选 B (changed=true)
  │   └─ 更新 bestTarget/bestPath/bestScore
  │
  ├╌╌╌╌╌╌╌╌╌ 决策优先级链 ╌╌╌╌╌╌╌╌╌╌
  │
  ├─ [1] shouldGoExit(context, bestScore, hasWorthwhile, hasNonNegative)?
  │   │   ├─ exitPath 空? → false
  │   │   ├─ R==0 && hasNonNegative? → false          // 禁止空手离场
  │   │   ├─ R/L<1 && collectedGold<3 && bestScore>−∞? → false // 早期保护
  │   │   ├─ bestScore ≤ 5.0? → true                   // tau=5.0
  │   │   └─ !hasWorthwhileTarget? → true
  │   └─ 是 → return exitPath
  │
  ├─ [2] heldTarget 仍可达?
  │   ├─ heldScore = evaluate(heldPath, heldTarget, ...)
  │   ├─ Pocket决策:
  │   │   └─ choosePocketFirstTarget(...)
  │   │       ├─ findPocket(): hub 半径2内≥2金币?
  │   │       ├─ 对每个金币: baseScore = dR−0.82×qEff×len
  │   │       │             remainI = max( Iproxy(其他)/(1+0.2×dist) )
  │   │       │             scoreFirst = baseScore + 0.2×remainI
  │   │       └─ 选 scoreFirst 最高的 → return PocketDecision
  │   ├─ pocket enabled? → return pocket.path          // Pocket 优先
  │   └─ bestScore > heldScore + 5.0?                  // switchMargin=5.0
  │       ├─ 否 → return heldPath                       // 保持旧目标
  │       └─ 是 → 切换 → return bestPath
  │
  ├─ [3] bestPath 非空? → return bestPath
  │
  └─ [4] return fallbackPath(localCurrent)
      │
      ├─ R/L<1 && collectedGold<3? → delayExit=true
      ├─ exit_已知 && !delayExit? → return routePath(current, exit_)
      └─ else: 遍历 observedPositions → 选 I_proxy 最高的
          └─ 相同 I_proxy? 选路径短的
  │
  └─ return selectedPath                               // 回到 run() 的步骤⑥
```

### 2.3 buildResult(data, path, mode)

```
buildResult(data, path, mode)
  │
  ├─ boss = runBossBattleJson(data.source)              // Boss 战第二次调用
  ├─ bossCanWin = bossBattleCanWinWithinLimit(boss)
  │   └─ boss["ok"] && (!hasMinRounds || withinMinRounds)
  ├─ reviveCost = boss["CoinConsumption"]
  │
  └─ for step in 0..path.size-1:
      ├─ tile = data.grid[row][col]
      ├─ first visit?
      │   ├─ delta = scoreDelta(tile) → +50(G) / -30(T) / 0
      │   ├─ resource += delta
      │   └─ collected[row][col] = true
      │
      ├─ isBossTriggerCell(data, pos)?                 // 曼哈顿距任何Boss==1?
      │   ├─ bossCanWin?
      │   │   └─ event = "boss"
      │   ├─ resource ≥ reviveCost?
      │   │   ├─ resource −= reviveCost
      │   │   └─ event = "boss_revive"
      │   └─ else:
      │       ├─ event = "boss_game_over"
      │       └─ gameOver = true
      │
      ├─ frame = { step, row, col, tile, delta, resource }
      ├─ result["path"].push_back({row,col})
      ├─ result["frames"].push_back(frame)
      └─ gameOver? → break

  └─ result["finished"] = !gameOver && path.back()==exit
```

### 2.4 attachGreedyDebug(result, run)

```
attachGreedyDebug(result, run)
  │
  └─ for i in 0..min(debugSteps.size, frames.size)-1:
      └─ frames[i]["debug"] = greedyStepDebugJson(debugSteps[i])
          ├─ step, localCurrent, realCurrent, alpha, qEff, decision
          ├─ candidates[]:
          │   └─ for c in step.candidates:
          │       └─ greedyCandidateDebugJson(c)
          │           └─ { localTarget, realTarget, tile, score, deltaR,
          │                informationProxy, tailGain, qEff, pathLength,
          │                marginPenalty, projectedResource, selected }
          ├─ rejected[]: greedyRejectedDebugJson(r)
          ├─ closedSingletonGate: { candidateA, rewardA, bestNonClosedB, rewardB,
          │                         combinedA, allowed, reason }
          └─ pocket: { enabled, pocketHub, pocketResources[], candidates[],
                        chosenPocketTarget, reason }
```

---

## 三、路径二：全局最近邻 — 完整调用链

```
AIPlayerEngine::RunAdventure(jsonText, "smart")
  │
  ├─ parseMaze(jsonText) → MazeData
  │
  └─ planAdventurePath(data, "smart")
      │
      ├─ remaining = Set(所有 golds)
      ├─ bossTriggered[i] = false × N
      │
      ├─ while remaining非空 或 有Boss未击败:
      │   │
      │   ├─ for target in remaining:
      │   │   └─ shortestPath(data, current, target, "smart")
      │   │       └─ astarPath(data, start, target)           // 全局 A*（见3.1）
      │   │           ├─ dist[r][c]=∞, parent[r][c]=kInvalid
      │   │           ├─ priority_queue<{f=g+h, pos}, min-heap>
      │   │           ├─ 循环: pop → lazy delete → 是target?break
      │   │           │   → 四方向: passable(data,nr,nc)?
      │   │           │   → relax: nextDist<dist[nr][nc]? 更新+push
      │   │           └─ 回溯 parent 链重建 path
      │   │   └─ candidate.size() < bestPath.size()? → 更新 best
      │   │
      │   ├─ for i in bossTriggered:
      │   │   └─ for {dr,dc} in 四方向:
      │   │       └─ shortestPath(data, current, boss±1, "smart") → 同上
      │   │
      │   ├─ best==kInvalid? → break
      │   ├─ fullPath += bestPath[1:]                     // 跳过重复起点
      │   ├─ current = best
      │   ├─ bestBossIndex≥0?
      │   │   └─ bossTriggered[idx]=true, grid[boss]=" "  // Boss变空地
      │   └─ else: remaining.erase(best)
      │
      └─ shortestPath(data, current, exit, "smart") → 追加到 fullPath
```

### 3.1 A* 最短路（全局版）

```
astarPath(data, start, target)                          // 单次 A* 调用
  │
  ├─ heuristic(pos) = |pos.r−target.r| + |pos.c−target.c|
  ├─ dist[start]=0, queue.push({h(start), start})
  │
  ├─ while queue非空:
  │   ├─ {prio, pos} = queue.top(); queue.pop()
  │   ├─ pos==target? → break
  │   └─ for {dr,dc} in 四方向:
  │       ├─ passable(data, nr, nc)?                     // 界内 & ≠"#" & ≠"B"
  │       ├─ nextDist = dist[pos]+1
  │       └─ nextDist < dist[nr][nc]?
  │           ├─ dist[nr][nc] = nextDist
  │           ├─ parent[nr][nc] = pos
  │           └─ queue.push({nextDist+h({nr,nc}), {nr,nc}})
  │
  ├─ dist[target]==∞? → throw "unreachable"
  └─ 回溯 parent 链 → reverse → return path
```

---

## 四、Boss 战系统 — 完整调用链

```
runBossBattleJson(source)
  │
  ├─ readMinRounds(source)                              // "minRounds"/"minRouds"/"min_turns"
  ├─ readCoinConsumption(source)                         // "CoinConsumption"
  ├─ 解析 PlayerSkills → Skill{id, damage, cooldown}[]
  │
  └─ for bossIndex in 0..N-1:                           // 顺序揭示
      │
      ├─ reserveSlack = (首Boss且还有后续)? 1 : 0
      ├─ phaseTurnLimit = minRounds − 已用回合 − 剩余Boss数
      │
      ├─ solveCurrentBossByLightCapacity(hp, cooldown, skills, reserveSlack, phaseTurnLimit)
      │   │
      │   ├─ 枚举回合数 t from lowerBound to phaseTurnLimit+reserveSlack:
      │   │   └─ enumerateKillExactTurns(hp, cooldown, skills, t, memo)
      │   │       │                                     // ===== BFS 层进枚举 =====
      │   │       ├─ 检查 memo 缓存
      │   │       ├─ states = [{hp, startCooldown, {}}]
      │   │       ├─ for turn in 0..exactTurns-1:
      │   │       │   ├─ 每层去重 (liveStateKey: hp+"|"+cooldown)
      │   │       │   └─ for state in states:
      │   │       │       └─ for action in availableBossActions(cooldown):
      │   │       │           ├─ applyBossAction(hp,cooldown,action) → (nextHp,nextCd)
      │   │       │           ├─ 非最后回合? nextHp≤0 → skip (不能提前死)
      │   │       │           ├─ 最后回合? nextHp>0 → skip (必须死)
      │   │       │           └─ 记录 BossPlanCandidate { sequence, cooldownAfter, ... }
      │   │       └─ 去重: 同 cooldownAfter → 保留字典序最小序列
      │   │
      │   ├─ 对每个 t 的方案评估 lightScore:
      │   │   ├─ evaluateSuffixLightCapacity(plan, skills, remainingUnknownBossCount, memo)
      │   │   │   ├─ 已知后缀: 逐个模拟 killExact → 累计回合
      │   │   │   └─ 未知后缀: suffixCapacityThreshold() 估算
      │   │   │       └─ 均匀分配截止时间片 → 每片avgDamage → min(各片容量)
      │   │   └─ lightScore = min(已知容量, 未知后缀阈值)
      │   │
      │   └─ chooseBestKnownSuffixCandidate() 选 lightScore 最优方案
      │
      ├─ 记录 phase { bossIndex, turns, sequence, cooldownAfter, ... }
      ├─ sequence += plan.sequence
      └─ cooldown = plan.cooldownAfter
      │
  └─ return { ok, sequence, turns, phases, withinMinRounds, reviveRule }
```

---

## 五、路由器调度一览

所有路由器有两个重载：全局版 `(MazeData)` 和局部版 `(LocalKnownMap)`。

```
routePath(start, target)  ─┬─ "greedy"/"smart"
                            │   └─ evaluator_.shortestPathOnKnownMap(start,target,localMap_)
                            │       └─ BFS: queue, visited set, 四方向扩展, parent回溯
                            │
                            ├─ "dijkstra"
                            │   └─ dijkstraPath(localMap_, start, target)
                            │       └─ priority_queue<{dist,pos}, min-heap>, lazy deletion
                            │
                            ├─ "astar"
                            │   └─ astarPath(localMap_, start, target)
                            │       └─ 同上 + 曼哈顿启发式 h = |r−tr| + |c−tc|
                            │
                            ├─ "branch_bound"
                            │   └─ branchBoundPath(localMap_, start, target)
                            │       └─ DFS + 下界剪枝: 曼哈顿距离作为剩余步数下界
                            │
                            └─ "divide_conquer"
                                └─ divideConquerPath(localMap_, start, target)
                                    └─ 双向 BFS: 两层交替扩展, 相遇点合并路径
```

---

## 六、数据流总结

```
输入 JSON
  │
  ├→ parseMaze() → MazeData { grid, start, exit, bosses, golds, source }
  │
  ├╌╌╌ 路径一（Reward贪心）╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌
  │
  │  MemoryGreedyAgent 构造函数
  │  │  └→ runBossBattleJson(source) → bossResult ─┐
  │  │                                               │
  │  agent.run() ─ 主循环 900步上限                   │
  │  │  ├→ updateKnownMap: 3×3→localMap              │
  │  │  ├→ applyCurrentCell: 结资源, Boss触发         │
  │  │  ├→ poseEstimator.update: 估计迷宫位置          │
  │  │  ├→ updateAlphaSmooth: α动态调节               │
  │  │  └→ selectBestPath:                           │
  │  │      ├→ evaluate(path,target)                 │
  │  │      │   ├→ pathResourceDelta                 │
  │  │      │   ├→ pathKeepsResourceNonNeg            │
  │  │      │   ├→ informationProxy                  │
  │  │      │   │   └→ unknownComponentSizesTouchingView │
  │  │      │   ├→ futureGainMarginal                │
  │  │      │   ├→ computeQEff                       │
  │  │      │   └→ marginPenalty                     │
  │  │      ├→ ClosedSingletonGate                   │
  │  │      ├→ PocketAwareGreedy                     │
  │  │      ├→ shouldGoExit                          │
  │  │      └→ fallbackPath                          │
  │  │
  │  buildResult(data, path)
  │  │  └→ runBossBattleJson(source) → bossResult ──┘(第二次)
  │  │  └→ 逐帧: scoreDelta, isBossTriggerCell
  │  │
  │  attachGreedyDebug(result, run)
  │  │  └→ greedyStepDebugJson × N_steps
  │  │
  │  └→ JSON 输出 { ok, path, frames[], events[], resource, steps, ratio }
  │
  ├╌╌╌ 路径二（全局最近邻）╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌
  │
  │  planAdventurePath(data, "smart")
  │  │  └→ while循环: 每轮对所有金币+Boss触发点跑 astarPath(global grid)
  │  │                → 选最短路径 → 走过去
  │  │
  │  buildResult(data, path) → 同上
  │  └→ JSON 输出
```

---

## 七、关键数值常量（RewardConfig.h）

| 参数 | 值 | 公式位置 |
|---|---|---|
| ω_I | 0.175 | ω_I·α·I_proxy |
| α_0 | 4.0 | α_raw 基准 |
| α范围 | [1.0, 8.0] | clip |
| β | 1.23 | β·V_tail |
| κ_u | 60 | I_proxy = κ_u·Σ... |
| η_q | 0.82 | η_q·q_eff·len |
| q_min | 1.0 | max(q_ref, 1.0) |
| m_safe | 30 | φ_margin 安全线 |
| λ_m | 8.0 | φ_margin 强度 |
| τ | 5.0 | 停止阈值 |
| switchMargin | 5.0 | 目标切换 |
| θ | 0.8 | α EMA 平滑 |
| λ | 10.0 | 密度平滑伪计数 |
