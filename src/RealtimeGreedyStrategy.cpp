#include "RealtimeGreedyStrategy.h"

#include "AStarStrategy.h"
#include "BossStrategy.h"
#include "BranchBoundStrategy.h"
#include "DijkstraStrategy.h"
#include "DivideConquerStrategy.h"
#include "PocketAwareGreedy.h"
#include "Reward.h"

#include <algorithm>
#include <cmath>
#include <climits>
#include <map>
#include <queue>
#include <set>
#include <stdexcept>

namespace ai_player {
namespace {

class MemoryGreedyAgent {
public:
    /**
     * 功能：创建实时贪心 AI 玩家。
     * 输入：
     *   - maze：桌面程序用于模拟 3x3 观察的真实迷宫数据。
     *   - routeAlgorithm：reward 选定目标后使用的两点寻路算法。
     *   - parameters：本次运行使用的 reward 参数，训练脚本会通过该参数做黑盒调参。
     * 输出：
     *   - 构造一个只用局部记忆做决策的 AI。
     * 关键逻辑：
     *   - AI 的决策坐标从起始位置局部原点 (0,0) 开始，真实坐标只用于模拟视野和输出路径。
     *   - routeAlgorithm 只影响路由，不参与候选目标的 reward 公式。
     *   - parameters 只覆盖本次 evaluator，不修改全局默认配置。
     */
    explicit MemoryGreedyAgent(const MazeData &maze, const std::string &routeAlgorithm, RewardParameters parameters)
        : maze_(maze), routeAlgorithm_(routeAlgorithm), evaluator_(parameters)
    {
        poseEstimator_.initialize();
        bossBattleResult_ = runBossBattleJson(maze_.source);
        bossBattleCanWin_ = bossBattleResult_.value("ok", false);
        if (bossBattleResult_.contains("withinMinRounds")) {
            bossBattleCanWin_ = bossBattleCanWin_ && bossBattleResult_["withinMinRounds"].get<bool>();
        }
        coinConsumption_ = bossBattleResult_.value("CoinConsumption", 0);
    }

    /**
     * 功能：执行实时探索并返回实际迷宫路径。
     * 输入：
     *   - 无。
     * 输出：
     *   - 返回从起始位置开始的真实坐标路径，供前端播放和后端结果结算。
     * 关键逻辑：
     *   - 每一步先观察 3x3、更新局部记忆和 reward 状态，再只走候选路径的下一步。
     */
    GreedyRunResult run()
    {
        // 初始化：真实坐标从迷宫起点开始，局部坐标始终以 (0,0) 为原点。
        // 这种设计保证了 AI 不知道自己在迷宫中的绝对位置，只依赖 3x3 视野做决策。
        Position realCurrent = maze_.start;
        Position localCurrent{0, 0};
        // 建立局部坐标和真实坐标的双向映射，供前端渲染和路径输出使用。
        localToReal_[localCurrent] = realCurrent;
        realToLocal_[realCurrent] = localCurrent;

        GreedyRunResult result;
        // 路径以真实坐标起点开始。
        result.path.push_back(realCurrent);
        // 步数上限设为网格单元数的 4 倍，防止死循环耗尽内存。
        // 正常探索极少达到此上限，仅作为安全网。
        const int maxSteps = static_cast<int>(maze_.grid.size() * maze_.grid[0].size() * 4);

        // 主循环：每步执行 观察→结算→评分→行走 的决策流水线。
        for (int step = 0; step < maxSteps && realCurrent != maze_.exit; ++step) {
            // 第一步：以真实坐标为中心扫描 3x3 视野，写入局部记忆。
            updateKnownMap(realCurrent, localCurrent);
            // 第二步：结算当前格资源（金币、陷阱），标记访问状态。
            applyCurrentCell(localCurrent);
            // 第三步：更新地图姿势估计器，估算已探索比例和未知区域分布。
            poseEstimator_.update(localMap_, localCurrent);
            // 第四步：平滑更新 alpha 参数，影响 reward 公式中信息项和资源项的权重平衡。
            state_.alphaSmooth = evaluator_.updateAlphaSmooth(state_.alphaSmooth, localMap_, poseEstimator_);
            // 处理 Boss 战导致的特殊状态：复活或游戏结束。
            if (pendingRevive_ || gameOver_) {
                GreedyStepDebug stepDebug;
                fillStatusDebug(localCurrent, realCurrent, static_cast<int>(result.path.size()) - 1,
                                pendingRevive_ ? "boss-revive-reset" : "boss-game-over", stepDebug);
                result.debugSteps.push_back(std::move(stepDebug));
                // Boss 战失败但资源够复活：重置到起点，清空当前目标，继续探索。
                if (pendingRevive_) {
                    pendingRevive_ = false;
                    localCurrent = {0, 0};
                    realCurrent = maze_.start;
                    currentTarget_ = kInvalid;
                    currentTargetScore_ = -1e18;
                    currentTargetFromPocket_ = false;
                    result.path.push_back(realCurrent);
                    continue;  // 跳过本帧后续逻辑，从起点重新开始
                }
                // Boss 战失败且资源不足：标记游戏结束并退出循环。
                result.gameOver = true;
                break;
            }

            // 第五步：通过 reward 公式选出最佳目标及其路径。
            GreedyStepDebug stepDebug;
            const auto selectedPath = selectBestPath(localCurrent, realCurrent, static_cast<int>(result.path.size()) - 1,
                                                     stepDebug);
            result.debugSteps.push_back(std::move(stepDebug));
            // 若选出的路径长度为 1（仅包含当前位置），说明无路可走，终止。
            if (selectedPath.size() <= 1) break;

            // 第六步：只走路径的下一步（index 1），实现"走一步、重规划一步"的实时策略。
            const Position nextLocal = selectedPath[1];
            const auto it = localToReal_.find(nextLocal);
            // 若下一格在映射中找不到（通常不会发生），安全终止。
            if (it == localToReal_.end()) break;
            // 更新局部坐标和真实坐标，并将下一步加入输出路径。
            localCurrent = nextLocal;
            realCurrent = it->second;
            result.path.push_back(realCurrent);
        }
        return result;
    }

private:
    const MazeData &maze_;
    std::string routeAlgorithm_;
    LocalKnownMap localMap_;
    MapPoseEstimator poseEstimator_;
    PathValueEvaluator evaluator_;
    AgentState state_;
    std::map<Position, Position> localToReal_;
    std::map<Position, Position> realToLocal_;
    std::vector<Position> knownBosses_;
    std::vector<bool> defeatedBosses_;
    Json bossBattleResult_;
    bool bossBattleCanWin_ = true;
    int coinConsumption_ = 0;
    bool pendingRevive_ = false;
    bool gameOver_ = false;
    Position localExit_ = kInvalid;
    Position currentTarget_ = kInvalid;
    double currentTargetScore_ = -1e18;
    bool currentTargetFromPocket_ = false;

    /**
     * 功能：判断真实坐标是否在迷宫范围内。
     * 输入：
     *   - row：真实行号。
     *   - col：真实列号。
     * 输出：
     *   - 返回坐标是否有效。
     * 关键逻辑：
     *   - 该函数只用于模拟 3x3 视野边界，不参与目标评分。
     */
    bool realInBounds(int row, int col) const
    {
        return row >= 0 && col >= 0 && row < static_cast<int>(maze_.grid.size()) &&
               col < static_cast<int>(maze_.grid[0].size());
    }

    /**
     * 功能：根据真实 3x3 视野更新局部记忆地图。
     * 输入：
     *   - realCurrent：当前真实坐标，仅用于读取本步视野。
     *   - localCurrent：当前局部坐标，用于把视野写入局部记忆。
     * 输出：
     *   - 无返回值，更新 local_known_map、Boss 信息和出口局部位置。
     * 关键逻辑：
     *   - 观察结果按相对位移写入局部坐标；reward 后续只读取局部记忆。
     */
    void updateKnownMap(Position realCurrent, Position localCurrent)
    {
        // 以当前位置为中心的 3x3 视野扫描：dr 和 dc 各取 -1, 0, 1，
        // 共扫描 9 个格子，模拟 AI 每步能看到的局部范围。
        for (int dr = -1; dr <= 1; ++dr) {
            for (int dc = -1; dc <= 1; ++dc) {
                // 根据当前真实坐标和相对偏移计算视野格的真实坐标，
                // 同时根据局部坐标和相同偏移计算对应的局部记忆坐标。
                const Position realPos{realCurrent.first + dr, realCurrent.second + dc};
                const Position localPos{localCurrent.first + dr, localCurrent.second + dc};
                // 真实坐标越界时标记为地图外部，后续 reward 和路由均不会把该格作为候选。
                if (!realInBounds(realPos.first, realPos.second)) {
                    localMap_.setOutside(localPos);
                    continue;
                }

                // 从真实迷宫读取本格原始 tile，用于写入局部记忆。
                std::string tile = maze_.grid[realPos.first][realPos.second];
                const int bossIndex = bossIndexAt(localPos);
                // 若该格是 Boss 且已被击败，则视为空地，
                // 这样后续路由和 reward 才不会把已击败 Boss 当作障碍物。
                if (tile == "B" && bossIndex >= 0 && defeatedBosses_[bossIndex]) {
                    tile = " ";
                }

                // 将本格 tile 写入局部记忆地图，后续所有决策（reward、路由、候选生成）
                // 都只读取 localMap_，不会再看真实迷宫。
                localMap_.setObserved(localPos, tile);
                // 维护局部坐标与真实坐标的双向映射，供前端展示和路径输出使用。
                localToReal_[localPos] = realPos;
                realToLocal_[realPos] = localPos;

                // 首次发现 Boss 时注册到已知列表，并标记其邻接触发区，
                // 方便后续 triggerAdjacentBoss 检测进入触发范围。
                if (tile == "B" && bossIndex < 0) {
                    knownBosses_.push_back(localPos);
                    defeatedBosses_.push_back(false);
                    localMap_.markBossTriggers(localPos);
                }
                // 记录出口局部坐标，供 shouldGoExit 和 buildContext 判断何时停止探索。
                if (tile == "E") localExit_ = localPos;
            }
        }
    }

    /**
     * 功能：结算 AI 实际站上当前局部格后的状态。
     * 输入：
     *   - localCurrent：AI 当前局部坐标。
     * 输出：
     *   - 无返回值，更新资源、步数、访问状态和 Boss 战状态。
     * 关键逻辑：
     *   - 金币和陷阱只结算一次；走到 Boss 正邻接格会强制触发 Boss 战并清除 Boss 本体阻挡。
     */
    void applyCurrentCell(Position localCurrent)
    {
        // 将当前格标记为已访问，后续候选生成会排除已访问格，避免重复探索浪费步数。
        localMap_.markVisited(localCurrent);
        // 步数为 0 时 AI 仍站在起点，此时不结算资源，避免把起点格误判为收集。
        // 金币收集：只结算一次，通过 isCollected 标志防止重复拾取。
        if (state_.steps > 0 && localMap_.tile(localCurrent) == "G" && !localMap_.isCollected(localCurrent)) {
            state_.resource += kGoldValue;
            ++state_.collectedGold;          // 记录拾取金币数量，供 shouldGoExit 做停止探索判断
            localMap_.markCollected(localCurrent);
        // 陷阱触发：每次触发扣除资源（kTrapValue 为负值），同样只触发一次。
        } else if (state_.steps > 0 && localMap_.tile(localCurrent) == "T" && !localMap_.isTriggered(localCurrent)) {
            state_.resource += kTrapValue;
            localMap_.markTriggered(localCurrent);
        }
        // 检查是否进入 Boss 邻接触发区，触发 Boss 战斗逻辑。
        triggerAdjacentBoss(localCurrent);
        // 每走一步累加步数，用于 R/L 比率计算和步数上限保护。
        ++state_.steps;
    }

    /**
     * 功能：查找局部 Boss 在已知列表中的下标。
     * 输入：
     *   - localBoss：Boss 局部坐标。
     * 输出：
     *   - 返回下标，未找到时返回 -1。
     * 关键逻辑：
     *   - 用于避免重复记录同一个 Boss，并支持触发后清除 Boss 本体。
     */
    int bossIndexAt(Position localBoss) const
    {
        for (int i = 0; i < static_cast<int>(knownBosses_.size()); ++i) {
            if (knownBosses_[i] == localBoss) return i;
        }
        return -1;
    }

    /**
     * 功能：在 AI 进入 Boss 正邻接格时触发 Boss 战。
     * 输入：
     *   - localCurrent：AI 当前局部坐标。
     * 输出：
     *   - 无返回值，更新 Boss 已击败状态。
     * 关键逻辑：
     *   - 只要曼哈顿距离为 1 就视为强制触发；触发后 Boss 本体从局部地图中清除为通路。
     */
    void triggerAdjacentBoss(Position localCurrent)
    {
        // 遍历所有已知 Boss，检查当前格是否与 Boss 曼哈顿距离为 1（正邻接）。
        for (int i = 0; i < static_cast<int>(knownBosses_.size()); ++i) {
            const int distance = std::abs(localCurrent.first - knownBosses_[i].first) +
                                 std::abs(localCurrent.second - knownBosses_[i].second);
            // 只有距离恰好为 1 且该 Boss 尚未被击败时才触发战斗，
            // 防止同一 Boss 被重复触发或隔墙误触发。
            if (distance == 1 && !defeatedBosses_[i]) {
                // 若 boss 战预计算可获胜，直接标记击败并清除 Boss 本体阻挡。
                if (bossBattleCanWin_) {
                    defeatedBosses_[i] = true;
                    localMap_.clearBoss(knownBosses_[i]);
                // 若不可获胜但当前资源足够支付复活金币，扣除金币并设置待复活标志，
                // 下一帧 run() 会检测 pendingRevive_ 并传送回起点。
                } else if (state_.resource >= coinConsumption_ * 50) {
                    state_.resource -= coinConsumption_ * 50;
                    pendingRevive_ = true;
                // 资源不足且 Boss 战无法获胜则游戏结束。
                } else {
                    gameOver_ = true;
                }
            }
        }
    }

    /**
     * 功能：构造不进行目标选择的状态调试帧。
     * 输入：
     *   - localCurrent/realCurrent：当前局部坐标和真实坐标。
     *   - step：当前路径帧编号。
     *   - decision：状态原因，例如复活或 Game Over。
     *   - debug：输出调试帧。
     * 输出：
     *   - 无返回值，通过 debug 返回当前状态。
     * 关键逻辑：
     *   - Boss 失败复活会直接把玩家送回起点，这一帧没有普通 reward 候选，需要补一个调试帧保持前端帧序对齐。
     */
    void fillStatusDebug(Position localCurrent, Position realCurrent, int step, const std::string &decision,
                         GreedyStepDebug &debug) const
    {
        const PathValueContext context = buildContext(localCurrent);
        debug.step = step;
        debug.localCurrent = localCurrent;
        debug.realCurrent = realCurrent;
        debug.alpha = state_.alphaSmooth;
        debug.observedRatio = static_cast<double>(poseEstimator_.estimatedObservedCount()) / 225.0;
        debug.qEff = evaluator_.computeQEff(context, localMap_);
        debug.decision = decision;
    }

    /**
     * 功能：生成当前可选探索目标。
     * 输入：
     *   - localCurrent：当前局部坐标。
     * 输出：
     *   - 返回已观察、可通行、未访问且可达的局部目标。
     * 关键逻辑：
     *   - 已访问格只允许作为路径中转，不作为候选目标；Boss 触发区可以作为目标，进入即触发战斗。
     */
    std::vector<Position> candidateTargets(Position localCurrent) const
    {
        std::vector<Position> targets;
        // 遍历局部记忆中所有已观察到的格子作为候选。
        for (const auto &pos : localMap_.observedPositions()) {
            // 排除当前格自身：AI 已站在上面，不需要再走过去。
            // 排除出口：出口由 shouldGoExit 单独决策，不参与 reward 评分竞争。
            // 排除已访问格：已访问格资源已结算，重复访问无收益。
            // 排除不可通行格：墙和未探索区域不参与候选生成。
            if (pos == localCurrent || pos == localExit_ || localMap_.isVisited(pos) ||
                !localMap_.isWalkableForPlanning(pos)) {
                continue;
            }
            // 即使满足上述条件，若从当前位置不可达（被墙隔开），也不作为候选。
            if (routePath(localCurrent, pos).empty()) continue;
            targets.push_back(pos);
        }
        return targets;
    }

    /**
     * 功能：记录已观察但没有进入 reward 候选集的格子及过滤原因。
     * 输入：
     *   - localCurrent：当前局部坐标，用于判断格子是否能从当前位置路由到达。
     *   - debug：当前步调试信息，函数会向 rejected 列表追加过滤记录。
     * 输出：
     *   - 无返回值，通过 debug.rejected 保存被过滤格子的坐标、类型和原因。
     * 关键逻辑：
     *   - 过滤条件与 candidateTargets 完全对应，仅用于定位候选缺失问题，不改变 reward 和路径决策。
     */
    void recordRejectedTargets(Position localCurrent, GreedyStepDebug &debug) const
    {
        for (const auto &pos : localMap_.observedPositions()) {
            std::string reason;
            int pathLength = 0;
            if (pos == localCurrent) {
                reason = "current";
            } else if (localMap_.isVisited(pos)) {
                reason = "visited";
            } else if (!localMap_.isWalkableForPlanning(pos)) {
                reason = "not-walkable";
            } else {
                const auto path = routePath(localCurrent, pos);
                if (!path.empty()) continue;
                reason = "unreachable";
                pathLength = 0;
            }

            GreedyRejectedDebug item;
            item.localTarget = pos;
            const auto realIt = localToReal_.find(pos);
            item.realTarget = realIt == localToReal_.end() ? kInvalid : realIt->second;
            item.tile = localMap_.tile(pos);
            item.reason = reason;
            item.pathLength = pathLength;
            debug.rejected.push_back(item);
        }
    }

    /**
     * 功能：构造当前评分上下文。
     * 输入：
     *   - localCurrent：当前局部坐标。
     * 输出：
     *   - 返回包含资源、步数、alpha 和出口路径的上下文。
     * 关键逻辑：
     *   - 只有出口已观察且在局部 known_map 上可达时，才把出口路径交给 q_eff 和停止探索规则。
     */
    PathValueContext buildContext(Position localCurrent) const
    {
        PathValueContext context;
        context.state = state_;
        if (localExit_ != kInvalid) {
            context.exitPath = routePath(localCurrent, localExit_);
        }
        context.bossGatedAreaMaxTargets = bossGatedAreaMaxTargets(localCurrent);
        return context;
    }

    /**
     * 功能：计算必须踏过 Boss 本体后才能到达的已知可走区域。
     * 输入：
     *   - localCurrent：AI 当前局部坐标。
     * 输出：
     *   - 返回一组局部坐标；这些格子在删掉 Boss 后不可达，但经过 Boss 本体可以到达。
     * 关键逻辑：
     *   - 先在局部记忆图中把所有已知 Boss 本体当作墙，求当前位置的可达集合。
     *   - 对每个可从当前侧邻接到的 Boss，从 Boss 本体开始 BFS，凡是不在“删 Boss 可达集合”中的可走格都视为 Boss-gated 区域。
     *   - 该集合只使用 localMap_ 和已观察 Boss，不读取真实出口位置；用于把这些目标的 |C| 按 Amax 处理。
     */
    // 目标：找出所有"必须穿过至少一个 Boss 才能到达"的已知可走格。
    // 这些格子在 I_proxy 中享有面积加成（bossEdgeAreaBonus），因为到达它们意味着推进通关。
    //
    // 算法分三步：
    //   (A) BFS 从当前位置出发，视所有 Boss 为墙 → reachableWithoutBoss（不穿 Boss 可达区）
    //   (B) 找出哪些 Boss 与 reachableWithoutBoss 接壤（能从当前侧触发）
    //   (C) 对每个接壤 Boss，BFS 穿过它 → 差集 = 必须穿 Boss 才能到的区域
    std::set<Position> bossGatedAreaMaxTargets(Position localCurrent) const
    {
        // 将已知 Boss 本体坐标放入集合，供 O(1) 查询。
        std::set<Position> bossSet(knownBosses_.begin(), knownBosses_.end());
        // 可走条件：不是 Boss 本体，且满足 localMap 的通用通行规则（已观察、非墙）。
        const auto walkableWithoutBoss = [&](Position pos) {
            return !bossSet.count(pos) && localMap_.isWalkableForPlanning(pos);
        };

        // ===== 步骤 A：BFS 找"不穿 Boss 就能到达"的全部格子 =====
        // 视所有 Boss 本体为墙，从当前位置做洪水填充。
        std::set<Position> reachableWithoutBoss;
        std::queue<Position> queue;
        if (walkableWithoutBoss(localCurrent)) {
            reachableWithoutBoss.insert(localCurrent);
            queue.push(localCurrent);
        }
        while (!queue.empty()) {
            const Position current = queue.front();
            queue.pop();
            for (const auto [dr, dc] : kDirs) {
                const Position next{current.first + dr, current.second + dc};
                // 已访问或不可走（含 Boss 本体）→ 跳过。
                if (reachableWithoutBoss.count(next) || !walkableWithoutBoss(next)) continue;
                reachableWithoutBoss.insert(next);
                queue.push(next);
            }
        }
        // 此时 reachableWithoutBoss = 当前位置能到的所有"和平区域"。

        // ===== 步骤 B + C：对每个 Boss，找"穿过去后才能到达"的区域 =====
        std::set<Position> gated;
        for (const auto &boss : knownBosses_) {
            // Boss 本体自身的通行规则检查：已被击败的 Boss 已在 localMap 中标记为可通行，
            // 跳过它们（不需要穿过已击败的 Boss）。
            if (!localMap_.isWalkableForPlanning(boss)) continue;

            // 判断 Boss 是否与"不穿 Boss 可达区"接壤。
            // 只要 Boss 四个邻居格中至少有一个在 reachableWithoutBoss 中，
            // 就说明 AI 能从当前位置走到 Boss 旁边并触发战斗。
            bool bossReachableFromCurrentSide = false;
            for (const auto [dr, dc] : kDirs) {
                if (reachableWithoutBoss.count({boss.first + dr, boss.second + dc})) {
                    bossReachableFromCurrentSide = true;
                    break;
                }
            }
            // Boss 不接壤 → 当前无法触发 → 跳过（可能在迷宫另一端，还走不到）。
            if (!bossReachableFromCurrentSide) continue;

            // 穿过该 Boss 做 BFS：从 Boss 本体出发，探索"击败 Boss 后能到达"的区域。
            std::set<Position> throughBossVisited;
            std::queue<Position> throughBossQueue;
            throughBossVisited.insert(boss);
            throughBossQueue.push(boss);
            while (!throughBossQueue.empty()) {
                const Position current = throughBossQueue.front();
                throughBossQueue.pop();
                // 当前格不在"不穿 Boss 可达区"中 → 说明必须穿过 Boss 才能到这里 → 记入 gated。
                if (!reachableWithoutBoss.count(current)) gated.insert(current);
                for (const auto [dr, dc] : kDirs) {
                    const Position next{current.first + dr, current.second + dc};
                    // 三种情况跳过：
                    // (a) 已访问过 → 避免循环
                    // (b) 是另一个 Boss 本体 → 不跨 Boss（只穿过当前这一个）
                    // (c) 不可通行（墙/未观察）
                    if (throughBossVisited.count(next) || (bossSet.count(next) && next != boss) ||
                        !localMap_.isWalkableForPlanning(next)) {
                        continue;
                    }
                    throughBossVisited.insert(next);
                    throughBossQueue.push(next);
                }
            }
        }
        // gated = 所有必须至少穿过一个 Boss 才能到达的已知可走格集合。
        // 这些候选在 evaluate() 中会触发 I_proxy 的 forceAreaMax=true，
        // 按 bossEdgeAreaBonus(=15) 而不是普通 A_max(=12) 计算面积贡献。
        return gated;
    }

    /**
     * 功能：按当前配置的路由算法在局部已知地图上求两点路径。
     * 输入：
     *   - start：局部路径起点。
     *   - target：reward 层已经选出的局部目标。
     * 输出：
     *   - 返回 start 到 target 的局部路径；不可达时返回空路径。
     * 关键逻辑：
     *   - reward 负责“去哪一个格子”，本函数只负责“怎么走到该格子”。
     *   - 所有路由算法都只读取 localMap_，因此不会偷看完整迷宫。
     */
    std::vector<Position> routePath(Position start, Position target) const
    {
        // 根据配置的路由算法选择不同的寻路实现。所有算法都只读取局部记忆地图 localMap_，
        // 因此不会偷看完整迷宫，保证公平性。
        // "greedy" 和 "smart" 的 reward 版本路由复用同一个最短路径实现。
        if (routeAlgorithm_ == "greedy" || routeAlgorithm_ == "smart") {
            return evaluator_.shortestPathOnKnownMap(start, target, localMap_);
        }
        // 以下四种是独立的图搜索算法变体，各自有不同的最优性保证和性能特征。
        if (routeAlgorithm_ == "dijkstra") return dijkstraPath(localMap_, start, target);
        if (routeAlgorithm_ == "astar") return astarPath(localMap_, start, target);
        if (routeAlgorithm_ == "branch_bound") return branchBoundPath(localMap_, start, target);
        if (routeAlgorithm_ == "divide_conquer") return divideConquerPath(localMap_, start, target);
        // 不支持的路由算法名直接抛异常，避免静默回退导致行为不一致。
        throw std::runtime_error("unsupported route algorithm: " + routeAlgorithm_);
    }

    /**
     * 功能：根据 reward 分数和目标保持机制选择下一条路径。
     * 输入：
     *   - localCurrent：当前局部坐标。
     * 输出：
     *   - 返回当前应沿着走一步的完整局部路径。
     * 关键逻辑：
     *   - 主评分使用 reward 组件；只有新目标比分数保持目标高出 switchMargin 时才切换。
     */
    std::vector<Position> selectBestPath(Position localCurrent, Position realCurrent, int step,
                                         GreedyStepDebug &debug)
    {
        // 构建评分上下文：包含当前状态、出口路径和 Boss 门控区域信息。
        const PathValueContext context = buildContext(localCurrent);
        // 填充本步调试信息的基础字段。
        debug.step = step;
        debug.localCurrent = localCurrent;
        debug.realCurrent = realCurrent;
        debug.alpha = state_.alphaSmooth;
        // 已观察比例 = 已观察格子数 / 225（15x15 迷宫总格数），衡量探索进度。
        debug.observedRatio = static_cast<double>(poseEstimator_.estimatedObservedCount()) / 225.0;
        // qEff 是当前状态下每步的平均资源效率，用于后续绕路代价比较。
        debug.qEff = evaluator_.computeQEff(context, localMap_);
        debug.decision = "no-candidate";  // 默认决策，后续会被覆盖
        // 记录所有被过滤掉的格子及原因，供前端调试面板展示。
        recordRejectedTargets(localCurrent, debug);
        double bestScore = -1e18;
        bool hasWorthwhileTarget = false;     // 是否存在 reward 超过 qEff * 绕路代价的目标
        bool hasNonNegativeTarget = false;    // 是否存在走完后资源不为负的目标
        // 根据出口是否已知选择面积上限：已知出口时用更保守的 knownExitAreaCap，
        // 因为此时 AI 已经有退路，不应过度追求未知区域的探索价值。
        const double activeAreaCap = context.exitPath.empty() ? static_cast<double>(evaluator_.parameters().areaMax)
                                                              : evaluator_.parameters().knownExitAreaCap;
        const int debugAreaCap = std::max(1, static_cast<int>(std::ceil(activeAreaCap)));
        Position bestTarget = kInvalid;
        std::vector<Position> bestPath;
        std::vector<ClosedSingletonGateCandidate> gateCandidates;  // 供闭单例门控后处理使用
        // 获取当前局部地图中所有可探索候选目标。
        const auto targets = candidateTargets(localCurrent);

        // 对每个候选目标独立评分，构建候选列表和门控请求数据。
        for (const auto &target : targets) {
            // 对该候选目标计算从当前位置出发的路径。
            auto path = routePath(localCurrent, target);
            // 核心评分：evaluate 综合路径长度、资源变化、信息增益等因素给出一个标量分数。
            const double score = evaluator_.evaluate(path, target, context, localMap_, poseEstimator_);
            // 构建该候选的调试信息，供前端监控页展示每个候选的评分分解。
            GreedyCandidateDebug item;
            item.localTarget = target;
            const auto realIt = localToReal_.find(target);
            item.realTarget = realIt == localToReal_.end() ? kInvalid : realIt->second;
            item.tile = localMap_.tile(target);
            item.score = score;
            // deltaR：沿路径走到目标后，资源的净变化（金币收入 + 陷阱扣减）。
            item.deltaR = evaluator_.pathResourceDelta(path, localMap_);
            // Boss 门控区域 + 未知延伸触碰地图边缘时，强制使用 areaMax 评估信息增益，
            // 因为这类区域的实际面积可能远超当前视野所能估计的范围。
            const bool forceAreaMax = context.bossGatedAreaMaxTargets.count(target) > 0 &&
                                      poseEstimator_.unknownExtensionTouchesMazeEdge(target, localMap_);
            // informationProxy：走到目标后预计能观察到的新格子数量，是探索价值的核心度量。
            item.informationProxy =
                evaluator_.informationProxy(target, localMap_, poseEstimator_, activeAreaCap, forceAreaMax);
            // tailGain：到达目标后，从目标出发的后续期望收益，体现远期价值。
            item.tailGain = evaluator_.futureGainMarginal(target, path, localMap_);
            item.qEff = debug.qEff;
            // pathLength：路径步数 = 路径长度 - 1（不含起点）。
            item.pathLength = path.empty() ? 0 : static_cast<int>(path.size()) - 1;
            // projectedResource：到达目标后的预计剩余资源。
            item.projectedResource = state_.resource + item.deltaR;
            // marginPenalty：若预计资源非负，计算当前边界惩罚，防止资源在阈值附近过度冒险。
            item.marginPenalty = item.projectedResource < 0 ? 0.0 : evaluator_.marginPenalty(item.projectedResource);
            // 记录是否存在非负资源候选，供 shouldGoExit 判断。
            hasNonNegativeTarget = hasNonNegativeTarget || (!path.empty() && item.projectedResource >= 0);
            // 统计从目标位置可接触的未知连通分量大小，用于评估探索潜力。
            item.unknownComponents = forceAreaMax
                                         ? std::vector<int>{evaluator_.parameters().bossEdgeAreaBonus}
                                         : poseEstimator_.unknownComponentSizesTouchingView(target, localMap_, debugAreaCap);
            item.unknownComponentSum = 0;
            for (const int size : item.unknownComponents) item.unknownComponentSum += size;
            // 为闭单例门控后处理准备数据：复制关键字段到 gateCandidate 结构。
            ClosedSingletonGateCandidate gateCandidate;
            gateCandidate.target = target;
            gateCandidate.tile = item.tile;
            gateCandidate.score = item.score;
            gateCandidate.deltaR = item.deltaR;
            gateCandidate.informationProxy = item.informationProxy;
            gateCandidate.tailGain = item.tailGain;
            gateCandidate.pathLength = item.pathLength;
            gateCandidate.projectedResource = item.projectedResource;
            gateCandidate.unknownComponentSum = item.unknownComponentSum;
            gateCandidate.unknownComponents = item.unknownComponents;
            gateCandidate.path = path;
            gateCandidates.push_back(std::move(gateCandidate));
            // 计算 hasWorthwhileTarget：候选的 reward 是否超过走到目标再绕路去出口的额外代价。
            // detourCost = 当前位置到目标的步数 + 目标到出口的步数 - 当前位置到出口的步数。
            if (!context.exitPath.empty() && context.exitPath.size() > 1) {
                const auto targetToExit = routePath(target, localExit_);
                if (!targetToExit.empty()) {
                    const int detourCost = item.pathLength + static_cast<int>(targetToExit.size()) - 1 -
                                           (static_cast<int>(context.exitPath.size()) - 1);
                    hasWorthwhileTarget = hasWorthwhileTarget || score > debug.qEff * detourCost;
                }
            }
            debug.candidates.push_back(item);
            // 维护本轮最佳候选，用于 switchMargin 目标切换判断。
            if (score > bestScore) {
                bestScore = score;
                bestTarget = target;
                bestPath = path;
            }
        }

        // 闭单例门控后处理：在候选评分完成后，检测并处理"封闭口袋只有一个入口"的特殊场景。
        // 这种场景下普通 reward 可能低估口袋内部的价值，门控逻辑会提升口袋内目标的优先级。
        ClosedSingletonGateRequest gateRequest;
        gateRequest.localCurrent = localCurrent;
        gateRequest.localExit = localExit_;
        gateRequest.context = context;
        gateRequest.localMap = &localMap_;
        gateRequest.poseEstimator = &poseEstimator_;
        gateRequest.evaluator = &evaluator_;
        gateRequest.candidates = gateCandidates;
        const auto gateResult = applyClosedSingletonLookaheadGate(gateRequest);
        debug.closedSingletonGate = gateResult.debug;  // 门控调试信息供前端展示
        // 门控有选择结果时覆盖最佳候选，重算 hasWorthwhileTarget。
        if (gateResult.hasSelection) {
            bestTarget = gateResult.selectedTarget;
            bestPath = gateResult.selectedPath;
            bestScore = gateResult.selectedScore;
            // 门控改变了选择时，需要重新评估 hasWorthwhileTarget，
            // 排除被门控替换掉的候选 A，避免 shouldGoExit 误判。
            if (gateResult.changed && !context.exitPath.empty() && context.exitPath.size() > 1) {
                hasWorthwhileTarget = false;
                for (const auto &candidate : gateCandidates) {
                    // 跳过被门控覆盖的候选 A 和无效候选。
                    if (candidate.target == gateResult.debug.candidateA || candidate.score <= -1e17 ||
                        candidate.path.empty()) {
                        continue;
                    }
                    const auto targetToExit = routePath(candidate.target, localExit_);
                    if (targetToExit.empty()) continue;
                    const int detourCost = candidate.pathLength + static_cast<int>(targetToExit.size()) - 1 -
                                           (static_cast<int>(context.exitPath.size()) - 1);
                    hasWorthwhileTarget = hasWorthwhileTarget || candidate.score > debug.qEff * detourCost;
                }
            }
        }

        // 决策优先级 1：停止探索，转向出口。
        // shouldGoExit 综合 tau 阈值、资源效率、绕路代价判断是否该收手。
        if (shouldGoExit(context, bestScore, hasWorthwhileTarget, hasNonNegativeTarget)) {
            currentTarget_ = localExit_;
            currentTargetScore_ = bestScore;
            currentTargetFromPocket_ = false;
            debug.decision = "exit";
            debug.selectedLocal = localExit_;
            const auto realIt = localToReal_.find(localExit_);
            debug.selectedReal = realIt == localToReal_.end() ? kInvalid : realIt->second;
            return context.exitPath;
        }

        // 决策优先级 2：检查是否有正在持有的目标（上一帧选择的目标尚未到达）。
        // 目标保持机制防止 AI 在相邻两个目标之间来回摇摆，提高路径稳定性。
        std::vector<Position> heldPath;
        double heldScore = -1e18;
        // 持有目标仍然有效（未访问、可通行）时才计算其当前路径和分数。
        if (currentTarget_ != kInvalid && !localMap_.isVisited(currentTarget_) &&
            localMap_.isWalkableForPlanning(currentTarget_)) {
            heldPath = routePath(localCurrent, currentTarget_);
            heldScore = evaluator_.evaluate(heldPath, currentTarget_, context, localMap_, poseEstimator_);
        }

        // 决策优先级 3：口袋优先目标选择。
        // PocketAwareGreedy 识别"只有一个入口的区域"，若该区域内资源丰富，
        // 则优先占领入口并清空内部，避免被其他目标分散注意力。
        auto pocketDecision =
            choosePocketFirstTarget(localCurrent, context, localMap_, poseEstimator_, evaluator_);
        attachPocketRealPositions(pocketDecision.debug);  // 填充真实坐标供前端展示
        debug.pocket = pocketDecision.debug;
        if (pocketDecision.enabled && !pocketDecision.path.empty()) {
            currentTarget_ = pocketDecision.target;
            currentTargetScore_ = pocketDecision.score;
            currentTargetFromPocket_ = true;  // 标记为目标来自口袋策略
            debug.decision = "pocket-first-target";
            markSelectedDebug(pocketDecision.target, debug);
            return pocketDecision.path;
        }

        // 决策优先级 4：口袋激活后保持当前口袋目标。
        // 即使本轮口袋策略未产生新目标，但只要上一帧选择来自口袋且目标仍可达，
        // 就继续走，避免口袋清空中途切换目标。
        if (currentTargetFromPocket_ && !heldPath.empty()) {
            currentTargetScore_ = heldScore;
            debug.decision = "hold-pocket-target";
            markSelectedDebug(currentTarget_, debug);
            return heldPath;
        }

        // 决策优先级 5：目标切换判定。
        // switchMargin 是切换阈值，新目标分数必须比持有目标高出一个 margin 才切换，
        // 防止在两个分数接近的目标之间反复横跳浪费步数。
        const double switchMargin = evaluator_.parameters().switchMargin;
        const bool shouldSwitch = !bestPath.empty() &&
                                  (heldPath.empty() || bestScore > heldScore + switchMargin);
        // 持有目标仍有效且新目标不值得切换时，继续走持有目标。
        if (!heldPath.empty() && !shouldSwitch) {
            currentTargetScore_ = heldScore;
            debug.decision = "hold-target";
            markSelectedDebug(currentTarget_, debug);
            return heldPath;
        }
        // 新目标分数足够高（或没有持有目标），切换到新目标。
        if (!bestPath.empty()) {
            currentTarget_ = bestTarget;
            currentTargetScore_ = bestScore;
            currentTargetFromPocket_ = false;
            debug.decision = "best-target";
            markSelectedDebug(bestTarget, debug);
            return bestPath;
        }

        // 决策优先级 6：所有正常决策路径都失效时的兜底恢复。
        // 清空目标状态，调用 fallbackPath 以信息增益为导向选一个脱困路径。
        currentTarget_ = kInvalid;
        currentTargetScore_ = -1e18;
        currentTargetFromPocket_ = false;
        debug.decision = "fallback";
        return fallbackPath(localCurrent);
    }

    /**
     * 功能：把 pocket 调试信息中的局部坐标补充为真实坐标。
     * 输入：
     *   - pocket：pocket 评分调试信息。
     * 输出：
     *   - 无返回值，原地填充 realPocketHub、realPocketResources 和候选真实坐标。
     * 关键逻辑：
     *   - PocketAwareGreedy 只读取 localMap，不知道真实迷宫；真实坐标只在这里用于前端展示。
     */
    void attachPocketRealPositions(PocketDebug &pocket) const
    {
        auto toReal = [&](Position local) {
            const auto it = localToReal_.find(local);
            return it == localToReal_.end() ? kInvalid : it->second;
        };

        pocket.realPocketHub = toReal(pocket.pocketHub);
        pocket.realChosenPocketTarget = toReal(pocket.chosenPocketTarget);
        pocket.realPocketResources.clear();
        for (const auto &resource : pocket.pocketResources) {
            pocket.realPocketResources.push_back(toReal(resource));
        }
        for (auto &candidate : pocket.candidates) {
            candidate.realTarget = toReal(candidate.target);
            candidate.realBestRemainingTarget = toReal(candidate.bestRemainingTarget);
        }
    }

    /**
     * 功能：在本步调试候选表中标记实际采用的目标。
     * 输入：
     *   - target：最终选择的局部目标。
     *   - debug：本步调试记录。
     * 输出：
     *   - 无返回值，更新 selectedLocal、selectedReal 和候选 selected 标记。
     * 关键逻辑：
     *   - 前端监控页依赖该标记判断“分数最高”和“实际选择”是否一致。
     */
    void markSelectedDebug(Position target, GreedyStepDebug &debug) const
    {
        debug.selectedLocal = target;
        const auto realIt = localToReal_.find(target);
        debug.selectedReal = realIt == localToReal_.end() ? kInvalid : realIt->second;
        for (auto &item : debug.candidates) {
            item.selected = item.localTarget == target;
        }
    }

    /**
     * 功能：判断是否停止探索并转向出口。
     * 输入：
     *   - context：包含出口路径的评分上下文。
     *   - bestScore：当前最佳探索目标分数。
     *   - hasWorthwhileTarget：是否存在 reward 足以覆盖相对出口绕路机会成本的候选目标。
     *   - hasNonNegativeTarget：是否存在走完后资源不为负的候选目标。
     * 输出：
     *   - 返回是否应直接去出口。
     * 关键逻辑：
     *   - 当前 R/L 为 0 时，除非所有探索候选都会让资源变负，否则禁止提前走出口。
     *   - 当前 R/L 小于 1 且已拾取金币数小于 3 时，只要还有候选目标就继续探索。
     *   - 出口可达且探索收益不超过 tau 时停止探索。
     *   - 即使 bestScore 超过 tau，也必须存在目标 reward 高于 q_eff * 绕路代价，
     *     其中绕路代价 = len(当前位置→target) + len(target→出口) - len(当前位置→出口)，
     *     否则说明探索目标补偿不了绕路多走的步数。
     */
    bool shouldGoExit(const PathValueContext &context, double bestScore, bool hasWorthwhileTarget,
                      bool hasNonNegativeTarget) const
    {
        // 出口路径不存在或长度不足 2（即已在出口或紧邻出口），无法判断是否应走向出口。
        if (context.exitPath.empty() || context.exitPath.size() <= 1) return false;
        // 当前资源为零但存在非负资源候选时禁止提前走出口：
        // 此时走出口虽然安全，但放弃了一切翻盘可能，相当于主动认输。
        if (context.state.resource == 0 && hasNonNegativeTarget) return false;
        // 计算当前资源效率 R/L（资源 / 步数），epsilon 防止除零。
        const double currentRatio =
            static_cast<double>(context.state.resource) / (context.state.steps + evaluator_.parameters().epsilon);
        // 当前 R/L < 1 且金币收集不足 3 个时，只要还有候选目标就继续探索。
        // 这是因为 maze 评分中金币权重高，过早走出口可能错过关键金币导致低分。
        if (currentRatio < 1.0 && context.state.collectedGold < 3 && bestScore > -1e17) return false;
        // 若最佳候选的 reward 分数不超过阈值 tau，说明探索边际收益已耗尽，应转向出口。
        if (bestScore <= evaluator_.parameters().tau) return true;
        // 即使 bestScore 超过 tau，还需检查是否存在 reward 足以覆盖绕路代价的目标。
        // hasWorthwhileTarget 在 selectBestPath 中已计算，衡量 score > qEff * detourCost。
        // 若没有，说明所有高 reward 目标绕路步数太多，不如直接去出口。
        return !hasWorthwhileTarget;
    }

    /**
     * 功能：没有正常候选时选择脱困路径。
     * 输入：
     *   - localCurrent：当前局部坐标。
     * 输出：
     *   - 返回通向出口或能带来更多新视野的局部路径。
     * 关键逻辑：
     *   - fallback 不加入 reward 项，只用于避免候选为空时停在原地。
     *   - 当 ratio < 1 且金币数 < 3 时，优先尝试非出口探索路径；没有探索路径时才允许走出口。
     */
    std::vector<Position> fallbackPath(Position localCurrent) const
    {
        // 计算当前 R/L 比率，与 shouldGoExit 使用相同逻辑判断是否延迟走出口。
        const double currentRatio =
            static_cast<double>(state_.resource) / (state_.steps + evaluator_.parameters().epsilon);
        // 延迟出口：R/L < 1 且金币不足 3 时，应优先探索而非直接去出口。
        const bool delayExit = currentRatio < 1.0 && state_.collectedGold < 3;
        // 出口已知且允许走出口时，直接返回出口路径作为兜底方案。
        if (localExit_ != kInvalid && !delayExit) {
            const auto exitPath = routePath(localCurrent, localExit_);
            if (!exitPath.empty()) return exitPath;
        }

        // 没有正常候选时，退而求其次：选择能带来最多信息增益的目标。
        // informationProxy 估算从目标位置能观察到多少新未知区域。
        std::vector<Position> bestPath;
        double bestInfo = -1.0;
        for (const auto &target : localMap_.observedPositions()) {
            // 排除当前格；若延迟出口则也排除出口格，强制探索。
            if (target == localCurrent || (delayExit && target == localExit_) ||
                !localMap_.isWalkableForPlanning(target)) {
                continue;
            }
            auto path = routePath(localCurrent, target);
            if (path.empty()) continue;
            // 根据出口是否已知选择不同的面积上限，控制信息代理的计算范围。
            const double activeAreaCap = localExit_ == kInvalid ? static_cast<double>(evaluator_.parameters().areaMax)
                                                                : evaluator_.parameters().knownExitAreaCap;
            const double info = evaluator_.informationProxy(target, localMap_, poseEstimator_, activeAreaCap);
            // 信息增益更高者优先；相同时选路径更短的，减少步数浪费。
            if (info > bestInfo || (info == bestInfo && (bestPath.empty() || path.size() < bestPath.size()))) {
                bestInfo = info;
                bestPath = std::move(path);
            }
        }
        // 延迟出口期间若所有目标的信息增益都为零（已无新区域可探索），清空路径。
        if (delayExit && bestInfo <= 0.0) bestPath.clear();
        // 最终兜底：一切探索路径都找不到时，只要出口已知就走出口。
        if (bestPath.empty() && localExit_ != kInvalid) {
            const auto exitPath = routePath(localCurrent, localExit_);
            if (!exitPath.empty()) return exitPath;
        }
        return bestPath;
    }
};
} // namespace

/**
 * 功能：执行 3x3 实时贪心探索策略。
 * 输入：
 *   - data：任务迷宫数据。
 *   - routeAlgorithm：reward 选中目标后使用的两点路由算法。
 *   - parameters：本次运行使用的 reward 参数。
 * 输出：
 *   - 返回真实坐标路径，外部接口保持不变。
 * 关键逻辑：
 *   - 内部使用局部记忆地图和可复用 reward 组件评分，每次只走重规划路径的下一步。
 *   - 分治、分支限界、Dijkstra、A* 都只作为 routeAlgorithm，不承担完整探索。
 */
std::vector<Position> realtimeGreedyPath(const MazeData &data, const std::string &routeAlgorithm,
                                         RewardParameters parameters)
{
    return realtimeGreedyRun(data, routeAlgorithm, parameters).path;
}

/**
 * 功能：执行 3x3 实时贪心探索并返回路径调试信息。
 * 输入：
 *   - data：任务迷宫数据。
 *   - routeAlgorithm：reward 选中目标后使用的两点路由算法。
 *   - parameters：本次运行使用的 reward 参数。
 * 输出：
 *   - 返回真实坐标路径和每步候选目标评分分解。
 * 关键逻辑：
 *   - 该接口供前端评分监控页使用，不改变原有 realtimeGreedyPath 对外路径接口。
 *   - routeAlgorithm 只影响路径搜索，不改变 reward 目标选择公式。
 */
GreedyRunResult realtimeGreedyRun(const MazeData &data, const std::string &routeAlgorithm,
                                  RewardParameters parameters)
{
    MemoryGreedyAgent agent(data, routeAlgorithm, parameters);
    return agent.run();
}
} // namespace ai_player
