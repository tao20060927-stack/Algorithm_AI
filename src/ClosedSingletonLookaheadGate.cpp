#include "ClosedSingletonLookaheadGate.h"

#include <algorithm>
#include <cmath>
#include <optional>
#include <set>

namespace ai_player {
namespace {

constexpr double kInvalidScore = -1e17;

struct MemoryContinuation {
    bool simulated = false;
    bool safe = false;
    Position bestTarget{kInvalid};
    double bestReward = -1e18;
};

/**
 * 功能：判断分数是否为可参与选择的有效 reward。
 * 输入：
 *   - score：候选 reward 分数。
 * 输出：
 *   - 返回该分数是否不是负无穷类非法值。
 * 关键逻辑：
 *   - 当前项目用 -1e18 表示非法候选，因此用较宽松阈值过滤。
 */
bool validScore(double score)
{
    return score > kInvalidScore && std::isfinite(score);
}

/**
 * 功能：判断局部坐标是否位于目标 A 的 3x3 邻域。
 * 输入：
 *   - center：目标 A。
 *   - pos：待检查坐标。
 * 输出：
 *   - 返回 pos 是否落在 center 周围 3x3 范围内。
 * 关键逻辑：
 *   - c_A 不能使用 A 周围 3x3 新视野，也不能考虑这个区域内的候选。
 */
bool insideThreeByThree(Position center, Position pos)
{
    return std::abs(center.first - pos.first) <= 1 && std::abs(center.second - pos.second) <= 1;
}

/**
 * 功能：判断候选是否为本 gate 处理的 |C|=1 封闭小节点。
 * 输入：
 *   - candidate：已完成 reward 评分的候选。
 * 输出：
 *   - 返回候选是否满足 closed-singleton 的保守判定。
 * 关键逻辑：
 *   - 第一版只用现有 |C| 字段，不新增 openDegree 等特征；|C|=1 说明它是封闭小未知块。
 */
bool isClosedSingletonCandidate(const ClosedSingletonGateCandidate &candidate)
{
    return validScore(candidate.score) && !candidate.path.empty() && candidate.pathLength > 0 &&
           candidate.unknownComponentSum == 1 && candidate.tile != "E";
}

/**
 * 功能：判断候选是否是可作为 B 的有效开放/非封闭候选。
 * 输入：
 *   - candidate：当前候选。
 *   - candidateA：当前 top-1 封闭小候选。
 * 输出：
 *   - 返回 candidate 是否可以作为与 A 比较的 B。
 * 关键逻辑：
 *   - |C|=0 且没有资源或 tail 的候选视为死节点；closed-singleton 本身也不作为 B。
 */
bool isMeaningfulNonClosedCandidate(const ClosedSingletonGateCandidate &candidate,
                                    const ClosedSingletonGateCandidate &candidateA)
{
    if (candidate.target == candidateA.target) return false;
    if (!validScore(candidate.score) || candidate.path.empty() || candidate.pathLength <= 0) return false;
    if (candidate.projectedResource < 0) return false;
    if (isClosedSingletonCandidate(candidate)) return false;
    if (candidate.unknownComponentSum == 0 && candidate.deltaR <= 0 && candidate.tailGain <= 0.0 &&
        candidate.tile != "E") {
        return false;
    }
    return candidate.unknownComponentSum > 1 || candidate.deltaR > 0 || candidate.tailGain > 0.0 ||
           candidate.tile == "E";
}

/**
 * 功能：在 memory-only 模拟中执行到 A 的已知路径。
 * 输入：
 *   - candidateA：封闭小候选 A，包含当前到 A 的已知路径。
 *   - request：当前状态和 known_map。
 * 输出：
 *   - 若能安全模拟，返回更新后的局部地图和状态；否则返回空。
 * 关键逻辑：
 *   - 只结算当前已知路径上的金币/陷阱和 visited，不调用 3x3 视野更新，不触发未知信息读取。
 */
std::optional<std::pair<LocalKnownMap, AgentState>>
simulateExecuteAWithoutNewVision(const ClosedSingletonGateCandidate &candidateA,
                                 const ClosedSingletonGateRequest &request)
{
    if (!request.localMap || candidateA.path.size() <= 1) return std::nullopt;

    LocalKnownMap localMapAfter = *request.localMap;
    AgentState stateAfter = request.context.state;
    for (size_t i = 1; i < candidateA.path.size(); ++i) {
        const Position pos = candidateA.path[i];
        if (!localMapAfter.isWalkableForPlanning(pos)) return std::nullopt;
        // Boss 正邻接格会触发现有 Boss 战状态机；这里不复制 Boss 战模拟，避免和真实执行逻辑分叉。
        if (localMapAfter.isBossTrigger(pos)) return std::nullopt;

        localMapAfter.markVisited(pos);
        const std::string tile = localMapAfter.tile(pos);
        if (tile == "G" && !localMapAfter.isCollected(pos)) {
            stateAfter.resource += kGoldValue;
            ++stateAfter.collectedGold;
            localMapAfter.markCollected(pos);
        } else if (tile == "T" && !localMapAfter.isTriggered(pos)) {
            stateAfter.resource += kTrapValue;
            localMapAfter.markTriggered(pos);
        }
        if (stateAfter.resource < 0) return std::nullopt;
        ++stateAfter.steps;
    }
    return std::make_pair(localMapAfter, stateAfter);
}

/**
 * 功能：判断 A 后状态中的候选是否是有效 continuation。
 * 输入：
 *   - score：A 后重新计算的 reward。
 *   - delta：路径真实资源变化。
 *   - info：Iproxy 信息价值。
 *   - tail：边际 tail 上界。
 *   - path：A 到候选的 known-map 路径。
 *   - projectedResource：走完候选后的资源。
 * 输出：
 *   - 返回该候选是否能参与 c_A 最大值计算。
 * 关键逻辑：
 *   - 过滤 -inf、不可达、资源非法，以及 |C|=0 且没有任何收益来源的死节点。
 */
bool validContinuationCandidate(double score, int delta, double info, double tail,
                                const std::vector<Position> &path, int projectedResource)
{
    if (!validScore(score) || path.empty() || path.size() <= 1) return false;
    if (projectedResource < 0) return false;
    return !(delta <= 0 && info == 0.0 && tail <= 0.0);
}

/**
 * 功能：计算执行 A 后的 memory-only 最高有效后继 reward。
 * 输入：
 *   - candidateA：当前 top-1 封闭小候选。
 *   - request：当前 known_map、评分器、姿态估计和上下文。
 * 输出：
 *   - 返回是否完成安全模拟，以及 c_A 的目标和分数。
 * 关键逻辑：
 *   - 不加入 A 周围 3x3 新视野；只在现有 memory 中排除 A 周围 3x3 候选后重新评分。
 */
MemoryContinuation computeMemoryOnlyContinuationAfterA(const ClosedSingletonGateCandidate &candidateA,
                                                       const ClosedSingletonGateRequest &request)
{
    MemoryContinuation continuation;
    const auto simulated = simulateExecuteAWithoutNewVision(candidateA, request);
    continuation.simulated = true;
    if (!simulated || !request.evaluator || !request.poseEstimator) return continuation;

    const LocalKnownMap &localMapAfter = simulated->first;
    PathValueContext contextAfter = request.context;
    contextAfter.state = simulated->second;
    if (request.localExit != kInvalid) {
        contextAfter.exitPath =
            request.evaluator->shortestPathOnKnownMap(candidateA.target, request.localExit, localMapAfter);
    } else {
        contextAfter.exitPath.clear();
    }

    continuation.safe = true;
    const double activeAreaCap = contextAfter.exitPath.empty()
                                     ? static_cast<double>(request.evaluator->parameters().areaMax)
                                     : request.evaluator->parameters().knownExitAreaCap;
    const int debugAreaCap = std::max(1, static_cast<int>(std::ceil(activeAreaCap)));

    for (const auto &target : localMapAfter.observedPositions()) {
        if (target == candidateA.target || target == request.localExit || insideThreeByThree(candidateA.target, target) ||
            localMapAfter.isVisited(target) || !localMapAfter.isWalkableForPlanning(target)) {
            continue;
        }

        const auto path = request.evaluator->shortestPathOnKnownMap(candidateA.target, target, localMapAfter);
        const double score =
            request.evaluator->evaluate(path, target, contextAfter, localMapAfter, *request.poseEstimator);
        const int delta = request.evaluator->pathResourceDelta(path, localMapAfter);
        const int projectedResource = contextAfter.state.resource + delta;
        const double info =
            request.evaluator->informationProxy(target, localMapAfter, *request.poseEstimator, activeAreaCap);
        const double tail = request.evaluator->futureGainMarginal(target, path, localMapAfter);
        const auto components =
            request.poseEstimator->unknownComponentSizesTouchingView(target, localMapAfter, debugAreaCap);
        int componentSum = 0;
        for (const int size : components) componentSum += size;

        if (!validContinuationCandidate(score, delta, info, tail, path, projectedResource)) continue;
        // A 后 continuation 仍然排除 |C|=0 死节点；开放或资源候选才可成为 c_A。
        if (componentSum == 0 && delta <= 0 && tail <= 0.0) continue;
        if (score > continuation.bestReward) {
            continuation.bestReward = score;
            continuation.bestTarget = target;
        }
    }
    return continuation;
}

/**
 * 功能：从当前候选队列中找到最高分候选。
 * 输入：
 *   - candidates：已完成评分的候选列表。
 * 输出：
 *   - 返回最高分候选下标，不存在有效候选时返回空。
 * 关键逻辑：
 *   - gate 只检查当前 top-1，不重新定义全局排序。
 */
std::optional<size_t> findTopCandidate(const std::vector<ClosedSingletonGateCandidate> &candidates)
{
    std::optional<size_t> bestIndex;
    double bestScore = -1e18;
    for (size_t i = 0; i < candidates.size(); ++i) {
        if (!validScore(candidates[i].score)) continue;
        if (!bestIndex || candidates[i].score > bestScore) {
            bestIndex = i;
            bestScore = candidates[i].score;
        }
    }
    return bestIndex;
}

/**
 * 功能：从当前候选队列中找到最好的非封闭有效候选 B。
 * 输入：
 *   - candidates：已完成评分的候选列表。
 *   - candidateA：当前 top-1 封闭小候选。
 * 输出：
 *   - 返回 B 的下标；不存在时返回空。
 * 关键逻辑：
 *   - B 只使用当前 reward，不计算 after-B continuation。
 */
std::optional<size_t> findBestNonClosedCandidate(const std::vector<ClosedSingletonGateCandidate> &candidates,
                                                 const ClosedSingletonGateCandidate &candidateA)
{
    std::optional<size_t> bestIndex;
    double bestScore = -1e18;
    for (size_t i = 0; i < candidates.size(); ++i) {
        if (!isMeaningfulNonClosedCandidate(candidates[i], candidateA)) continue;
        if (!bestIndex || candidates[i].score > bestScore) {
            bestIndex = i;
            bestScore = candidates[i].score;
        }
    }
    return bestIndex;
}

} // namespace

ClosedSingletonGateResult applyClosedSingletonLookaheadGate(const ClosedSingletonGateRequest &request)
{
    ClosedSingletonGateResult result;
    result.debug.checked = true;
    if (!request.localMap || !request.poseEstimator || !request.evaluator) {
        result.debug.disabledReason = "missing gate dependencies";
        return result;
    }

    const auto topIndex = findTopCandidate(request.candidates);
    if (!topIndex) {
        result.debug.disabledReason = "no valid candidate";
        return result;
    }

    const auto &candidateA = request.candidates[*topIndex];
    result.debug.candidateA = candidateA.target;
    result.debug.candidateAType = candidateA.tile.empty() ? "space" : candidateA.tile;
    result.debug.componentSizeA = candidateA.unknownComponentSum;
    result.debug.rewardA = candidateA.score;
    result.debug.gamma = request.evaluator->parameters().gammaClosedSingleton;
    result.debug.margin = request.evaluator->parameters().marginClosedSingleton;
    result.debug.isClosedSingletonA = isClosedSingletonCandidate(candidateA);
    if (!result.debug.isClosedSingletonA) {
        result.debug.disabledReason = "top candidate is not closed singleton";
        return result;
    }

    const auto bestBIndex = findBestNonClosedCandidate(request.candidates, candidateA);
    if (!bestBIndex) {
        result.hasSelection = true;
        result.selectedTarget = candidateA.target;
        result.selectedPath = candidateA.path;
        result.selectedScore = candidateA.score;
        result.debug.triggered = true;
        result.debug.allowed = true;
        result.debug.reason = "Closed singleton allowed: no meaningful non-closed candidate exists.";
        return result;
    }

    const auto &candidateB = request.candidates[*bestBIndex];
    result.debug.triggered = true;
    result.debug.bestNonClosedB = candidateB.target;
    result.debug.rewardB = candidateB.score;

    const MemoryContinuation continuation = computeMemoryOnlyContinuationAfterA(candidateA, request);
    result.debug.simulatedAfterA = continuation.simulated && continuation.safe;
    if (!continuation.safe) {
        result.debug.triggered = false;
        result.debug.disabledReason = "cannot safely simulate candidate A without diverging from real execution";
        return result;
    }

    result.debug.bestAfterATarget = continuation.bestTarget;
    result.debug.bestAfterAReward = continuation.bestReward;
    result.debug.combinedA =
        validScore(continuation.bestReward)
            ? candidateA.score + request.evaluator->parameters().gammaClosedSingleton * continuation.bestReward
            : -1e18;

    // 非对称 gate：只计算 A 的 memory-only continuation，不计算开放候选 B 的 c_B。
    result.debug.allowed =
        result.debug.combinedA > candidateB.score + request.evaluator->parameters().marginClosedSingleton;
    result.hasSelection = true;
    if (result.debug.allowed) {
        result.selectedTarget = candidateA.target;
        result.selectedPath = candidateA.path;
        result.selectedScore = candidateA.score;
        result.debug.reason = "Closed singleton allowed: reward(A) + gamma * c_A beats best non-closed candidate.";
    } else {
        result.changed = true;
        result.selectedTarget = candidateB.target;
        result.selectedPath = candidateB.path;
        result.selectedScore = candidateB.score;
        result.debug.reason =
            "Closed singleton rejected: after-A best reward is not enough to justify selecting A.";
    }
    return result;
}

} // namespace ai_player
