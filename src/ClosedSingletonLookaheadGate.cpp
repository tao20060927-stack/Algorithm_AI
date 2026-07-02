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
    // |C|=1 表示该候选接触的未知组件大小恰好为 1，
    // 即该格子周围只有一个独立的小未知区域，属于"封闭小单例"（closed singleton）。
    // 这类候选的信息增益有限（最多揭示一个小的未知块），
    // 但其即时 reward 可能较高，容易在 greedy 排序中排到第一。
    // gate 的核心任务就是判断：选择这个 closed singleton 的长期价值
    // 是否真的优于选择一个开放候选 B。
    // tile != "E" 排除终点——终点不是信息获取型候选，
    // 不应被 gate 拦截或重新排序，直接按正常流程处理即可。
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
    // 不能把 A 自身当作 B 来比较——gate 需要找一个"替代方案"与 A 竞争，
    // 而不是让 A 和自己比。
    if (candidate.target == candidateA.target) return false;
    // 基础有效性检查：score 必须合法，路径必须存在且长度大于 0。
    // 如果连最基本的路径都没有，不可能作为有效的替代候选 B。
    if (!validScore(candidate.score) || candidate.path.empty() || candidate.pathLength <= 0) return false;
    // projectedResource < 0 表示执行该候选后资源会变为负。
    // 负资源状态在实际执行中不可行——AI 不会选择让资源变负的路径。
    if (candidate.projectedResource < 0) return false;
    // 如果该候选本身也是 closed singleton，不作为 B。
    // B 的角色是"有意义的开放候选"——代表与 A 竞争的替代方案。
    // 如果 B 也是 closed singleton，gate 就会变成两个小节点互相比较，
    // 失去了"用开放候选检验封闭候选"的意义。
    if (isClosedSingletonCandidate(candidate)) return false;
    // |C|=0 且 deltaR<=0（无即时资源收益）且 tailGain<=0（无长远收益）
    // 且非终点的候选是"死节点"（dead node）。
    // 走完它既不会获得新地图信息（|C|=0），也不会获得资源或长远收益，
    // 没有理由放弃 A 去选它。排除这类候选可以避免 gate 把 A 和一个无意义的
    // 候选做比较，导致 A 被错误放行。
    if (candidate.unknownComponentSum == 0 && candidate.deltaR <= 0 && candidate.tailGain <= 0.0 &&
        candidate.tile != "E") {
        return false;
    }
    // 最终判定：至少有一条"有意义"的特征——
    // 有未知区域（|C|>1）、有资源增益、有长远收益、或是终点。
    // 满足任意一条即认为该候选可以作为与 A 比较的 B。
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
    // 如果局部地图不存在，或路径只有起点（长度 <= 1），无法模拟执行。
    // 路径长度至少为 2 才意味着有实际的移动步骤。
    if (!request.localMap || candidateA.path.size() <= 1) return std::nullopt;
    // 深拷贝局部地图和状态，创建模拟专用的副本。
    // 因为模拟过程中会修改 visited、collected、triggered 等状态，
    // 必须与原始数据隔离，避免污染当前的真实状态。
    LocalKnownMap localMapAfter = *request.localMap;
    AgentState stateAfter = request.context.state;
    // 从路径的第二个点开始（第一个点是当前位置，不需要"走"），
    // 逐步模拟每一步的实际效果。
    for (size_t i = 1; i < candidateA.path.size(); ++i) {
        const Position pos = candidateA.path[i];
        // 每一步检查该位置是否仍可通行。
        // 如果路径上的格子已被标记为不可走（例如墙壁或越界），
        // 说明已知地图与路径不一致，模拟失败——这意味着路径信息已过期。
        if (!localMapAfter.isWalkableForPlanning(pos)) return std::nullopt;
        // Boss 正邻接格会触发现有的 Boss 战斗状态机。
        // 这里跳过 Boss 触发的原因：
        // （1）Boss 战涉及复杂的回合制状态机，无法在 memory-only 模拟中精确复现；
        // （2）强行模拟 Boss 战会导致与真实执行逻辑分叉，
        //     使得 gate 的决策基于不准确的模拟结果；
        // （3）保守处理：遇到 Boss 触发时放弃模拟，
        //     避免基于错误模拟做出 gate 决策。
        if (localMapAfter.isBossTrigger(pos)) return std::nullopt;
        // 标记该位置为已访问，模拟地图探索过程。
        // 同一个位置只会被标记一次，重复标记不影响正确性。
        localMapAfter.markVisited(pos);
        const std::string tile = localMapAfter.tile(pos);
        // 如果是金币且未被收集过：增加资源值，累加收集金币计数，标记为已收集。
        // 这模拟了真实行走中拾取金币的行为——金币只能被收集一次。
        if (tile == "G" && !localMapAfter.isCollected(pos)) {
            stateAfter.resource += kGoldValue;
            ++stateAfter.collectedGold;
            localMapAfter.markCollected(pos);
        }
        // 如果是陷阱且未被触发过：扣减资源（陷阱值为负），标记为已触发。
        else if (tile == "T" && !localMapAfter.isTriggered(pos)) {
            stateAfter.resource += kTrapValue;
            localMapAfter.markTriggered(pos);
        }
        // 如果资源变为负数，说明当前路径不可行。
        // 在实际执行中 AI 的路径搜索也会排除使资源变负的路径，
        // 因此模拟也必须保持这个约束，否则会高估 A 的可达性。
        if (stateAfter.resource < 0) return std::nullopt;
        // 每走一步累加步数，用于后续的路径成本估算和 reward 计算。
        ++stateAfter.steps;
    }
    // 返回模拟后的（地图，状态）对，供 computeMemoryOnlyContinuationAfterA 使用，
    // 计算 A 之后的最佳后继 reward c_A。
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
    // 第一步：在 memory-only（不获得新视野）条件下模拟执行 A 的路径。
    // 目的是计算走完 A 后的资源状态、已访问集合和金币收集情况。
    // 这里的关键假设是"不获得 A 周围 3x3 的新视野"——
    // 因为在决策时 A 尚未执行，不应预支执行 A 后才能看到的信息。
    const auto simulated = simulateExecuteAWithoutNewVision(candidateA, request);
    continuation.simulated = true;
    // 模拟失败或缺少评分器/姿态估计器时，无法继续计算 c_A。
    // continuation.bestReward 保持 -1e18，后续会被 validScore 过滤。
    if (!simulated || !request.evaluator || !request.poseEstimator) return continuation;
    // 获取模拟后的地图和状态，用于在 A 之后重新评分所有候选。
    const LocalKnownMap &localMapAfter = simulated->first;
    PathValueContext contextAfter = request.context;
    contextAfter.state = simulated->second;
    // 如果已知出口位置，在 A 后的地图上重新计算到出口的路径。
    // 走完 A 后 visited 标记和金币收集状态发生了变化，
    // 从 A 到出口的最优路径可能与原来不同。
    if (request.localExit != kInvalid) {
        contextAfter.exitPath =
            request.evaluator->shortestPathOnKnownMap(candidateA.target, request.localExit, localMapAfter);
    } else {
        contextAfter.exitPath.clear();
    }
    // 模拟成功，标记 continuation 为 safe。
    // 这意味着 A 的路径在 memory-only 条件下是可行的。
    continuation.safe = true;
    // activeAreaCap：信息代理（Iproxy）计算的面积上限。
    // 若出口路径存在，使用专用的已知出口面积上限（knownExitAreaCap），
    // 因为出口可见时信息搜索范围应更保守；
    // 若出口未知，使用默认的全局面积上限（areaMax）。
    const double activeAreaCap = contextAfter.exitPath.empty()
                                     ? static_cast<double>(request.evaluator->parameters().areaMax)
                                     : request.evaluator->parameters().knownExitAreaCap;
    const int debugAreaCap = std::max(1, static_cast<int>(std::ceil(activeAreaCap)));
    // 遍历 A 后地图上的所有已观测位置，寻找 c_A——A 之后的最佳后继候选。
    // c_A 是"在 A 之后、不依赖 A 的新视野、仅用现有已知地图"能走的最佳目标的 reward。
    for (const auto &target : localMapAfter.observedPositions()) {
        // 排除以下位置：
        // - A 自身：不能原地不动。
        // - 出口：出口路径已单独处理，不参与 c_A 候选评分。
        // - A 的 3x3 邻域：这是关键排除！因为 c_A 模拟的前提是
        //   "不使用走完 A 后获得的新视野"。A 的 3x3 范围内的信息
        //   只有实际执行 A 后才能获得，不应在 memory-only 模拟中使用。
        //   如果允许使用 3x3 内的候选，c_A 会被高估，
        //   导致 gate 错误地放行 A。
        // - 已访问位置：再次去已访问位置没有探索价值。
        // - 不可通行位置：不能作为目标。
        if (target == candidateA.target || target == request.localExit || insideThreeByThree(candidateA.target, target) ||
            localMapAfter.isVisited(target) || !localMapAfter.isWalkableForPlanning(target)) {
            continue;
        }
        // 在 A 后的已知地图上计算从 A 到候选目标的最短路径。
        const auto path = request.evaluator->shortestPathOnKnownMap(candidateA.target, target, localMapAfter);
        // 用 A 后的上下文重新评分该候选。
        // 上下文包含了 A 执行后的资源、步数、出口路径等，
        // 确保评分反映的是"真正走完 A 之后"的决策环境。
        const double score =
            request.evaluator->evaluate(path, target, contextAfter, localMapAfter, *request.poseEstimator);
        // 路径资源增量：包含路径上所有金币和陷阱的净值。
        const int delta = request.evaluator->pathResourceDelta(path, localMapAfter);
        // 走完该候选后的总资源 = A 后资源 + 候选路径资源变化。
        const int projectedResource = contextAfter.state.resource + delta;
        // 信息代理值：该候选预期揭示的新地图区域价值。
        const double info =
            request.evaluator->informationProxy(target, localMapAfter, *request.poseEstimator, activeAreaCap);
        // 边际尾部长远收益：走完该候选后还能获得的长远回报上界估计。
        const double tail = request.evaluator->futureGainMarginal(target, path, localMapAfter);
        // 该候选接触到的所有未知区域组件大小之和。
        // 组件大小反映了该候选的"信息发现潜力"。
        const auto components =
            request.poseEstimator->unknownComponentSizesTouchingView(target, localMapAfter, debugAreaCap);
        int componentSum = 0;
        for (const int size : components) componentSum += size;
        // 过滤无效候选：score 非法、路径不可达、资源为负。
        if (!validContinuationCandidate(score, delta, info, tail, path, projectedResource)) continue;
        // 二次过滤：仍然排除 |C|=0 且无 delta/tail 收益的死节点。
        // 尽管在 validContinuationCandidate 中已经做了基础过滤，
        // 这里增加额外的 componentSum 检查是为了更严格地排除
        // 既没有未知区域信息、也没有即时或长远收益的候选。
        if (componentSum == 0 && delta <= 0 && tail <= 0.0) continue;
        // 取所有有效 continuation 中 reward 最高的作为 c_A。
        // c_A 将用于与 B 的即时 reward 比较，判断 A 的长期价值。
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
    // 依赖检查：localMap 用于路径计算，poseEstimator 用于未知区域大小估计，
    // evaluator 用于 reward 评分和参数读取。三者缺一不可，
    // 缺少任何一个都无法完成 gate 的完整判断流程。
    if (!request.localMap || !request.poseEstimator || !request.evaluator) {
        result.debug.disabledReason = "missing gate dependencies";
        return result;
    }
    // 步骤 1：找到当前评分最高的候选 A（top-1）。
    // Gate 只干预 top-1 是 closed singleton 的情况。
    // 如果 top-1 本身就是开放候选或终点，
    // 说明 greedy 的选择已经是合理的，gate 不需要介入。
    const auto topIndex = findTopCandidate(request.candidates);
    if (!topIndex) {
        result.debug.disabledReason = "no valid candidate";
        return result;
    }
    // 记录候选 A 的基础信息，用于后续调试和决策过程的可追溯性。
    const auto &candidateA = request.candidates[*topIndex];
    result.debug.candidateA = candidateA.target;
    result.debug.candidateAType = candidateA.tile.empty() ? "space" : candidateA.tile;
    result.debug.componentSizeA = candidateA.unknownComponentSum;
    result.debug.rewardA = candidateA.score;
    // gamma：后续 reward 的折扣因子，控制对 long-term 价值的权重。
    // 默认值通常为 1.0，表示不打折扣。
    // margin：安全边际，防止因微小数值差异频繁切换选择。
    result.debug.gamma = request.evaluator->parameters().gammaClosedSingleton;
    result.debug.margin = request.evaluator->parameters().marginClosedSingleton;
    // 步骤 2：检查 A 是否真的是 closed singleton（|C|=1 且非终点）。
    // 这是 gate 触发的必要条件——如果 A 不是封闭小候选，
    // 说明 greedy 的 top-1 已经是合理的开放选择，gate 不需要干预。
    result.debug.isClosedSingletonA = isClosedSingletonCandidate(candidateA);
    if (!result.debug.isClosedSingletonA) {
        result.debug.disabledReason = "top candidate is not closed singleton";
        return result;
    }
    // 步骤 3：找到最好的非封闭候选 B。
    // B 代表"如果不选 A，当前最优的替代方案是什么"。
    // B 不会被进一步分析其 continuation（非对称设计），
    // 仅用当前 reward 作为与 A 比较的基准。
    const auto bestBIndex = findBestNonClosedCandidate(request.candidates, candidateA);
    if (!bestBIndex) {
        // 如果不存在有意义的非封闭候选 B，说明：
        // - 除了 A 之外所有候选都是死节点或也是 closed singleton；
        // - 或者所有候选都因资源约束不可行。
        // 此时没有替代方案可以比较，直接放行 A。
        result.hasSelection = true;
        result.selectedTarget = candidateA.target;
        result.selectedPath = candidateA.path;
        result.selectedScore = candidateA.score;
        result.debug.triggered = true;
        result.debug.allowed = true;
        result.debug.reason = "Closed singleton allowed: no meaningful non-closed candidate exists.";
        return result;
    }
    // 记录候选 B 的信息用于调试。
    const auto &candidateB = request.candidates[*bestBIndex];
    result.debug.triggered = true;
    result.debug.bestNonClosedB = candidateB.target;
    result.debug.rewardB = candidateB.score;
    // 步骤 4：计算 memory-only continuation c_A。
    // c_A 是在"执行 A 但不获得新视野"的假设下，
    // 从 A 之后能走的最佳后继目标的 reward。
    // 这个模拟是保守的——不假设 A 会揭示新信息（3x3 内），
    // 只用当前已知地图评估 A 的后续价值。
    // 数据流：candidateA -> simulateExecuteAWithoutNewVision -> 更新地图/状态 ->
    //         在 A 后地图上对所有观测位置重新评分 -> 取最高 reward 作为 c_A。
    const MemoryContinuation continuation = computeMemoryOnlyContinuationAfterA(candidateA, request);
    result.debug.simulatedAfterA = continuation.simulated && continuation.safe;
    if (!continuation.safe) {
        // 无法安全模拟 A——可能因为路径经过 Boss 触发格，
        // 或者路径在 A 后地图上变得不可达。
        // 此时保守地回退，不做 gate 干预，
        // 让 greedy 自然选择 top-1（即 A）。
        // 这避免了基于不完整模拟做出错误决策的风险。
        result.debug.triggered = false;
        result.debug.disabledReason = "cannot safely simulate candidate A without diverging from real execution";
        return result;
    }
    // 记录 c_A 的目标坐标和 reward 值用于调试。
    result.debug.bestAfterATarget = continuation.bestTarget;
    result.debug.bestAfterAReward = continuation.bestReward;
    // 步骤 5：计算 combinedA = reward(A) + gamma * c_A。
    // combinedA 是 A 的"总价值"——即时 reward 加上折扣后的最佳 continuation reward。
    // 如果 c_A 不存在有效值（即 A 之后没有值得走的候选），
    // 则 combinedA 退化为 -1e18，等价于 -inf。
    // 这表示 A 没有后续价值，此时 combinedA 不可能大于任何有效的 reward(B) + margin。
    result.debug.combinedA =
        validScore(continuation.bestReward)
            ? candidateA.score + request.evaluator->parameters().gammaClosedSingleton * continuation.bestReward
            : -1e18;
    // 步骤 6（核心比较）：非对称 gate 决策。
    // 比较 combinedA = reward(A) + gamma * c_A 与 reward(B) + margin。
    //
    // margin 的安全边际作用：
    // - 防止因浮点精度或微小数值差异而频繁切换选择。
    // - 实现"保守倾向"——在没有明确优势时不干预 greedy 的原始选择，
    //   只有 A 的 combined 显著优于 B 时才放行 A。
    //
    // 非对称设计（只计算 c_A，不计算 c_B）的原因：
    // （1）B 是开放候选（|C|>1），其后续路径高度依赖走完 B 后获得的新视野，
    //     memory-only 模拟对 B 的误差会很大，计算 c_B 意义有限。
    // （2）Gate 的目标是"防止过早选择 closed singleton"，
    //     而非找到全局最优解。只需要证明 A 的长期价值不够，
    //     就可以安全地拒绝 A 并选择 B。
    // （3）省略 c_B 使得 gate 偏保守：如果 A 的 combined 不敌 B 的即时 reward + margin，
    //     就拒绝 A。这符合"宁可错过 closed singleton，也不错误延迟开放候选"的设计倾向。
    // 非对称 gate：只计算 A 的 memory-only continuation，不计算开放候选 B 的 c_B。
    result.debug.allowed =
        result.debug.combinedA > candidateB.score + request.evaluator->parameters().marginClosedSingleton;
    result.hasSelection = true;
    if (result.debug.allowed) {
        // A 通过 gate：即使考虑了 A 的长期 continuation，
        // combinedA 仍然大于 reward(B) + margin。
        // 说明选择 closed singleton A 的总价值确实优于选择开放候选 B。
        // 保持原选择（A）不变，不修改 changed 标志。
        result.selectedTarget = candidateA.target;
        result.selectedPath = candidateA.path;
        result.selectedScore = candidateA.score;
        result.debug.reason = "Closed singleton allowed: reward(A) + gamma * c_A beats best non-closed candidate.";
    } else {
        // A 被 gate 拒绝：combinedA <= reward(B) + margin，
        // 即 closed singleton 的即时 + 后续价值不足以证明
        // 放弃当前最优开放候选 B 是合理的。
        // 标记 changed=true，将选择切换到 B——
        // 这是 gate 改变 greedy 决策的唯一路径。
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
