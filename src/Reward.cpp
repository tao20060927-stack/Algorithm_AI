#include "Reward.h"

#include <algorithm>
#include <cmath>
#include <climits>
#include <queue>

namespace ai_player {
namespace {
bool insideEstimated(Position pos)
{
    return pos.first >= 0 && pos.second >= 0 && pos.first < 15 && pos.second < 15;
}

int pathLength(const std::vector<Position> &path)
{
    return path.empty() ? 0 : static_cast<int>(path.size()) - 1;
}
} // namespace

/**
 * 功能：记录局部坐标中的已观察格子。
 * 输入：
 *   - localPos：以入口为原点的局部坐标。
 *   - tile：3x3 视野实际观察到的格子类型。
 * 输出：
 *   - 无返回值，更新局部记忆地图。
 * 关键逻辑：
 *   - 只保存已经观察到的信息；Boss 触发区标记不会因为后续观察被清除。
 */
void LocalKnownMap::setObserved(Position localPos, const std::string &tile)
{
    auto &cell = cells_[localPos];
    cell.observed = true;
    cell.tile = tile;
}

/**
 * 功能：根据已观察到的 Boss 本体标记上下左右四个触发区。
 * 输入：
 *   - localBoss：Boss 在局部坐标系中的位置。
 * 输出：
 *   - 无返回值，更新触发区标记。
 * 关键逻辑：
 *   - 触发区即使尚未被观察，也会作为已知危险约束参与后续路径规划。
 */
void LocalKnownMap::markBossTriggers(Position localBoss)
{
    for (const auto [dr, dc] : kDirs) {
        cells_[{localBoss.first + dr, localBoss.second + dc}].bossTrigger = true;
    }
}

/**
 * 功能：Boss 战触发后清除 Boss 本体阻挡。
 * 输入：
 *   - localBoss：Boss 在局部坐标系中的位置。
 * 输出：
 *   - 无返回值，将 Boss 本体按普通通路记录。
 * 关键逻辑：
 *   - Boss 战由正邻接格强制触发；触发后 Boss 本体不再作为路径障碍。
 */
void LocalKnownMap::clearBoss(Position localBoss)
{
    auto &cell = cells_[localBoss];
    cell.observed = true;
    cell.tile = " ";
}

/**
 * 功能：标记局部格子已经被 AI 实际踩过。
 * 输入：
 *   - localPos：局部坐标。
 * 输出：
 *   - 无返回值，更新访问状态。
 * 关键逻辑：
 *   - 访问状态只用于候选目标过滤，不作为 reward 惩罚项。
 */
void LocalKnownMap::markVisited(Position localPos)
{
    cells_[localPos].visited = true;
}

/**
 * 功能：标记金币已经被拾取。
 * 输入：
 *   - localPos：金币所在的局部坐标。
 * 输出：
 *   - 无返回值，更新一次性资源状态。
 * 关键逻辑：
 *   - 后续路径价值计算遇到该金币时不再重复加分。
 */
void LocalKnownMap::markCollected(Position localPos)
{
    cells_[localPos].collected = true;
}

/**
 * 功能：标记陷阱已经触发。
 * 输入：
 *   - localPos：陷阱所在的局部坐标。
 * 输出：
 *   - 无返回值，更新一次性陷阱状态。
 * 关键逻辑：
 *   - 陷阱只在第一次踩到时扣资源，之后在路径收益里按普通格处理。
 */
void LocalKnownMap::markTriggered(Position localPos)
{
    cells_[localPos].triggered = true;
}

/**
 * 功能：判断局部坐标是否已有记忆记录。
 * 输入：
 *   - localPos：局部坐标。
 * 输出：
 *   - 返回该坐标是否存在于局部记忆地图中。
 * 关键逻辑：
 *   - 未出现过的坐标视为未知，不能作为路径节点。
 */
bool LocalKnownMap::has(Position localPos) const
{
    return cells_.find(localPos) != cells_.end();
}

/**
 * 功能：判断局部格子是否已经观察。
 * 输入：
 *   - localPos：局部坐标。
 * 输出：
 *   - 返回是否已经由 3x3 视野点亮。
 * 关键逻辑：
 *   - 未观察格不能作为目标，也不能被路径穿过。
 */
bool LocalKnownMap::isObserved(Position localPos) const
{
    const auto it = cells_.find(localPos);
    return it != cells_.end() && it->second.observed;
}

/**
 * 功能：判断局部格子是否已经访问。
 * 输入：
 *   - localPos：局部坐标。
 * 输出：
 *   - 返回 AI 是否实际踩过该格。
 * 关键逻辑：
 *   - 已访问格可以作为路径中转点，但不能作为探索目标。
 */
bool LocalKnownMap::isVisited(Position localPos) const
{
    const auto it = cells_.find(localPos);
    return it != cells_.end() && it->second.visited;
}

/**
 * 功能：判断金币是否已经拾取。
 * 输入：
 *   - localPos：局部坐标。
 * 输出：
 *   - 返回该格金币是否已经结算。
 * 关键逻辑：
 *   - 用于避免路径真实收益和后续金币机会重复计算同一枚金币。
 */
bool LocalKnownMap::isCollected(Position localPos) const
{
    const auto it = cells_.find(localPos);
    return it != cells_.end() && it->second.collected;
}

/**
 * 功能：判断陷阱是否已经触发。
 * 输入：
 *   - localPos：局部坐标。
 * 输出：
 *   - 返回该格陷阱是否已经扣过资源。
 * 关键逻辑：
 *   - 已触发陷阱不再计入路径负收益。
 */
bool LocalKnownMap::isTriggered(Position localPos) const
{
    const auto it = cells_.find(localPos);
    return it != cells_.end() && it->second.triggered;
}

/**
 * 功能：判断局部格子是否为 Boss 正邻接触发区。
 * 输入：
 *   - localPos：局部坐标。
 * 输出：
 *   - 返回该格是否被标记为 Boss 触发区。
 * 关键逻辑：
 *   - 触发区可通行，但保留该标记用于动态 alpha 的 Boss 风险统计和战斗触发判断。
 */
bool LocalKnownMap::isBossTrigger(Position localPos) const
{
    const auto it = cells_.find(localPos);
    return it != cells_.end() && it->second.bossTrigger;
}

/**
 * 功能：判断局部格子能否作为规划节点。
 * 输入：
 *   - localPos：局部坐标。
 * 输出：
 *   - 返回该格能否在 known_map 路径搜索中通行。
 * 关键逻辑：
 *   - 只允许穿过已观察、非墙、非 Boss 本体的格子；Boss 正邻接格可走，走到即触发 Boss 战。
 */
bool LocalKnownMap::isWalkableForPlanning(Position localPos) const
{
    const auto it = cells_.find(localPos);
    if (it == cells_.end() || !it->second.observed) return false;
    return it->second.tile != "#" && it->second.tile != "B";
}

/**
 * 功能：读取局部格子的已知类型。
 * 输入：
 *   - localPos：局部坐标。
 * 输出：
 *   - 返回格子类型；未知格返回 "U"。
 * 关键逻辑：
 *   - reward 只能使用已经进入局部记忆地图的信息。
 */
std::string LocalKnownMap::tile(Position localPos) const
{
    const auto it = cells_.find(localPos);
    return it == cells_.end() ? "U" : it->second.tile;
}

/**
 * 功能：列出所有已观察局部格子。
 * 输入：
 *   - 无。
 * 输出：
 *   - 返回已观察局部坐标列表。
 * 关键逻辑：
 *   - 候选目标、动态 alpha 和估计嵌入都只基于这些记忆格。
 */
std::vector<Position> LocalKnownMap::observedPositions() const
{
    std::vector<Position> positions;
    for (const auto &[pos, cell] : cells_) {
        if (cell.observed) positions.push_back(pos);
    }
    return positions;
}

/**
 * 功能：列出当前已知且未拾取的金币。
 * 输入：
 *   - 无。
 * 输出：
 *   - 返回金币局部坐标列表。
 * 关键逻辑：
 *   - 只包含已观察金币，避免后续金币机会偷看未知区域。
 */
std::vector<Position> LocalKnownMap::knownCoins() const
{
    std::vector<Position> coins;
    for (const auto &[pos, cell] : cells_) {
        if (cell.observed && cell.tile == "G" && !cell.collected) coins.push_back(pos);
    }
    return coins;
}

/**
 * 功能：统计当前已知 Boss 触发区数量。
 * 输入：
 *   - 无。
 * 输出：
 *   - 返回触发区格子数量。
 * 关键逻辑：
 *   - 动态 alpha 用该数量估计未知区域风险，优先统计触发区而不是 Boss 本体。
 */
int LocalKnownMap::knownBossTriggerCount() const
{
    int count = 0;
    for (const auto &[pos, cell] : cells_) {
        (void)pos;
        if (cell.bossTrigger) ++count;
    }
    return count;
}

/**
 * 功能：构造入口位置估计器并初始化候选嵌入。
 * 输入：
 *   - 无。
 * 输出：
 *   - 创建包含多入口假设的估计器。
 * 关键逻辑：
 *   - 不使用真实入口坐标，只枚举四条边界中心附近的估计入口。
 */
MapPoseEstimator::MapPoseEstimator()
{
    initialize();
}

/**
 * 功能：初始化四条边界的多入口候选。
 * 输入：
 *   - 无。
 * 输出：
 *   - 重置候选集合和默认最佳候选。
 * 关键逻辑：
 *   - 每条边界取全部 15 个位置，避免真实入口不在中央附近时所有候选被错误判为不可行。
 */
void MapPoseEstimator::initialize()
{
    hypotheses_.clear();
    for (int offset = 0; offset < kEstimatedSize; ++offset) {
        hypotheses_.push_back({{0, offset}, Direction::Down, 0.0, true});
        hypotheses_.push_back({{14, offset}, Direction::Up, 0.0, true});
        hypotheses_.push_back({{offset, 0}, Direction::Right, 0.0, true});
        hypotheses_.push_back({{offset, 14}, Direction::Left, 0.0, true});
    }
    best_ = hypotheses_[0];
    observedEstimated_.clear();
}

/**
 * 功能：用当前局部记忆更新最佳 15x15 估计嵌入。
 * 输入：
 *   - localMap：AI 已观察到的局部记忆地图。
 *   - localCurrent：AI 当前局部坐标。
 * 输出：
 *   - 无返回值，更新最佳候选和估计已观察格集合。
 * 关键逻辑：
 *   - 候选只根据局部已知形状评分；任何已观察格越界的候选直接不可行。
 */
void MapPoseEstimator::update(const LocalKnownMap &localMap, Position localCurrent)
{
    const auto observed = localMap.observedPositions();
    double bestScore = -1e18;
    MapEmbeddingHypothesis fallback = hypotheses_.empty() ? MapEmbeddingHypothesis{} : hypotheses_[0];

    for (auto &hypothesis : hypotheses_) {
        hypothesis.feasible = true;
        hypothesis.score = 0.0;
        std::set<Position> estimated;
        for (const auto &pos : observed) {
            const Position mapped = mapWithHypothesis(pos, hypothesis);
            if (!insideEstimated(mapped)) {
                hypothesis.feasible = false;
                hypothesis.score = -1e18;
                break;
            }
            estimated.insert(mapped);
        }
        if (!hypothesis.feasible) continue;

        const Position currentEstimated = mapWithHypothesis(localCurrent, hypothesis);
        int edgeTouches = 0;
        for (const auto &pos : estimated) {
            if (pos.first == 0 || pos.first == kEstimatedSize - 1) ++edgeTouches;
            if (pos.second == 0 || pos.second == kEstimatedSize - 1) ++edgeTouches;
        }
        const double centerDistance = std::abs(currentEstimated.first - 7) + std::abs(currentEstimated.second - 7);
        hypothesis.score = -0.15 * edgeTouches - 0.05 * centerDistance;
        if (hypothesis.score > bestScore) {
            bestScore = hypothesis.score;
            best_ = hypothesis;
            observedEstimated_ = std::move(estimated);
        }
    }

    if (bestScore <= -1e17) {
        best_ = fallback;
        observedEstimated_.clear();
    }
}

/**
 * 功能：返回当前最佳入口嵌入假设。
 * 输入：
 *   - 无。
 * 输出：
 *   - 返回最高分且可行的估计嵌入。
 * 关键逻辑：
 *   - 该结果是 AI 的估计坐标，不是真实全局坐标。
 */
MapEmbeddingHypothesis MapPoseEstimator::best() const
{
    return best_;
}

/**
 * 功能：把局部坐标映射到估计 15x15 坐标。
 * 输入：
 *   - localPos：以入口为原点的局部坐标。
 * 输出：
 *   - 返回估计全局坐标。
 * 关键逻辑：
 *   - 使用当前最佳假设旋转和平移局部坐标，不能读取真实迷宫坐标。
 */
Position MapPoseEstimator::localToEstimatedGlobal(Position localPos) const
{
    return mapWithHypothesis(localPos, best_);
}

/**
 * 功能：判断局部坐标是否落在估计迷宫内部。
 * 输入：
 *   - localPos：局部坐标。
 * 输出：
 *   - 返回映射后是否位于 15x15 范围内。
 * 关键逻辑：
 *   - 迷宫外部不能当作未知区域产生探索价值。
 */
bool MapPoseEstimator::isInsideEstimatedMaze(Position localPos) const
{
    return insideEstimated(localToEstimatedGlobal(localPos));
}

/**
 * 功能：计算估计 15x15 中仍未知的格子数量。
 * 输入：
 *   - 无。
 * 输出：
 *   - 返回 225 减去估计已观察格数量。
 * 关键逻辑：
 *   - 只统计估计迷宫内部，避免把迷宫外部误当未知奖励。
 */
int MapPoseEstimator::estimatedUnknownCount() const
{
    return kEstimatedSize * kEstimatedSize - estimatedObservedCount();
}

/**
 * 功能：计算估计 15x15 中已观察的格子数量。
 * 输入：
 *   - 无。
 * 输出：
 *   - 返回映射后的已观察格去重数量。
 * 关键逻辑：
 *   - 用于停止探索规则中的观察比例。
 */
int MapPoseEstimator::estimatedObservedCount() const
{
    return static_cast<int>(observedEstimated_.size());
}

/**
 * 功能：统计与目标 3x3 视野接触的未知连通块大小。
 * 输入：
 *   - localTarget：候选目标的局部坐标。
 * 输出：
 *   - 返回接触到的每个未知连通块大小。
 * 关键逻辑：
 *   - 在估计 15x15 网格中只按 observed=false 做连通块，不读取真实迷宫内容。
 */
std::vector<int> MapPoseEstimator::unknownComponentSizesTouchingView(Position localTarget) const
{
    bool observed[15][15]{};
    for (const auto &pos : observedEstimated_) {
        if (insideEstimated(pos)) observed[pos.first][pos.second] = true;
    }

    int componentId[15][15];
    std::fill(&componentId[0][0], &componentId[0][0] + 225, -1);
    std::vector<int> componentSizes;
    int nextId = 0;
    for (int row = 0; row < 15; ++row) {
        for (int col = 0; col < 15; ++col) {
            if (observed[row][col] || componentId[row][col] != -1) continue;
            std::queue<Position> queue;
            queue.push({row, col});
            componentId[row][col] = nextId;
            int size = 0;
            while (!queue.empty()) {
                const auto [r, c] = queue.front();
                queue.pop();
                ++size;
                for (const auto [dr, dc] : kDirs) {
                    const Position next{r + dr, c + dc};
                    if (!insideEstimated(next) || observed[next.first][next.second] ||
                        componentId[next.first][next.second] != -1) {
                        continue;
                    }
                    componentId[next.first][next.second] = nextId;
                    queue.push(next);
                }
            }
            componentSizes.push_back(size);
            ++nextId;
        }
    }

    std::set<int> touching;
    const Position center = localToEstimatedGlobal(localTarget);
    for (int row = center.first - 1; row <= center.first + 1; ++row) {
        for (int col = center.second - 1; col <= center.second + 1; ++col) {
            if (!insideEstimated({row, col})) continue;
            if (!observed[row][col] && componentId[row][col] >= 0) {
                touching.insert(componentId[row][col]);
            }
            for (const auto [dr, dc] : kDirs) {
                const Position next{row + dr, col + dc};
                if (insideEstimated(next) && !observed[next.first][next.second] &&
                    componentId[next.first][next.second] >= 0) {
                    touching.insert(componentId[next.first][next.second]);
                }
            }
        }
    }

    std::vector<int> sizes;
    for (const int id : touching) {
        sizes.push_back(componentSizes[id]);
    }
    return sizes;
}

/**
 * 功能：按指定候选嵌入映射局部坐标。
 * 输入：
 *   - localPos：局部坐标。
 *   - hypothesis：入口和朝向假设。
 * 输出：
 *   - 返回估计 15x15 坐标。
 * 关键逻辑：
 *   - 不依赖真实全局坐标，只根据入口假设对局部坐标旋转和平移。
 */
Position MapPoseEstimator::mapWithHypothesis(Position localPos, const MapEmbeddingHypothesis &hypothesis) const
{
    switch (hypothesis.inwardDirection) {
    case Direction::Down:
        return {hypothesis.entry.first + localPos.first, hypothesis.entry.second + localPos.second};
    case Direction::Up:
        return {hypothesis.entry.first - localPos.first, hypothesis.entry.second + localPos.second};
    case Direction::Right:
        return {hypothesis.entry.first + localPos.second, hypothesis.entry.second + localPos.first};
    case Direction::Left:
        return {hypothesis.entry.first + localPos.second, hypothesis.entry.second - localPos.first};
    }
    return hypothesis.entry;
}

/**
 * 功能：创建路径价值评估器。
 * 输入：
 *   - parameters：各算法可独立配置的 reward 参数。
 * 输出：
 *   - 构造可复用评分组件。
 * 关键逻辑：
 *   - 参数不写死在贪心策略中，方便后续其它算法复用不同权重。
 */
PathValueEvaluator::PathValueEvaluator(RewardParameters parameters) : parameters_(parameters) {}

/**
 * 功能：读取当前 reward 参数。
 * 输入：
 *   - 无。
 * 输出：
 *   - 返回参数结构体引用。
 * 关键逻辑：
 *   - 贪心策略读取停止探索阈值和目标保持阈值时使用同一套参数。
 */
const RewardParameters &PathValueEvaluator::parameters() const
{
    return parameters_;
}

/**
 * 功能：计算路径真实资源变化。
 * 输入：
 *   - path：局部坐标路径，path[0] 是当前位置。
 *   - localMap：局部记忆地图。
 * 输出：
 *   - 返回金币和陷阱一次性结算后的资源增量。
 * 关键逻辑：
 *   - 跳过 path[0]，避免重复结算当前站立格；已拾取金币和已触发陷阱都计 0。
 */
int PathValueEvaluator::pathResourceDelta(const std::vector<Position> &path, const LocalKnownMap &localMap) const
{
    int delta = 0;
    for (size_t i = 1; i < path.size(); ++i) {
        const Position pos = path[i];
        const std::string tile = localMap.tile(pos);
        if (tile == "G" && !localMap.isCollected(pos)) delta += kGoldValue;
        if (tile == "T" && !localMap.isTriggered(pos)) delta += kTrapValue;
    }
    return delta;
}

/**
 * 功能：计算探索信息价值 I_proxy。
 * 输入：
 *   - target：候选目标局部坐标。
 *   - localMap：局部记忆地图。
 *   - poseEstimator：当前估计 15x15 嵌入。
 * 输出：
 *   - 返回新视野数量和按当前价值密度估计后的未知连通块贡献之和。
 * 关键逻辑：
 *   - N_new 只看目标 3x3 中尚未观察且位于估计迷宫内的格子。
 *   - 未知连通块贡献使用 min(|C|, Amax) * rho_area_value，避免只按面积奖励危险未知区。
 */
double PathValueEvaluator::informationProxy(Position target, const LocalKnownMap &localMap,
                                            const MapPoseEstimator &poseEstimator) const
{
    int newVisible = 0;
    for (int row = target.first - 1; row <= target.first + 1; ++row) {
        for (int col = target.second - 1; col <= target.second + 1; ++col) {
            const Position pos{row, col};
            if (poseEstimator.isInsideEstimatedMaze(pos) && !localMap.isObserved(pos)) ++newVisible;
        }
    }

    int observedCount = 0;
    int goldCount = 0;
    int trapCount = 0;
    for (const auto &pos : localMap.observedPositions()) {
        ++observedCount;
        const std::string tile = localMap.tile(pos);
        if (tile == "G") ++goldCount;
        if (tile == "T") ++trapCount;
    }
    const double denominator = observedCount + parameters_.lambda;
    const double rhoG = (goldCount + parameters_.lambdaG) / denominator;
    const double rhoT = (trapCount + parameters_.lambdaT) / denominator;
    const double rhoB = (localMap.knownBossTriggerCount() + parameters_.lambdaB) / denominator;
    const double areaValue = 50.0 * rhoG - 30.0 * rhoT - parameters_.kappaB * rhoB;
    const double areaValueDensity = std::clamp(std::max(areaValue, 0.0) / 50.0, 0.0, 1.0);

    double componentValue = 0.0;
    for (const int size : poseEstimator.unknownComponentSizesTouchingView(target)) {
        componentValue += static_cast<double>(std::min(size, parameters_.areaMax)) * areaValueDensity;
    }
    return newVisible + parameters_.kappaU * componentValue;
}

/**
 * 功能：计算目标后的边际金币机会上界。
 * 输入：
 *   - target：候选目标局部坐标。
 *   - path：到候选目标的当前路径。
 *   - localMap：局部记忆地图。
 * 输出：
 *   - 返回 max 50/(dist+1)，没有可达金币时返回 0。
 * 关键逻辑：
 *   - 路径上已经会拾取的金币被排除，避免和真实路径收益重复计入；该值只作为 tail upper bound。
 */
double PathValueEvaluator::futureGainMarginal(Position target, const std::vector<Position> &path,
                                              const LocalKnownMap &localMap) const
{
    std::set<Position> coinsOnPath;
    for (size_t i = 1; i < path.size(); ++i) {
        if (localMap.tile(path[i]) == "G" && !localMap.isCollected(path[i])) coinsOnPath.insert(path[i]);
    }

    double best = 0.0;
    for (const auto &coin : localMap.knownCoins()) {
        if (coinsOnPath.find(coin) != coinsOnPath.end()) continue;
        const auto coinPath = shortestPathOnKnownMap(target, coin, localMap);
        if (coinPath.empty()) continue;
        best = std::max(best, static_cast<double>(kGoldValue) / (pathLength(coinPath) + 1.0));
    }
    return best;
}

/**
 * 功能：计算路径长度机会成本系数 q_eff。
 * 输入：
 *   - context：当前资源、步数和可选出口路径。
 *   - localMap：局部记忆地图。
 * 输出：
 *   - 返回 max(q_ref, q_min)。
 * 关键逻辑：
 *   - 出口已知可达时用出口路径估计 q_ref，否则用当前 R/L；q_min 防止开局长度成本消失。
 */
double PathValueEvaluator::computeQEff(const PathValueContext &context, const LocalKnownMap &localMap) const
{
    double qRef = static_cast<double>(context.state.resource) / (context.state.steps + parameters_.epsilon);
    if (!context.exitPath.empty()) {
        const int exitDelta = pathResourceDelta(context.exitPath, localMap);
        qRef = static_cast<double>(context.state.resource + exitDelta) /
               (context.state.steps + pathLength(context.exitPath) + parameters_.epsilon);
    }
    return std::max(qRef, parameters_.qMin);
}

/**
 * 功能：计算低资源安全 barrier。
 * 输入：
 *   - projectedResource：走完候选路径后的资源。
 * 输出：
 *   - 返回平滑安全惩罚。
 * 关键逻辑：
 *   - 资源低于安全线时按平方惩罚，避免固定大惩罚把正常探索路径全部压成极低分。
 */
double PathValueEvaluator::marginPenalty(int projectedResource) const
{
    if (projectedResource >= parameters_.safeResource) return 0.0;
    const double margin = static_cast<double>(parameters_.safeResource - projectedResource) / parameters_.safeResource;
    return parameters_.lambdaMargin * margin * margin;
}

/**
 * 功能：根据当前观察统计更新平滑动态 alpha。
 * 输入：
 *   - previousAlpha：上一步使用的平滑 alpha。
 *   - localMap：局部记忆地图。
 *   - poseEstimator：估计嵌入，用于未知比例。
 * 输出：
 *   - 返回新的 alpha_t_smooth。
 * 关键逻辑：
 *   - 金币提高探索价值，陷阱和 Boss 触发区降低探索价值，再用 theta 做指数平滑。
 */
double PathValueEvaluator::updateAlphaSmooth(double previousAlpha, const LocalKnownMap &localMap,
                                             const MapPoseEstimator &poseEstimator) const
{
    int observedCount = 0;
    int goldCount = 0;
    int trapCount = 0;
    for (const auto &pos : localMap.observedPositions()) {
        ++observedCount;
        const std::string tile = localMap.tile(pos);
        if (tile == "G") ++goldCount;
        if (tile == "T") ++trapCount;
    }

    const double denominator = observedCount + parameters_.lambda;
    const double rhoG = (goldCount + parameters_.lambdaG) / denominator;
    const double rhoT = (trapCount + parameters_.lambdaT) / denominator;
    const double rhoB = (localMap.knownBossTriggerCount() + parameters_.lambdaB) / denominator;
    const double rhoU = static_cast<double>(poseEstimator.estimatedUnknownCount()) / 225.0;
    const double unknownValue = 50.0 * rhoG - 30.0 * rhoT - parameters_.kappaB * rhoB;

    const double raw = parameters_.alpha0 *
                       (1.0 + parameters_.wU * rhoU + parameters_.wV * std::max(unknownValue, 0.0) / 50.0) /
                       (1.0 + parameters_.wR * (rhoT + parameters_.cB * rhoB));
    const double clipped = std::clamp(raw, parameters_.alphaMin, parameters_.alphaMax);
    return parameters_.theta * previousAlpha + (1.0 - parameters_.theta) * clipped;
}

/**
 * 功能：计算候选路径的主评分。
 * 输入：
 *   - path：当前位置到目标的局部路径。
 *   - target：候选目标局部坐标。
 *   - context：当前资源、步数、alpha 和出口路径。
 *   - localMap：局部记忆地图。
 *   - poseEstimator：估计嵌入。
 * 输出：
 *   - 返回加性 reward 分数，非法路径返回负无穷。
 * 关键逻辑：
 *   - 若 DeltaR、I_proxy、tailUB 都为 0，说明目标没有任何收益来源，直接判为负无穷。
 *   - 其余情况使用 DeltaR + omegaI*alpha*I + beta*tailUB - qEff*len - margin，不加入最近访问惩罚。
 */
double PathValueEvaluator::evaluate(const std::vector<Position> &path, Position target,
                                    const PathValueContext &context, const LocalKnownMap &localMap,
                                    const MapPoseEstimator &poseEstimator) const
{
    if (path.size() <= 1) return -1e18;
    for (const auto &pos : path) {
        if (!localMap.isWalkableForPlanning(pos)) return -1e18;
    }

    const int delta = pathResourceDelta(path, localMap);
    const int projectedResource = context.state.resource + delta;
    if (projectedResource < 0) return -1e18;

    const double info = informationProxy(target, localMap, poseEstimator);
    const double tail = futureGainMarginal(target, path, localMap);
    if (delta == 0 && info == 0.0 && tail == 0.0) return -1e18;
    const double qEff = computeQEff(context, localMap);
    const double margin = marginPenalty(projectedResource);
    return delta + parameters_.omegaI * context.state.alphaSmooth * info + parameters_.beta * tail -
           qEff * pathLength(path) - margin;
}

/**
 * 功能：在局部已知地图上计算最短路径。
 * 输入：
 *   - start：局部起点。
 *   - target：局部目标。
 *   - localMap：局部记忆地图。
 * 输出：
 *   - 返回只经过已观察可通行格子的路径；不可达时返回空路径。
 * 关键逻辑：
 *   - BFS 不允许穿过未知格、墙或 Boss 本体；Boss 正邻接触发区允许进入并由主流程触发战斗。
 */
std::vector<Position> PathValueEvaluator::shortestPathOnKnownMap(Position start, Position target,
                                                                 const LocalKnownMap &localMap) const
{
    if (!localMap.isWalkableForPlanning(start) || !localMap.isWalkableForPlanning(target)) return {};

    std::map<Position, Position> parent;
    std::set<Position> visited;
    std::queue<Position> queue;
    queue.push(start);
    visited.insert(start);
    parent[start] = kInvalid;

    while (!queue.empty()) {
        const Position current = queue.front();
        queue.pop();
        if (current == target) break;
        for (const auto [dr, dc] : kDirs) {
            const Position next{current.first + dr, current.second + dc};
            if (visited.find(next) != visited.end() || !localMap.isWalkableForPlanning(next)) continue;
            visited.insert(next);
            parent[next] = current;
            queue.push(next);
        }
    }

    if (visited.find(target) == visited.end()) return {};
    std::vector<Position> path;
    for (Position pos = target; pos != kInvalid; pos = parent[pos]) {
        path.push_back(pos);
    }
    std::reverse(path.begin(), path.end());
    return path;
}

} // namespace ai_player
