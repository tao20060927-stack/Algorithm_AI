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
        Position realCurrent = maze_.start;
        Position localCurrent{0, 0};
        localToReal_[localCurrent] = realCurrent;
        realToLocal_[realCurrent] = localCurrent;

        GreedyRunResult result;
        result.path.push_back(realCurrent);
        const int maxSteps = static_cast<int>(maze_.grid.size() * maze_.grid[0].size() * 4);

        for (int step = 0; step < maxSteps && realCurrent != maze_.exit; ++step) {
            updateKnownMap(realCurrent, localCurrent);
            applyCurrentCell(localCurrent);
            poseEstimator_.update(localMap_, localCurrent);
            state_.alphaSmooth = evaluator_.updateAlphaSmooth(state_.alphaSmooth, localMap_, poseEstimator_);
            if (pendingRevive_ || gameOver_) {
                GreedyStepDebug stepDebug;
                fillStatusDebug(localCurrent, realCurrent, static_cast<int>(result.path.size()) - 1,
                                pendingRevive_ ? "boss-revive-reset" : "boss-game-over", stepDebug);
                result.debugSteps.push_back(std::move(stepDebug));
                if (pendingRevive_) {
                    pendingRevive_ = false;
                    localCurrent = {0, 0};
                    realCurrent = maze_.start;
                    currentTarget_ = kInvalid;
                    currentTargetScore_ = -1e18;
                    currentTargetFromPocket_ = false;
                    result.path.push_back(realCurrent);
                    continue;
                }
                result.gameOver = true;
                break;
            }

            GreedyStepDebug stepDebug;
            const auto selectedPath = selectBestPath(localCurrent, realCurrent, static_cast<int>(result.path.size()) - 1,
                                                     stepDebug);
            result.debugSteps.push_back(std::move(stepDebug));
            if (selectedPath.size() <= 1) break;

            const Position nextLocal = selectedPath[1];
            const auto it = localToReal_.find(nextLocal);
            if (it == localToReal_.end()) break;
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
        for (int dr = -1; dr <= 1; ++dr) {
            for (int dc = -1; dc <= 1; ++dc) {
                const Position realPos{realCurrent.first + dr, realCurrent.second + dc};
                if (!realInBounds(realPos.first, realPos.second)) continue;

                const Position localPos{localCurrent.first + dr, localCurrent.second + dc};
                std::string tile = maze_.grid[realPos.first][realPos.second];
                const int bossIndex = bossIndexAt(localPos);
                if (tile == "B" && bossIndex >= 0 && defeatedBosses_[bossIndex]) {
                    tile = " ";
                }

                localMap_.setObserved(localPos, tile);
                localToReal_[localPos] = realPos;
                realToLocal_[realPos] = localPos;

                if (tile == "B" && bossIndex < 0) {
                    knownBosses_.push_back(localPos);
                    defeatedBosses_.push_back(false);
                    localMap_.markBossTriggers(localPos);
                }
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
        localMap_.markVisited(localCurrent);
        if (state_.steps > 0 && localMap_.tile(localCurrent) == "G" && !localMap_.isCollected(localCurrent)) {
            state_.resource += kGoldValue;
            localMap_.markCollected(localCurrent);
        } else if (state_.steps > 0 && localMap_.tile(localCurrent) == "T" && !localMap_.isTriggered(localCurrent)) {
            state_.resource += kTrapValue;
            localMap_.markTriggered(localCurrent);
        }
        triggerAdjacentBoss(localCurrent);
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
        for (int i = 0; i < static_cast<int>(knownBosses_.size()); ++i) {
            const int distance = std::abs(localCurrent.first - knownBosses_[i].first) +
                                 std::abs(localCurrent.second - knownBosses_[i].second);
            if (distance == 1 && !defeatedBosses_[i]) {
                if (bossBattleCanWin_) {
                    defeatedBosses_[i] = true;
                    localMap_.clearBoss(knownBosses_[i]);
                } else if (state_.resource >= coinConsumption_) {
                    state_.resource -= coinConsumption_;
                    pendingRevive_ = true;
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
        for (const auto &pos : localMap_.observedPositions()) {
            if (pos == localCurrent || pos == localExit_ || localMap_.isVisited(pos) ||
                !localMap_.isWalkableForPlanning(pos)) {
                continue;
            }
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
        return context;
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
        if (routeAlgorithm_ == "greedy" || routeAlgorithm_ == "smart") {
            return evaluator_.shortestPathOnKnownMap(start, target, localMap_);
        }
        if (routeAlgorithm_ == "dijkstra") return dijkstraPath(localMap_, start, target);
        if (routeAlgorithm_ == "astar") return astarPath(localMap_, start, target);
        if (routeAlgorithm_ == "branch_bound") return branchBoundPath(localMap_, start, target);
        if (routeAlgorithm_ == "divide_conquer") return divideConquerPath(localMap_, start, target);
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
        const PathValueContext context = buildContext(localCurrent);
        debug.step = step;
        debug.localCurrent = localCurrent;
        debug.realCurrent = realCurrent;
        debug.alpha = state_.alphaSmooth;
        debug.observedRatio = static_cast<double>(poseEstimator_.estimatedObservedCount()) / 225.0;
        debug.qEff = evaluator_.computeQEff(context, localMap_);
        debug.decision = "no-candidate";
        recordRejectedTargets(localCurrent, debug);
        double bestScore = -1e18;
        bool hasWorthwhileTarget = false;
        bool hasNonNegativeTarget = false;
        Position bestTarget = kInvalid;
        std::vector<Position> bestPath;

        for (const auto &target : candidateTargets(localCurrent)) {
            auto path = routePath(localCurrent, target);
            const double score = evaluator_.evaluate(path, target, context, localMap_, poseEstimator_);
            GreedyCandidateDebug item;
            item.localTarget = target;
            const auto realIt = localToReal_.find(target);
            item.realTarget = realIt == localToReal_.end() ? kInvalid : realIt->second;
            item.tile = localMap_.tile(target);
            item.score = score;
            item.deltaR = evaluator_.pathResourceDelta(path, localMap_);
            item.informationProxy = evaluator_.informationProxy(target, localMap_, poseEstimator_);
            item.tailGain = evaluator_.futureGainMarginal(target, path, localMap_);
            item.qEff = debug.qEff;
            item.pathLength = path.empty() ? 0 : static_cast<int>(path.size()) - 1;
            item.projectedResource = state_.resource + item.deltaR;
            item.marginPenalty = item.projectedResource < 0 ? 0.0 : evaluator_.marginPenalty(item.projectedResource);
            hasNonNegativeTarget = hasNonNegativeTarget || (!path.empty() && item.projectedResource >= 0);
            item.unknownComponents =
                poseEstimator_.unknownComponentSizesTouchingView(target, localMap_, evaluator_.parameters().areaMax);
            item.unknownComponentSum = 0;
            for (const int size : item.unknownComponents) item.unknownComponentSum += size;
            if (!context.exitPath.empty() && context.exitPath.size() > 1) {
                const int exitLength = static_cast<int>(context.exitPath.size()) - 1;
                hasWorthwhileTarget = hasWorthwhileTarget || score > debug.qEff * (item.pathLength - exitLength);
            }
            debug.candidates.push_back(item);
            if (score > bestScore) {
                bestScore = score;
                bestTarget = target;
                bestPath = std::move(path);
            }
        }

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

        std::vector<Position> heldPath;
        double heldScore = -1e18;
        if (currentTarget_ != kInvalid && !localMap_.isVisited(currentTarget_) &&
            localMap_.isWalkableForPlanning(currentTarget_)) {
            heldPath = routePath(localCurrent, currentTarget_);
            heldScore = evaluator_.evaluate(heldPath, currentTarget_, context, localMap_, poseEstimator_);
        }

        auto pocketDecision =
            choosePocketFirstTarget(localCurrent, context, localMap_, poseEstimator_, evaluator_);
        attachPocketRealPositions(pocketDecision.debug);
        debug.pocket = pocketDecision.debug;
        if (pocketDecision.enabled && !pocketDecision.path.empty()) {
            currentTarget_ = pocketDecision.target;
            currentTargetScore_ = pocketDecision.score;
            currentTargetFromPocket_ = true;
            debug.decision = "pocket-first-target";
            markSelectedDebug(pocketDecision.target, debug);
            return pocketDecision.path;
        }

        if (currentTargetFromPocket_ && !heldPath.empty()) {
            currentTargetScore_ = heldScore;
            debug.decision = "hold-pocket-target";
            markSelectedDebug(currentTarget_, debug);
            return heldPath;
        }

        const double switchMargin = evaluator_.parameters().switchMargin;
        const bool shouldSwitch = !bestPath.empty() &&
                                  (heldPath.empty() || bestScore > heldScore + switchMargin);
        if (!heldPath.empty() && !shouldSwitch) {
            currentTargetScore_ = heldScore;
            debug.decision = "hold-target";
            markSelectedDebug(currentTarget_, debug);
            return heldPath;
        }
        if (!bestPath.empty()) {
            currentTarget_ = bestTarget;
            currentTargetScore_ = bestScore;
            currentTargetFromPocket_ = false;
            debug.decision = "best-target";
            markSelectedDebug(bestTarget, debug);
            return bestPath;
        }

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
     *   - 出口可达且探索收益不超过 tau 时停止探索。
     *   - 即使 bestScore 超过 tau，也必须存在目标高于 q_eff * (len(target)-len(exit))，否则说明探索目标补偿不了绕路机会成本。
     */
    bool shouldGoExit(const PathValueContext &context, double bestScore, bool hasWorthwhileTarget,
                      bool hasNonNegativeTarget) const
    {
        if (context.exitPath.empty() || context.exitPath.size() <= 1) return false;
        if (context.state.resource == 0 && hasNonNegativeTarget) return false;
        if (bestScore <= evaluator_.parameters().tau) return true;
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
     */
    std::vector<Position> fallbackPath(Position localCurrent) const
    {
        if (localExit_ != kInvalid) {
            const auto exitPath = routePath(localCurrent, localExit_);
            if (!exitPath.empty()) return exitPath;
        }

        std::vector<Position> bestPath;
        double bestInfo = -1.0;
        for (const auto &target : localMap_.observedPositions()) {
            if (target == localCurrent || !localMap_.isWalkableForPlanning(target)) continue;
            auto path = routePath(localCurrent, target);
            if (path.empty()) continue;
            const double info = evaluator_.informationProxy(target, localMap_, poseEstimator_);
            if (info > bestInfo || (info == bestInfo && (bestPath.empty() || path.size() < bestPath.size()))) {
                bestInfo = info;
                bestPath = std::move(path);
            }
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
