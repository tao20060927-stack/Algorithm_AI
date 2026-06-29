# AI Player Desktop

WebView2 桌面端迷宫 AI 玩家，C++ 后端驱动。针对部分可观测迷宫探索问题，设计基于 Dinkelbach 分式规划 surrogate 的 reward function，实现 3×3 实时贪心策略和全局路径规划。

## 当前进度

### ✅ 已完成

| 模块 | 文件 | 说明 |
|---|---|---|
| **奖励函数设计** | `src/Reward.cpp/.h`, `src/RewardConfig.h` | 基于 `Score = ΔR + ω_I·α·I_proxy + β·V_tail − q_eff·len − φ_margin` 的加性 surrogate，全部 18 个可调参数集中在 `RewardConfig.h` |
| **实时贪心算法** | `src/RealtimeGreedyStrategy.cpp/.h` | 3×3 局部视野，每步重规划，含目标保持机制、动态 α 平滑、停止探索规则 |
| **迷宫探险** | `src/ShortestPathStrategy.cpp/.h` | 全局 A\*/Dijkstra 最短路径 + 最近邻贪心目标排序 |
| **Boss 战求解** | `src/AIPlayerEngine.cpp` | BFS 最小回合技能序列 |
| **密码破解** | `src/AIPlayerEngine.cpp` | SHA-256 加盐暴力枚举 |
| **地图嵌入估计** | `src/Reward.cpp` (`MapPoseEstimator`) | 不依赖真实坐标，仅从局部观察推断在 15×15 迷宫中的位置，用于未知连通块分析 |
| **桌面 GUI** | `src/DesktopApp.cpp` | WebView2 + HTML/JS 前端，支持单步/播放/速度调节/结果可视化 |

### 🚧 待实现（4 种额外算法）

1. **保守稳定版本（Score_A）** — 仅用 `ΔR + α·N_new − q_ref·len`，去掉 FutureGain 和未知连通块项，适合作为对照基线
2. **探索积极版本（Score_B）** — 在 Score_A 基础上启用 `I_proxy`（含 frontier + 连通块）和 `V_tail^marg`，提高未知区域探索效率
3. **风险敏感版本（Score_C）** — 在 Score_B 基础上加入安全裕量词典序排序和 barrier penalty，严格过滤不可行路径
4. **完整推荐版本（Score_D）** — 综合全部组件：动态 α + 结构化 I_proxy + 边际 tail value + q_exit 出口基线 + 停止探索规则 + hysteresis

## 奖励函数

```
Score(t) = ΔR_real(path_t)
         + ω_I · α_t · I_proxy(t)
         + β · V_tail^marg(t)
         − q_eff · len(path_t)
         − φ_margin(R + ΔR_real(path_t))
```

其中：

| 项 | 含义 |
|---|---|
| `ΔR_real` | 路径真实资源变化（+50 金币，−30 陷阱，一次性结算） |
| `I_proxy` | 轻量化信息价值代理：新可见未知格数 + 背后未知连通块加权 |
| `V_tail^marg` | 边际尾部价值：排除已收集金币后的最近金币机会上界 |
| `q_eff · len` | 把全局 ratio 目标 `R/L` 转成局部步数代价 |
| `φ_margin` | 资源余量平方 barrier，低于安全线时快速增大惩罚 |

动态探索权重 `α_t` 根据已观察区域的金币/陷阱/Boss 密度自动调节，EMA 平滑避免震荡。

## 构建

```bash
mkdir build && cd build
cmake .. -G "MinGW Makefiles" && make
```

或直接打开 `AIPlayerDesktop.sln`（Visual Studio 2022+）。

依赖：C++20、WebView2 Runtime（Windows 10/11 自带）、nlohmann/json（header-only）。

## API

| 方法 | 说明 |
|---|---|
| `RunRealtimeGreedy(json)` | 3×3 实时贪心 + 新 reward 系统 |
| `RunAdventure(json, algorithm)` | 全局路径规划（smart / dijkstra / astar） |
| `SolveLock(json)` | 密码破解 |
| `RunBoss(json)` | Boss 战最小回合求解 |
| `ValidateMaze(json)` | 迷宫可达性校验 |

## 冒烟测试

```bash
./ai_player_smoke ../test.json
```
