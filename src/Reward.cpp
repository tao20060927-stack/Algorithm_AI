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
 *   - localPos：以起始位置为局部原点的坐标。
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
    cell.outside = false;
    cell.tile = tile;
}

/**
 * 功能：记录 3x3 视野中落在迷宫外部的局部格。
 * 输入：
 *   - localPos：以起始位置为局部原点的越界视野坐标。
 * 输出：
 *   - 无返回值，更新局部记忆地图中的 outside 标记。
 * 关键逻辑：
 *   - outside 不是普通墙；它表示 AI 通过局部视野确认该坐标在迷宫外，用于判断出生点是否在边缘。
 */
void LocalKnownMap::setOutside(Position localPos)
{
    auto &cell = cells_[localPos];
    cell.observed = true;
    cell.outside = true;
    cell.tile = "#";
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
 * 功能：判断局部格子是否被观察为迷宫外部。
 * 输入：
 *   - localPos：局部坐标。
 * 输出：
 *   - 返回该坐标是否为 outside。
 * 关键逻辑：
 *   - outside 只用于出生掩码判断和路径外部约束，不作为可通行格参与搜索。
 */
bool LocalKnownMap::isOutside(Position localPos) const
{
    const auto it = cells_.find(localPos);
    return it != cells_.end() && it->second.outside;
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
 *   - 触发区可通行，保留该标记用于表达 Boss 正邻接格语义，不再参与 reward 风险计算。
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
 *   - 只禁止墙；Boss 本体按普通已观察格参与规划和价值计算。
 */
bool LocalKnownMap::isWalkableForPlanning(Position localPos) const
{
    const auto it = cells_.find(localPos);
    if (it == cells_.end() || !it->second.observed) return false;
    if (it->second.outside) return false;
    return it->second.tile != "#";
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
        if (cell.observed && !cell.outside) positions.push_back(pos);
    }
    return positions;
}

/**
 * 功能：列出所有已确认的迷宫外部局部坐标。
 * 输入：
 *   - 无。
 * 输出：
 *   - 返回 outside 坐标列表。
 * 关键逻辑：
 *   - MapPoseEstimator 只在出生点 3x3 中使用这些坐标判断初始边缘假设。
 */
std::vector<Position> LocalKnownMap::outsidePositions() const
{
    std::vector<Position> positions;
    for (const auto &[pos, cell] : cells_) {
        if (cell.outside) positions.push_back(pos);
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
 * 功能：构造局部姿态估计器并初始化候选嵌入。
 * 输入：
 *   - 无。
 * 输出：
 *   - 创建包含多原点假设的估计器。
 * 关键逻辑：
 *   - 不使用真实起点坐标；先保持掩码未启用，等局部记忆足以确定 15x15 掩码时再裁剪未知区域。
 */
MapPoseEstimator::MapPoseEstimator()
{
    initialize();
}

/**
 * 功能：初始化局部原点的多位置候选。
 * 输入：
 *   - 无。
 * 输出：
 *   - 重置候选集合和默认最佳候选。
 * 关键逻辑：
 *   - 默认把局部原点放在估计中心但不启用掩码；真正裁剪必须等 update() 判定 maskActive_。
 */
void MapPoseEstimator::initialize()
{
    hypotheses_.clear();
    hypotheses_.push_back({{kEstimatedSize / 2, kEstimatedSize / 2}, Direction::Down, 0.0, true});
    best_ = hypotheses_[0];
    observedEstimated_.clear();
    maskSeeded_ = false;
    maskActive_ = false;
    seedKind_ = MaskSeedKind::Internal;
}

/**
 * 功能：用当前局部记忆更新最佳 15x15 估计嵌入。
 * 输入：
 *   - localMap：AI 已观察到的局部记忆地图。
 *   - localCurrent：AI 当前局部坐标。
 * 输出：
 *   - 无返回值，更新最佳候选和估计已观察格集合。
 * 关键逻辑：
 *   - 第一次更新根据出生点 3x3 的迷宫外部信息确定出生边缘假设。
 *   - 只有记忆地图跨度足以完全确定 15x15 掩码时，才启用掩码裁剪；否则保持无掩码 BFS。
 */
void MapPoseEstimator::update(const LocalKnownMap &localMap, Position localCurrent)
{
    const auto observed = localMap.observedPositions();
    if (!maskSeeded_) {
        const auto outside = localMap.outsidePositions();
        const auto hasOutside = [&](Position pos) {
            return std::find(outside.begin(), outside.end(), pos) != outside.end();
        };
        const auto addHypothesis = [&](Position entry) {
            hypotheses_.push_back({entry, Direction::Down, 0.0, true});
        };
        const bool topOutside = hasOutside({-1, -1}) && hasOutside({-1, 0}) && hasOutside({-1, 1});
        const bool bottomOutside = hasOutside({1, -1}) && hasOutside({1, 0}) && hasOutside({1, 1});
        const bool leftOutside = hasOutside({-1, -1}) && hasOutside({0, -1}) && hasOutside({1, -1});
        const bool rightOutside = hasOutside({-1, 1}) && hasOutside({0, 1}) && hasOutside({1, 1});

        hypotheses_.clear();
        if (topOutside && leftOutside) {
            seedKind_ = MaskSeedKind::TopLeft;
            addHypothesis({0, 0});
        } else if (topOutside && rightOutside) {
            seedKind_ = MaskSeedKind::TopRight;
            addHypothesis({0, kEstimatedSize - 1});
        } else if (bottomOutside && leftOutside) {
            seedKind_ = MaskSeedKind::BottomLeft;
            addHypothesis({kEstimatedSize - 1, 0});
        } else if (bottomOutside && rightOutside) {
            seedKind_ = MaskSeedKind::BottomRight;
            addHypothesis({kEstimatedSize - 1, kEstimatedSize - 1});
        } else if (topOutside) {
            seedKind_ = MaskSeedKind::Top;
            for (int col = 1; col < kEstimatedSize - 1; ++col) addHypothesis({0, col});
        } else if (bottomOutside) {
            seedKind_ = MaskSeedKind::Bottom;
            for (int col = 1; col < kEstimatedSize - 1; ++col) addHypothesis({kEstimatedSize - 1, col});
        } else if (leftOutside) {
            seedKind_ = MaskSeedKind::Left;
            for (int row = 1; row < kEstimatedSize - 1; ++row) addHypothesis({row, 0});
        } else if (rightOutside) {
            seedKind_ = MaskSeedKind::Right;
            for (int row = 1; row < kEstimatedSize - 1; ++row) addHypothesis({row, kEstimatedSize - 1});
        } else {
            addHypothesis({kEstimatedSize / 2, kEstimatedSize / 2});
        }
        best_ = hypotheses_.front();
        maskSeeded_ = true;
    }

    int minRow = 0;
    int maxRow = 0;
    int minCol = 0;
    int maxCol = 0;
    if (!observed.empty()) {
        minRow = maxRow = observed.front().first;
        minCol = maxCol = observed.front().second;
        for (const auto &pos : observed) {
            minRow = std::min(minRow, pos.first);
            maxRow = std::max(maxRow, pos.first);
            minCol = std::min(minCol, pos.second);
            maxCol = std::max(maxCol, pos.second);
        }
    }

    const bool hasFullHorizontal = maxCol - minCol + 1 >= kEstimatedSize;
    const bool hasFullVertical = maxRow - minRow + 1 >= kEstimatedSize;
    const bool canPruneHorizontal = maxCol - minCol + 1 >= kEstimatedSize - 1;
    const bool canPruneVertical = maxRow - minRow + 1 >= kEstimatedSize - 1;
    const bool canPruneHypotheses =
        ((seedKind_ == MaskSeedKind::Top || seedKind_ == MaskSeedKind::Bottom) && canPruneHorizontal) ||
        ((seedKind_ == MaskSeedKind::Left || seedKind_ == MaskSeedKind::Right) && canPruneVertical);
    if (canPruneHypotheses) {
        std::vector<MapEmbeddingHypothesis> feasibleHypotheses;
        for (auto hypothesis : hypotheses_) {
            bool feasible = true;
            for (const auto &pos : observed) {
                const Position mapped = mapWithHypothesis(pos, hypothesis);
                if (!insideEstimated(mapped)) {
                    feasible = false;
                    break;
                }
                const bool boundary = mapped.first == 0 || mapped.second == 0 || mapped.first == kEstimatedSize - 1 ||
                                      mapped.second == kEstimatedSize - 1;
                const std::string tile = localMap.tile(pos);
                const bool allowedEntry = pos == Position{0, 0} && tile == "S";
                if (boundary && tile != "#" && !allowedEntry) {
                    feasible = false;
                    break;
                }
            }
            if (!feasible) continue;
            for (const auto &pos : localMap.outsidePositions()) {
                if (insideEstimated(mapWithHypothesis(pos, hypothesis))) {
                    feasible = false;
                    break;
                }
            }
            if (feasible) feasibleHypotheses.push_back(hypothesis);
        }
        if (!feasibleHypotheses.empty()) {
            hypotheses_ = feasibleHypotheses;
            best_ = hypotheses_.front();
        }
    }

    switch (seedKind_) {
    case MaskSeedKind::Top:
    case MaskSeedKind::Bottom:
        maskActive_ = hypotheses_.size() == 1 || hasFullHorizontal;
        if (hasFullHorizontal) best_.entry.second = -minCol;
        break;
    case MaskSeedKind::Left:
    case MaskSeedKind::Right:
        maskActive_ = hypotheses_.size() == 1 || hasFullVertical;
        if (hasFullVertical) best_.entry.first = -minRow;
        break;
    case MaskSeedKind::TopLeft:
    case MaskSeedKind::TopRight:
    case MaskSeedKind::BottomLeft:
    case MaskSeedKind::BottomRight:
        maskActive_ = true;
        break;
    case MaskSeedKind::Internal:
        maskActive_ = (maxCol - minCol + 1 >= kEstimatedSize) && (maxRow - minRow + 1 >= kEstimatedSize);
        if (maskActive_) best_.entry = {-minRow, -minCol};
        break;
    }

    observedEstimated_.clear();
    for (const auto &pos : observed) {
        const Position mapped = mapWithHypothesis(pos, best_);
        if (maskActive_ && !insideEstimated(mapped)) {
            maskActive_ = false;
            observedEstimated_.clear();
            break;
        }
        if (insideEstimated(mapped)) observedEstimated_.insert(mapped);
    }
}

/**
 * 功能：返回当前最佳原点嵌入假设。
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
 * 功能：判断当前是否已经启用 15x15 掩码。
 * 输入：
 *   - 无。
 * 输出：
 *   - 返回掩码是否参与未知区域裁剪。
 * 关键逻辑：
 *   - 只有出生边缘和记忆地图跨度满足可确定条件时，该值才为 true。
 */
bool MapPoseEstimator::isMaskActive() const
{
    return maskActive_;
}

/**
 * 功能：把局部坐标映射到估计 15x15 坐标。
 * 输入：
 *   - localPos：以起始位置为局部原点的坐标。
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
    if (!maskActive_) return true;
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
 * 功能：统计目标格能够展开到的未知区域大小。
 * 输入：
 *   - localTarget：候选目标的局部坐标。
 *   - localMap：AI 当前局部记忆地图，已观察格会作为 BFS 障碍。
 *   - areaMax：未知区域计数上限，达到该值即可停止搜索。
 * 输出：
 *   - 返回一个元素，表示从目标格出发最多能 BFS 展开的未观察格数量，范围为 [0, areaMax]。
 * 关键逻辑：
 *   - 以目标格为起点，已观察格视为不可逾越障碍，未知格可以继续扩展。
 *   - 如果能搜索到 areaMax 个未知格，就直接按 areaMax 计；如果封闭且不足 areaMax，则按实际搜索到的数量计。
 *   - 掩码未启用时不裁剪；掩码启用后，15x15 外围一圈按迷宫边界墙处理，不计入未知区域。
 */
std::vector<int> MapPoseEstimator::unknownComponentSizesTouchingView(Position localTarget, const LocalKnownMap &localMap,
                                                                     int areaMax) const
{
    if (areaMax <= 0) return {0};
    if (maskActive_ && !isInsideEstimatedMaze(localTarget)) return {0};

    const auto observedPositions = localMap.observedPositions();
    std::set<Position> observed(observedPositions.begin(), observedPositions.end());
    const auto blockedByMaskBoundary = [&](Position pos) {
        const Position mapped = localToEstimatedGlobal(pos);
        if (maskActive_) {
            if (!insideEstimated(mapped)) return true;
            return mapped.first == 0 || mapped.second == 0 || mapped.first == kEstimatedSize - 1 ||
                   mapped.second == kEstimatedSize - 1;
        }
        switch (seedKind_) {
        case MaskSeedKind::Top:
        case MaskSeedKind::Bottom:
            return mapped.first <= 0 || mapped.first >= kEstimatedSize - 1;
        case MaskSeedKind::Left:
        case MaskSeedKind::Right:
            return mapped.second <= 0 || mapped.second >= kEstimatedSize - 1;
        default:
            return false;
        }
    };
    if (blockedByMaskBoundary(localTarget)) return {0};
    std::set<Position> visited;
    std::queue<Position> queue;
    queue.push(localTarget);
    visited.insert(localTarget);
    int expandableCount = 0;
    while (!queue.empty()) {
        const auto [row, col] = queue.front();
        queue.pop();
        if (!observed.count({row, col})) {
            ++expandableCount;
            if (expandableCount >= areaMax) return {areaMax};
        }
        for (const auto [dr, dc] : kDirs) {
            const Position next{row + dr, col + dc};
            if (visited.count(next) || observed.count(next) || blockedByMaskBoundary(next)) {
                continue;
            }
            visited.insert(next);
            queue.push(next);
        }
    }
    return {expandableCount};
}

/**
 * 功能：按指定候选嵌入映射局部坐标。
 * 输入：
 *   - localPos：局部坐标。
 *   - hypothesis：局部原点和朝向假设。
 * 输出：
 *   - 返回估计 15x15 坐标。
 * 关键逻辑：
 *   - 不依赖真实全局坐标，只根据原点假设对局部坐标旋转和平移。
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
 * 功能：检查路径执行过程中资源是否始终不为负。
 * 输入：
 *   - path：局部坐标路径，path[0] 是当前位置。
 *   - currentResource：执行路径前 AI 当前资源。
 *   - localMap：局部记忆地图。
 * 输出：
 *   - 返回路径每一个前缀结算后的资源是否都 >= 0。
 * 关键逻辑：
 *   - 逐步结算金币和陷阱，而不是只看整条路径的总 DeltaR；这可以禁止“先踩陷阱到负数，再吃金币补回来”的路径。
 */
bool PathValueEvaluator::pathKeepsResourceNonNegative(const std::vector<Position> &path, int currentResource,
                                                      const LocalKnownMap &localMap) const
{
    int resource = currentResource;
    for (size_t i = 1; i < path.size(); ++i) {
        const Position pos = path[i];
        const std::string tile = localMap.tile(pos);
        if (tile == "G" && !localMap.isCollected(pos)) resource += kGoldValue;
        if (tile == "T" && !localMap.isTriggered(pos)) resource += kTrapValue;
        if (resource < 0) return false;
    }
    return true;
}

/**
 * 功能：计算探索信息价值 I_proxy。
 * 输入：
 *   - target：候选目标局部坐标。
 *   - localMap：局部记忆地图。
 *   - poseEstimator：姿态估计器；只有掩码已完全确定时才用它裁剪 |C|。
 *   - areaCap：|C| 面积奖励上限；出口未知时通常为 Amax，出口已知后可降到 1.5。
 *   - forceAreaMax：是否把该目标的 |C| 强制按 Amax 处理。
 * 输出：
 *   - 返回按当前价值密度估计后的未知连通块贡献。
 * 关键逻辑：
 *   - |C| 由局部记忆地图 BFS 得到；掩码未启用时不裁剪，掩码启用后不允许 BFS 展开到掩码外部。
 *   - 对必须踏过 Boss 才能继续到达的通关推进区域，直接按 Amax 计入，避免边缘出口被面积裁剪误判为无价值。
 */
double PathValueEvaluator::informationProxy(Position target, const LocalKnownMap &localMap,
                                            const MapPoseEstimator &poseEstimator, double areaCap,
                                            bool forceAreaMax) const
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
    const double areaValue = 50.0 * rhoG - 30.0 * rhoT;
    const double areaValueDensity = std::clamp(std::max(areaValue, 0.0) / 50.0, parameters_.rhoAreaValueMin, 1.0);

    const double activeAreaCap = areaCap > 0.0 ? areaCap : static_cast<double>(parameters_.areaMax);
    const int searchAreaCap = std::max(1, static_cast<int>(std::ceil(activeAreaCap)));
    double componentValue = 0.0;
    if (forceAreaMax) {
        componentValue = static_cast<double>(parameters_.areaMax) * areaValueDensity;
    } else {
        for (const int size : poseEstimator.unknownComponentSizesTouchingView(target, localMap, searchAreaCap)) {
            componentValue += std::min(static_cast<double>(size), activeAreaCap) * areaValueDensity;
        }
    }
    return parameters_.kappaU * componentValue;
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
 *   - 金币提高探索价值，陷阱降低探索价值；Boss 只作为战斗事件处理，不再作为风险密度参与 alpha。
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
    const double rhoU = static_cast<double>(poseEstimator.estimatedUnknownCount()) / 225.0;
    const double unknownValue = 50.0 * rhoG - 30.0 * rhoT;

    const double raw = parameters_.alpha0 *
                       (1.0 + parameters_.wU * rhoU + parameters_.wV * std::max(unknownValue, 0.0) / 50.0) /
                       (1.0 + parameters_.wR * rhoT);
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
 *   - 若路径任意前缀会让资源变负，说明 AI 实际执行时会进入非法状态，直接判为负无穷。
 *   - 若 DeltaR、I_proxy、tailUB 都为 0，说明目标没有任何收益来源，直接判为负无穷。
 *   - 其余情况使用 DeltaR + omegaI*alpha*I + beta*tailUB - qEffLengthWeight*qEff*len - margin，不加入最近访问惩罚。
 *   - 如果目标位于 Boss-gated 区域，I_proxy 的 |C| 按 Amax 计算，用于表达“踏过 Boss 后可能继续通向出口”的推进价值。
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
    if (!pathKeepsResourceNonNegative(path, context.state.resource, localMap)) return -1e18;

    const double info = informationProxy(target, localMap, poseEstimator,
                                         context.exitPath.empty() ? static_cast<double>(parameters_.areaMax)
                                                                  : parameters_.knownExitAreaCap,
                                         context.bossGatedAreaMaxTargets.count(target) > 0);
    const double tail = futureGainMarginal(target, path, localMap);
    if (delta < 0 && info == 0.0) return -1e18;
    const double qEff = computeQEff(context, localMap);
    const double margin = marginPenalty(projectedResource);
    return delta + parameters_.omegaI * context.state.alphaSmooth * info + parameters_.beta * tail -
           parameters_.qEffLengthWeight * qEff * pathLength(path) - margin;
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
