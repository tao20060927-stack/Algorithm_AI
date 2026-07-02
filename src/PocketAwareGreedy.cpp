#include "PocketAwareGreedy.h"

#include <algorithm>
#include <cmath>

namespace ai_player {
namespace {

/**
 * 功能：计算局部路径步数。
 * 输入：
 *   - path：局部坐标路径。
 * 输出：
 *   - 返回路径边数；空路径返回 0。
 * 关键逻辑：
 *   - pocket 评分只需要路径长度机会成本，路径中第一个点是当前位置，不计入步数。
 */
int pathLength(const std::vector<Position> &path)
{
    return path.empty() ? 0 : static_cast<int>(path.size()) - 1;
}

/**
 * 功能：按局部路径长度比较两个坐标集合候选。
 * 输入：
 *   - current：AI 当前坐标。
 *   - left/right：两个 hub 候选。
 *   - evaluator/localMap：用于在已知地图上计算路径。
 * 输出：
 *   - 如果 left 比 right 更靠近当前坐标，返回 true。
 * 关键逻辑：
 *   - 多个 hub 都能形成 pocket 时，优先资源数量多，再优先离当前更近。
 */
bool closerHub(Position current, Position left, Position right, const PathValueEvaluator &evaluator,
               const LocalKnownMap &localMap)
{
    const auto leftPath = evaluator.shortestPathOnKnownMap(current, left, localMap);
    const auto rightPath = evaluator.shortestPathOnKnownMap(current, right, localMap);
    return pathLength(leftPath) < pathLength(rightPath);
}

/**
 * 功能：收集当前点和相邻已知可走格作为 hub 候选。
 * 输入：
 *   - localCurrent：AI 当前局部坐标。
 *   - localMap：局部已知地图。
 * 输出：
 *   - 返回 hub 候选列表。
 * 关键逻辑：
 *   - hub 只来自当前位置及其上下左右可通行邻居，不引入新的全局探索特征。
 */
std::vector<Position> hubCandidates(Position localCurrent, const LocalKnownMap &localMap)
{
    std::vector<Position> hubs{localCurrent};
    for (const auto [dr, dc] : kDirs) {
        const Position next{localCurrent.first + dr, localCurrent.second + dc};
        if (localMap.isWalkableForPlanning(next)) hubs.push_back(next);
    }
    return hubs;
}

/**
 * 功能：计算指定 hub 半径内已知可达且未收集的金币集合。
 * 输入：
 *   - hub：局部口袋中心候选。
 *   - localMap：局部已知地图。
 *   - evaluator：用于在 known_map 上求最短路径。
 *   - pocketRadius：口袋半径上限。
 * 输出：
 *   - 返回 hub 可达、距离不超过半径的未收集金币。
 * 关键逻辑：
 *   - 只用 localMap.knownCoins()，不会读取未知金币或完整迷宫。
 */
std::vector<Position> pocketCoinsForHub(Position hub, const LocalKnownMap &localMap,
                                        const PathValueEvaluator &evaluator, int pocketRadius)
{
    std::vector<Position> coins;
    // 遍历所有已知但尚未收集的金币。
    // 只用 localMap.knownCoins()——只返回当前已知地图中可见的金币，
    // 不会读取未知区域或完整迷宫数据。
    // 这确保了 pocket 识别在 fog-of-war 限制下完全可用。
    for (const auto &coin : localMap.knownCoins()) {
        // 计算从 hub（口袋中心）到该金币的最短已知路径。
        const auto path = evaluator.shortestPathOnKnownMap(hub, coin, localMap);
        // 两个条件决定金币是否属于此 hub 的口袋范围：
        // （1）路径存在（hub 可达该金币）；
        // （2）路径长度不超过 pocketRadius（在口袋半径内）。
        // pocketRadius 是全局参数，控制"局部口袋"的最大覆盖半径。
        // 半径越小，口袋越紧凑；半径越大，越多的远处金币会被归入同一口袋。
        if (!path.empty() && pathLength(path) <= pocketRadius) coins.push_back(coin);
    }
    return coins;
}

/**
 * 功能：识别当前局部资源口袋。
 * 输入：
 *   - localCurrent：AI 当前局部坐标。
 *   - localMap/evaluator：局部地图和路径工具。
 * 输出：
 *   - 通过 selectedHub 返回口袋 hub，函数返回 pocket 内金币集合。
 * 关键逻辑：
 *   - 至少两个未收集金币才形成 pocket；多个 pocket 优先金币数最多，再优先 hub 距当前更近。
 */
std::vector<Position> findPocket(Position localCurrent, const LocalKnownMap &localMap,
                                 const PathValueEvaluator &evaluator, int pocketRadius, Position &selectedHub)
{
    std::vector<Position> bestCoins;
    selectedHub = kInvalid;
    // 遍历所有 hub 候选（当前位置 + 相邻可走格）。
    // hub 是口袋的"中心点"——所有在 hub 半径内可达的金币被归为同一个口袋。
    // 不同的 hub 可能产生不同的口袋（金币集合），选择最优的那个。
    for (const auto &hub : hubCandidates(localCurrent, localMap)) {
        // 计算该 hub 半径内可达且未收集的金币集合。
        auto coins = pocketCoinsForHub(hub, localMap, evaluator, pocketRadius);
        // 至少需要 2 个金币才能形成"口袋"。
        // 单个金币没有"留到后面再收"的规划价值——
        // 单独的金币直接用 greedy 收掉即可，不需要 pocket-aware 策略介入。
        if (coins.size() < 2) continue;
        // 口袋比较策略（两级排序）：
        // （1）主排序：金币数量越多越好（更大的口袋有更高的规划价值）；
        // （2）次排序：金币数量相同时，hub 离当前位置越近越好
        //     （距离近意味着浪费的路径机会成本更小）。
        const bool betterCount = coins.size() > bestCoins.size();
        const bool betterDistance = coins.size() == bestCoins.size() &&
                                    (selectedHub == kInvalid || closerHub(localCurrent, hub, selectedHub, evaluator, localMap));
        // 满足任意一个更优条件即更新最优口袋。
        if (betterCount || betterDistance) {
            selectedHub = hub;
            bestCoins = std::move(coins);
        }
    }
    // 返回最优口袋的金币集合。如果没找到口袋（所有 hub 的金币数都 < 2），
    // bestCoins 为空且 selectedHub 为 kInvalid。
    return bestCoins;
}

/**
 * 功能：根据 Score_first 比较两个 pocket 金币候选。
 * 输入：
 *   - best：当前最优候选。
 *   - candidate：待比较候选。
 * 输出：
 *   - 如果 candidate 更应该作为第一目标，返回 true。
 * 关键逻辑：
 *   - 主排序为 Score_first；并列时优先路径短，再按坐标稳定输出。
 */
bool betterPocketCandidate(const PocketCandidateDebug &best, const PocketCandidateDebug &candidate)
{
    constexpr double kEps = 1e-9;
    if (candidate.scoreFirst > best.scoreFirst + kEps) return true;
    if (std::abs(candidate.scoreFirst - best.scoreFirst) <= kEps && candidate.pathLength != best.pathLength) {
        return candidate.pathLength < best.pathLength;
    }
    if (std::abs(candidate.scoreFirst - best.scoreFirst) <= kEps && candidate.pathLength == best.pathLength) {
        return candidate.target < best.target;
    }
    return false;
}

} // namespace

PocketDecision choosePocketFirstTarget(Position localCurrent,
                                       const PathValueContext &context,
                                       const LocalKnownMap &localMap,
                                       const MapPoseEstimator &poseEstimator,
                                       const PathValueEvaluator &evaluator)
{
    PocketDecision decision;
    const auto &parameters = evaluator.parameters();
    Position hub = kInvalid;
    // 步骤 1：识别当前局部资源口袋。
    // 在当前已知地图上搜索所有 hub 候选的口袋。
    // 如果找不到有效口袋（金币数 < 2），返回空决策——
    // 没有口袋就不需要"口袋优先"策略介入，由 greedy 正常决策。
    // 数据流：当前位置 -> hubCandidates -> pocketCoinsForHub(每个 hub) -> findPocket(选最优) -> pocketCoins。
    const auto pocketCoins = findPocket(localCurrent, localMap, evaluator, parameters.pocketRadius, hub);
    if (pocketCoins.size() < 2) return decision;
    // 步骤 2：口袋存在，启用 pocket-aware 决策逻辑。
    // 填充调试信息：记录口袋 hub 坐标和口袋内所有金币。
    // 这些信息用于后续分析和可视化 pocket-aware 策略的决策过程。
    decision.enabled = true;
    decision.debug.enabled = true;
    decision.debug.pocketHub = hub;
    decision.debug.pocketResources = pocketCoins;
    decision.debug.reason = "Pocket-aware first target greedy: high-Iproxy resource is kept for later.";
    // qEff：单位路径长度的机会成本因子。
    // 由 evaluator 根据当前上下文和已知地图计算。
    // 它把路径步数换算为等价的资源损失，用于比较不同路径长度的候选。
    // qEff 越高，长路径受到的惩罚越重。
    const double qEff = evaluator.computeQEff(context, localMap);
    PocketCandidateDebug best;
    best.scoreFirst = -1e18;
    // 步骤 3：遍历口袋中每个金币，计算其作为"第一个目标"的综合评分 Score_first。
    // Score_first 的核心思想：选择第一个目标时，不仅要考虑收该金币的直接收益，
    // 还要考虑收完它后口袋中剩余金币的信息价值——
    // 信息价值高的金币应该"留到后面"收，让后续步获得更多地图信息用于规划。
    for (const auto &coin : pocketCoins) {
        // 计算从当前位置到该金币的最短已知路径。
        const auto path = evaluator.shortestPathOnKnownMap(localCurrent, coin, localMap);
        // 空路径（不可达）或路径仅包含起点（距离为 0）的候选不可用。
        // 路径长度至少为 1 才表示有实际的移动。
        if (path.empty() || path.size() <= 1) continue;
        // 构建候选调试信息。
        PocketCandidateDebug item;
        item.target = coin;
        item.pathLength = pathLength(path);
        // deltaR：路径上的净资源变化（金币增加值 + 陷阱扣减值）。
        // 这反映了走完该路径后的即时资源收益。
        item.deltaR = evaluator.pathResourceDelta(path, localMap);
        // 资源负性检查：走完该路径后资源必须非负。
        // 如果路径中存在陷阱导致资源变为负值，该路径不可行——
        // 在实际执行中 AI 不会选择让资源变负的路径。
        // 将不可行候选的 baseScore 和 scoreFirst 设为 -inf，保留在 candidates 列表中用于调试，
        // 但不会参与 best 比较。
        if (!evaluator.pathKeepsResourceNonNegative(path, context.state.resource, localMap)) {
            item.baseScore = -1e18;
            item.scoreFirst = -1e18;
            decision.debug.candidates.push_back(item);
            continue;
        }
        // baseScore = 即时资源收益 - 路径长度机会成本。
        // 公式：baseScore = deltaR - qEffLengthWeight * qEff * pathLength。
        // qEffLengthWeight 控制路径成本在 baseScore 中的权重。
        // baseScore 衡量的是"走这条路收这个金币的纯直接价值"，
        // 不考虑口袋中其他金币的信息价值。
        item.baseScore = item.deltaR - parameters.qEffLengthWeight * qEff * item.pathLength;
        // ownIproxy：该金币自身的信息代理值。
        // 衡量到达该金币位置后预期能揭示多少新地图区域。
        // 高的 ownIproxy 意味着收这个金币能获得更多地图信息。
        item.ownIproxy = evaluator.informationProxy(coin, localMap, poseEstimator);
        item.bestRemainingIproxy = 0.0;
        item.remainI = 0.0;
        // 步骤 4：计算 remainI——口袋中"剩余"金币的折扣信息价值最大值。
        // 对于口袋中除当前金币外的每个剩余金币：
        //   remainI = Iproxy_of_remaining / (1 + mu * distance_to_remaining)
        // 距离折扣公式的含义：
        //   - 分母中的 1 保证即使距离为 0，remainI 也不会发散；
        //   - mu 控制距离惩罚力度：mu 越大，远处金币的信息价值折扣越重；
        //   - 距离越远的剩余金币，其信息价值对"当前选择"的影响越小。
        // 取所有剩余金币中 remainI 最大的作为 item.remainI——
        // 保守估计：假设收完当前金币后，下一步会去收信息价值最高的那个剩余金币。
        for (const auto &remaining : pocketCoins) {
            if (remaining == coin) continue;
            // 计算从当前金币到剩余金币的已知路径。
            const auto remainingPath = evaluator.shortestPathOnKnownMap(coin, remaining, localMap);
            if (remainingPath.empty()) continue;
            // 剩余金币的信息代理值（在剩余金币位置能揭示的新地图区域）。
            const double remainingIproxy = evaluator.informationProxy(remaining, localMap, poseEstimator);
            // 距离折扣：1.0 + mu * distance，mu 越大折扣越快。
            const double distanceDiscount = 1.0 + parameters.pocketMu * pathLength(remainingPath);
            const double remainI = remainingIproxy / distanceDiscount;
            // 保留最大的 remainI，作为"后续信息价值"的代表。
            if (remainI > item.remainI) {
                item.remainI = remainI;
                item.bestRemainingIproxy = remainingIproxy;
                item.bestRemainingTarget = remaining;
            }
        }
        // 步骤 5：计算 Score_first——口袋优先第一目标评分。
        // 公式：Score_first = baseScore + lambdaRemain * remainI。
        // - baseScore：收当前金币的直接价值（资源 - 路径成本）。
        // - lambdaRemain * remainI：口袋中剩余金币的折扣信息价值加权。
        //   lambdaRemain 控制"后续信息价值"在总评分中的权重。
        //   如果 lambdaRemain 较大，算法倾向于先收信息价值低的金币，
        //   把信息价值高的留到后面——因为收后面的金币时会获得更多地图信息，
        //   有助于做更优的后续决策。
        item.scoreFirst = item.baseScore + parameters.pocketLambdaRemain * item.remainI;
        decision.debug.candidates.push_back(item);
        // 步骤 6：选择 Score_first 最高的金币作为口袋第一目标。
        // 通过 betterPocketCandidate 实现两级比较：
        // （1）Score_first 更高优先；（2）路径更短优先；（3）坐标字典序稳定输出。
        if (betterPocketCandidate(best, item)) {
            best = item;
            decision.target = coin;
            decision.path = path;
            decision.score = item.scoreFirst;
        }
    }
    // 步骤 7：兜底检查。
    // 如果所有候选都因资源约束（路径走完后资源为负）或不可达被过滤掉，
    // 最终决策的 path 为空。此时禁用口袋决策，让 greedy 正常处理。
    if (decision.path.empty()) {
        decision.enabled = false;
        decision.debug.enabled = false;
        return decision;
    }
    // 标记最终选择的目标，并在所有候选上标记是否被选中，
    // 用于调试输出和可视化 pocket-aware 的决策过程。
    decision.debug.chosenPocketTarget = decision.target;
    for (auto &item : decision.debug.candidates) {
        item.selected = item.target == decision.target;
    }
    return decision;
}

} // namespace ai_player
