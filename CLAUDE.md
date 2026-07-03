# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

AI Player Desktop — WebView2 桌面端迷宫 AI 玩家 (C++20)。核心是一个基于 Dinkelbach 分式规划 surrogate 的 reward function，驱动 3×3 局部视野贪心探索算法。同时包含 Boss 战分支限界求解器、5 种可切换的路由算法（BFS/A\*/Dijkstra/分支限界/分治）、以及全局最近邻基线算法。

## Build & Run

```bash
cd build
cmake .. -G "Ninja" && cmake --build .
```

Targets:
| 目标 | 来源 | 功能 |
|---|---|---|
| `ai_player_smoke` | `src/SmokeTest.cpp` | 单 JSON 全算法覆盖测试 |
| `AIPlayerDesktop` | `src/DesktopApp.cpp` | WebView2 桌面 GUI (仅 MSVC) |
| `ai_player_eval` | `src/GreedyEval.cpp` | 批量评测 |
| `boss_strategy_tests` | `src/BossStrategyTest.cpp` | Boss 求解单元测试 |
| `resource_pickup_tests` | `src/ResourcePickupTest.cpp` | 资源拾取测试 |
| `pocket_aware_greedy_tests` | `src/PocketAwareGreedyTest.cpp` | Pocket 策略测试 |
| `closed_singleton_gate_tests` | `src/ClosedSingletonLookaheadGateTest.cpp` | 闭单例门控测试 |
| `mask_pose_tests` | `src/MaskPoseEstimatorTest.cpp` | 掩码估计测试 |
| `batch_runner` | `tools/batch_runner.cpp` | 批量跑分 CSV 输出 |
| `deep_debug` | `tools/deep_debug.cpp` | 单迷宫详细决策轨迹 JSON |

Requirements: C++20, CMake 3.20+, nlohmann/json (header-only). WebView2 GUI requires MSVC + WebView2 SDK.

## Two Independent Algorithm Paths

The project maintains **two completely separate** path-planning systems that share only input parsing and output building:

| | Path A: Reward Greedy | Path B: Global Nearest-Neighbor |
|---|---|---|
| Entry | `RunRealtimeGreedy(json)` or `RunAdventure(json, "astar")` | `RunAdventure(json, "smart")` |
| Info source | 3×3 fog-of-war → `LocalKnownMap` | Full `MazeData.grid` |
| Target selection | `evaluate()` reward formula | Nearest Manhattan distance |
| Path planner | `routePath()` on `localMap_` | `astarPath()` on `data.grid` |

Path A is the main system. Path B is a baseline for comparison.

## Architecture: Reward Greedy (Main Path)

### Top-Level Call Chain

```
AIPlayerEngine::RunRealtimeGreedy(jsonText)
  → parseMaze()                         // Parse 15×15 grid → MazeData
  → realtimeGreedyRun(data)             // Create MemoryGreedyAgent + run
    → MemoryGreedyAgent constructor     // Precompute boss battle, init pose estimator
    → agent.run()                       // Main game loop
  → buildResult(data, path)             // Build output JSON with frames/events
  → attachGreedyDebug(result, run)      // Attach per-step scoring breakdown
```

### Main Game Loop (`MemoryGreedyAgent::run()`)

Each step executes this 6-stage pipeline (up to 900 steps):

1. **`updateKnownMap()`** — Scan 3×3 around real position, write into `localMap_`
2. **`applyCurrentCell()`** — Settle gold (+50) / trap (−30), trigger boss if adjacent
3. **`poseEstimator_.update()`** — Update 15×15 mask estimate from local observations
4. **`updateAlphaSmooth()`** — Adjust dynamic exploration weight α based on observed density
5. **`selectBestPath()`** — Core decision: score all candidates, pick best target
6. Execute one step of the chosen path

### Core Scoring Formula (`evaluate()`)

```
Score = ΔR                          // Resource delta along path
      + 0.175 × α × I_proxy         // Information value (ω_I=0.175)
      + 1.23 × V_tail               // Marginal future coin value (β=1.23)
      − 0.82 × q_eff × len          // Path length cost (η_q=0.82)
      − φ_margin                     // Safety margin barrier

Hard constraints (violation → Score = −∞):
- Every step's cumulative resource ≥ 0 (prefix constraint)
- End resource ≥ 0
- All cells walkable (observed, not wall, not alive boss)
- Path must have resource gain OR info gain (no dead nodes)

I_proxy = 60 × Σ min(|C|, cap) × ρ_area
  where cap = 12 (exit unknown) or 1.5 (exit known)
  ρ_area = clip(max(50×ρ̂_G−30×ρ̂_T, 0) / 50, 0.001, 1)

q_eff = max(R/L estimate, 1.0)
φ_margin = 8.0 × (max(0, 30 − R_projected) / 30)²
```

### Decision Priority Chain (`selectBestPath()`)

1. **Exit** — if `shouldGoExit()` returns true (exit reachable + bestScore ≤ 5.0 + obs ratio sufficient)
2. **Hold target** — if current held target is still reachable and new best doesn't beat it by >5.0 (`switchMargin`)
3. **Pocket first** — if ≥2 coins within radius 2 of a hub, pick the one with highest `Score_first = baseScore + 0.2 × remainI`
4. **Closed Singleton Gate** — if top-1 candidate has `|C|=1`, simulate going there and compare `reward(A)+γ×c_A > reward(B)+margin`
5. **New target** — switch to best-scored candidate
6. **Fallback** — go to highest I_proxy observed cell or exit

### Key Subsystems

| System | File | Role |
|---|---|---|
| Reward formula | `Reward.cpp/.h` | `evaluate()`, `I_proxy`, `q_eff`, `φ_margin`, `α` |
| Local memory | `Reward.cpp` (`LocalKnownMap`) | 3×3 observation accumulation, fog-of-war |
| Mask estimator | `Reward.cpp` (`MapPoseEstimator`) | Infer 15×15 position from local obs only |
| Boss battle | `BossStrategy.cpp/.h` | Sequential reveal + rolling-horizon branch-and-bound |
| Pocket strategy | `PocketAwareGreedy.cpp/.h` | Local coin cluster first-target selection |
| Closed Singleton | `ClosedSingletonLookaheadGate.cpp/.h` | Non-symmetric lookahead for small-info nodes |
| Shortest path | `ShortestPathStrategy.cpp` | Algorithm dispatch + global nearest-neighbor |

### Routing Algorithms

All routers have two overloads: `(MazeData)` for global path B, `(LocalKnownMap)` for path A.

| Algorithm | File | Description |
|---|---|---|
| BFS | `Reward.cpp:shortestPathOnKnownMap` | Default for greedy/smart |
| A\* | `AStarStrategy.cpp` | Manhattan heuristic, priority queue |
| Dijkstra | `DijkstraStrategy.cpp` | A\* with h=0, priority queue |
| Branch & Bound | `BranchBoundStrategy.cpp` | DFS + Manhattan lower-bound prune |
| Divide & Conquer | `DivideConquerStrategy.cpp` | Bidirectional BFS meeting at midpoint |

### Tuning Parameters

All 22 parameters in `RewardConfig.h`:
| Key params | Value | Location |
|---|---|---|
| ω_I | 0.175 | ω_I·α·I_proxy |
| κ_u | 60 | I_proxy = κ_u·Σ... |
| η_q | 0.82 | η_q·q_eff·len |
| β | 1.23 | β·V_tail |
| τ | 5.0 | Stop threshold |
| switchMargin | 5.0 | Target persistence |
| θ | 0.8 | α EMA smoothing |

## Encoding Notes

Source files use UTF-8 without BOM, LF line endings (CRLF on Windows checkout). Chinese text in comments throughout. When editing, use the Edit tool which preserves existing encoding.

## Docs

- `docs/architecture.md` — Complete call chain trace, every function down to leaf level
- `docs/rules_and_constraints.md` — All rules, constraints, and special mechanisms
- `docs/README_reward_function.md` — Reward formula reference with parameter values
