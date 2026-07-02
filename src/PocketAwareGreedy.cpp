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
    for (const auto &coin : localMap.knownCoins()) {
        const auto path = evaluator.shortestPathOnKnownMap(hub, coin, localMap);
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
    for (const auto &hub : hubCandidates(localCurrent, localMap)) {
        auto coins = pocketCoinsForHub(hub, localMap, evaluator, pocketRadius);
        if (coins.size() < 2) continue;
        const bool betterCount = coins.size() > bestCoins.size();
        const bool betterDistance = coins.size() == bestCoins.size() &&
                                    (selectedHub == kInvalid || closerHub(localCurrent, hub, selectedHub, evaluator, localMap));
        if (betterCount || betterDistance) {
            selectedHub = hub;
            bestCoins = std::move(coins);
        }
    }
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
    const auto pocketCoins = findPocket(localCurrent, localMap, evaluator, parameters.pocketRadius, hub);
    if (pocketCoins.size() < 2) return decision;

    decision.enabled = true;
    decision.debug.enabled = true;
    decision.debug.pocketHub = hub;
    decision.debug.pocketResources = pocketCoins;
    decision.debug.reason = "Pocket-aware first target greedy: high-Iproxy resource is kept for later.";

    const double qEff = evaluator.computeQEff(context, localMap);
    PocketCandidateDebug best;
    best.scoreFirst = -1e18;

    for (const auto &coin : pocketCoins) {
        const auto path = evaluator.shortestPathOnKnownMap(localCurrent, coin, localMap);
        if (path.empty() || path.size() <= 1) continue;

        PocketCandidateDebug item;
        item.target = coin;
        item.pathLength = pathLength(path);
        item.deltaR = evaluator.pathResourceDelta(path, localMap);
        if (!evaluator.pathKeepsResourceNonNegative(path, context.state.resource, localMap)) {
            item.baseScore = -1e18;
            item.scoreFirst = -1e18;
            decision.debug.candidates.push_back(item);
            continue;
        }
        item.baseScore = item.deltaR - parameters.qEffLengthWeight * qEff * item.pathLength;
        item.ownIproxy = evaluator.informationProxy(coin, localMap, poseEstimator);
        item.bestRemainingIproxy = 0.0;
        item.remainI = 0.0;

        for (const auto &remaining : pocketCoins) {
            if (remaining == coin) continue;
            const auto remainingPath = evaluator.shortestPathOnKnownMap(coin, remaining, localMap);
            if (remainingPath.empty()) continue;
            const double remainingIproxy = evaluator.informationProxy(remaining, localMap, poseEstimator);
            const double distanceDiscount = 1.0 + parameters.pocketMu * pathLength(remainingPath);
            const double remainI = remainingIproxy / distanceDiscount;
            if (remainI > item.remainI) {
                item.remainI = remainI;
                item.bestRemainingIproxy = remainingIproxy;
                item.bestRemainingTarget = remaining;
            }
        }

        item.scoreFirst = item.baseScore + parameters.pocketLambdaRemain * item.remainI;
        decision.debug.candidates.push_back(item);
        if (betterPocketCandidate(best, item)) {
            best = item;
            decision.target = coin;
            decision.path = path;
            decision.score = item.scoreFirst;
        }
    }

    if (decision.path.empty()) {
        decision.enabled = false;
        decision.debug.enabled = false;
        return decision;
    }

    decision.debug.chosenPocketTarget = decision.target;
    for (auto &item : decision.debug.candidates) {
        item.selected = item.target == decision.target;
    }
    return decision;
}

} // namespace ai_player
