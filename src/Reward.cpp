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
    // 坐标是否在记忆地图中（不论是否观察过）。未插入 cells_ 的坐标视为从未被观察到。
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
    // 清空所有候选假设。
    hypotheses_.clear();
    // 创建唯一的默认假设：入口在 15×15 中心 {7,7}，面向下，评分为 0，标记为可行。
    // 此时 maskActive_=false，掩码未启用 — AI 假设自己在迷宫内部某处，不做边界裁剪。
    hypotheses_.push_back({{kEstimatedSize / 2, kEstimatedSize / 2}, Direction::Down, 0.0, true});
    // 将该唯一假设设为当前最优。
    best_ = hypotheses_[0];
    // 清空已观察估计坐标集 — 掩码未激活，observedEstimated_ 为空是合理的。
    observedEstimated_.clear();
    // 出生边缘种子尚未检测，等待首次 update() 调用。
    maskSeeded_ = false;
    // 掩码未激活 — I_proxy 的 BFS 不会被 15×15 边界裁剪。
    maskActive_ = false;
    // 默认认为 AI 出生在迷宫内部（无边缘信息）。
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
    // 获取当前已观察格列表，后续所有步骤都依赖此数据。
    const auto observed = localMap.observedPositions();
    // ===== 阶段 1：出生边缘种子检测（仅首次调用时执行） =====
    // 通过出生点周围 3x3 视野中的 outside（越界）格判断 AI 从迷宫哪条边进入。
    // 例如：顶部有越界格 + 左边有越界格 → AI 出生在左上角。
    if (!maskSeeded_) {
        const auto outside = localMap.outsidePositions();
        const auto hasOutside = [&](Position pos) {
            return std::find(outside.begin(), outside.end(), pos) != outside.end();
        };
        const auto addHypothesis = [&](Position entry) {
            hypotheses_.push_back({entry, Direction::Down, 0.0, true});
        };
        // 检测各方向是否有完整的一行/列越界格（以 3x3 的角格作为交叉判定）。
        const bool topOutside = hasOutside({-1, -1}) && hasOutside({-1, 0}) && hasOutside({-1, 1});
        const bool bottomOutside = hasOutside({1, -1}) && hasOutside({1, 0}) && hasOutside({1, 1});
        const bool leftOutside = hasOutside({-1, -1}) && hasOutside({0, -1}) && hasOutside({1, -1});
        const bool rightOutside = hasOutside({-1, 1}) && hasOutside({0, 1}) && hasOutside({1, 1});

        hypotheses_.clear();
        // 根据越界方向组合，确定 seedKind_ 并生成候选入口假设。
        // 角落情况（两个方向同时越界）：唯一确定入口位置。
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
        // 单边情况：只能确定在哪条边上，但不知道具体偏移量。
        // 生成 col=1..13（或 row=1..13）的多个候选假设。
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
            // 无越界信息（出生点在迷宫内部）：默认入口在中心，掩码暂不启用。
            addHypothesis({kEstimatedSize / 2, kEstimatedSize / 2});
        }
        best_ = hypotheses_.front();
        maskSeeded_ = true;
    }

    // ===== 阶段 2：计算已观察格的包围盒（局部坐标下的跨度） =====
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

    // ===== 阶段 3：用已观察格约束裁剪候选假设 =====
    // hasFullHorizontal: 水平跨度 >= 15，说明已经看到迷宫完整宽度。
    const bool hasFullHorizontal = maxCol - minCol + 1 >= kEstimatedSize;
    const bool hasFullVertical = maxRow - minRow + 1 >= kEstimatedSize;
    // canPrune: 跨度 >= 14 时，已可以排除不一致的偏移假设。
    const bool canPruneHorizontal = maxCol - minCol + 1 >= kEstimatedSize - 1;
    const bool canPruneVertical = maxRow - minRow + 1 >= kEstimatedSize - 1;
    // 裁剪条件：Top/Bottom 边界需要水平跨度足够，Left/Right 需要垂直跨度足够。
    const bool canPruneHypotheses =
        ((seedKind_ == MaskSeedKind::Top || seedKind_ == MaskSeedKind::Bottom) && canPruneHorizontal) ||
        ((seedKind_ == MaskSeedKind::Left || seedKind_ == MaskSeedKind::Right) && canPruneVertical);
    if (canPruneHypotheses) {
        std::vector<MapEmbeddingHypothesis> feasibleHypotheses;
        for (auto hypothesis : hypotheses_) {
            bool feasible = true;
            // 检查 1：每个已观察格映射后必须在 15x15 范围内。
            for (const auto &pos : observed) {
                const Position mapped = mapWithHypothesis(pos, hypothesis);
                if (!insideEstimated(mapped)) {
                    feasible = false;
                    break;
                }
                // 检查 2：映射到 15x15 边界的格子，在局部地图中必须是墙。
                // 因为迷宫的最外一圈是边界墙，如果局部看到的不是墙说明映射错误。
                // 唯一例外：起点 S（pos=={0,0}）可以位于边界的入口位置。
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
            // 检查 3：outside 格映射后不能落在 15x15 范围内。
            // outside 表示迷宫外部，如果在估计内部则假设矛盾。
            for (const auto &pos : localMap.outsidePositions()) {
                if (insideEstimated(mapWithHypothesis(pos, hypothesis))) {
                    feasible = false;
                    break;
                }
            }
            if (feasible) feasibleHypotheses.push_back(hypothesis);
        }
        // 只有至少保留一个可行假设时才更新；如果全部被排除则保留原假设。
        if (!feasibleHypotheses.empty()) {
            hypotheses_ = feasibleHypotheses;
            best_ = hypotheses_.front();
        }
    }

    // ===== 阶段 4：掩码激活判定 =====
    // 根据 seedKind_ 和当前观察跨度，决定是否启用 15x15 掩码裁剪。
    switch (seedKind_) {
    case MaskSeedKind::Top:
    case MaskSeedKind::Bottom:
        // 顶部/底部边界：假设唯一 或 水平已满 15 格时激活。
        maskActive_ = hypotheses_.size() == 1 || hasFullHorizontal;
        // 水平满 15 格后可以精确定位列偏移。
        if (hasFullHorizontal) best_.entry.second = -minCol;
        break;
    case MaskSeedKind::Left:
    case MaskSeedKind::Right:
        // 左/右边界：假设唯一 或 垂直已满 15 格时激活。
        maskActive_ = hypotheses_.size() == 1 || hasFullVertical;
        if (hasFullVertical) best_.entry.first = -minRow;
        break;
    case MaskSeedKind::TopLeft:
    case MaskSeedKind::TopRight:
    case MaskSeedKind::BottomLeft:
    case MaskSeedKind::BottomRight:
        // 角落情况：出生时即可确定唯一入口，掩码始终激活。
        maskActive_ = true;
        break;
    case MaskSeedKind::Internal:
        // 内部出生（无越界信息）：需要水平和垂直都满 15 格才能激活掩码。
        maskActive_ = (maxCol - minCol + 1 >= kEstimatedSize) && (maxRow - minRow + 1 >= kEstimatedSize);
        if (maskActive_) best_.entry = {-minRow, -minCol};
        break;
    }

    // ===== 阶段 5：重建估计已观察格集合 =====
    // 用最佳假设将所有已观察格映射到 15x15 估计坐标。
    observedEstimated_.clear();
    for (const auto &pos : observed) {
        const Position mapped = mapWithHypothesis(pos, best_);
        // 安全守卫：如果掩码已激活但映射结果越界，说明假设矛盾，回退掩码。
        if (maskActive_ && !insideEstimated(mapped)) {
            maskActive_ = false;
            observedEstimated_.clear();
            break;
        }
        // 只收录映射后落在 15x15 范围内的坐标。
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
    // 掩码未激活时，不做边界裁剪 — 所有方向都视为"在迷宫内"，避免过度限制早期探索。
    if (!maskActive_) return true;
    // 掩码激活后，将局部坐标映射到估计 15×15，检查是否在 [0,14]×[0,14] 范围内。
    // 落在范围外的格子视为迷宫外部，不计入未知区域探索价值。
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
    // 守卫 1：areaMax <= 0 意味着不统计任何未知格。
    if (areaMax <= 0) return {0};
    // 守卫 2：掩码已激活但目标不在估计迷宫内，无未知区域可探索。
    if (maskActive_ && !isInsideEstimatedMaze(localTarget)) return {0};

    // 构建已观察格集合（作为 BFS 的障碍），已观察格不可穿过。
    const auto observedPositions = localMap.observedPositions();
    std::set<Position> observed(observedPositions.begin(), observedPositions.end());
    // Lambda：判断某局部坐标是否被估计掩码边界阻挡。
    // 掩码激活时：15x15 四条边视为不可穿过的墙。
    // 掩码未激活但已知出生边时：seedKind_ 对应的方向也按迷宫边界处理。
    const auto blockedByMaskBoundary = [&](Position pos) {
        const Position mapped = localToEstimatedGlobal(pos);
        if (maskActive_) {
            // 掩码激活：超出 15x15 或位于边缘一圈都视作墙。
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
    // 守卫 3：目标本身被边界阻挡，无未知区域可展开。
    if (blockedByMaskBoundary(localTarget)) return {0};
    // BFS：从 target 出发，已观察格不可穿过，未知格可继续扩展。
    // 返回的是"能触达的未知格数量"——即 BFS 过程中遇到的、不在 observed 中的格子数。
    std::set<Position> visited;
    std::queue<Position> queue;
    queue.push(localTarget);
    visited.insert(localTarget);
    int expandableCount = 0;
    while (!queue.empty()) {
        const auto [row, col] = queue.front();
        queue.pop();
        // 当前格不在已观察集合中 → 是未知格 → 计数加一。
        if (!observed.count({row, col})) {
            ++expandableCount;
            // 提前终止优化：已知未知格数达到 areaMax 上限，无需继续搜索。
            if (expandableCount >= areaMax) return {areaMax};
        }
        for (const auto [dr, dc] : kDirs) {
            const Position next{row + dr, col + dc};
            // 跳过已访问格、已观察格和被掩码边界阻挡的格。
            if (visited.count(next) || observed.count(next) || blockedByMaskBoundary(next)) {
                continue;
            }
            visited.insert(next);
            queue.push(next);
        }
    }
    // BFS 结束，返回实际统计到的未知格数量（可能小于 areaMax，表示连通块封闭）。
    return {expandableCount};
}

/**
 * 功能：判断目标格的未知延伸区域是否能触达当前可确认的迷宫边缘。
 * 输入：
 *   - localTarget：候选目标的局部坐标。
 *   - localMap：AI 当前局部记忆地图，已观察格会作为 BFS 障碍。
 * 输出：
 *   - 如果从目标附近的未知区域可以扩展到掩码或部分边缘，返回 true；否则返回 false。
 * 关键逻辑：
 *   - 使用与 |C| 计算一致的 BFS 模型：已观察格不可穿过，未知格可扩展。
 *   - 只有触达当前已知的迷宫边缘时才返回 true；掩码未提供边缘信息时不会凭空推断。
 */
bool MapPoseEstimator::unknownExtensionTouchesMazeEdge(Position localTarget, const LocalKnownMap &localMap) const
{
    // 用途：判断从 target 出发的未知延伸是否触达迷宫边缘。
    // Boss-gated bonus 现在不再依赖该判断；该函数保留给边缘可达性诊断和后续掩码逻辑复用。

    // 守卫 1：内部出生且掩码未激活 → 完全不知道边缘在哪里 → 保守返回 false。
    if (!maskActive_ && seedKind_ == MaskSeedKind::Internal) return false;
    // 守卫 2：掩码已激活但 target 在 15×15 外 → 无意义，直接 false。
    if (maskActive_ && !isInsideEstimatedMaze(localTarget)) return false;

    // 收集所有已观察格，BFS 过程中遇到 observed 格视为障碍 ——
    // 只沿未知格向外扩展，已观察格已经探索过，不能作为 "未知延伸"。
    const auto observedPositions = localMap.observedPositions();
    std::set<Position> observed(observedPositions.begin(), observedPositions.end());

    // Lambda：判断一个局部格是否被掩码边界阻挡。
    // 掩码边界在此视为 "迷宫边缘" —— BFS 触达边界 = 未知延伸触达迷宫边缘。
    const auto blockedByMaskBoundary = [&](Position pos) {
        const Position mapped = localToEstimatedGlobal(pos);
        if (maskActive_) {
            // 完整掩码已启用：15×15 外围一圈是迷宫边界墙。
            // mapped 落在 15×15 外或恰好在外围一圈 → 触达边缘。
            if (!insideEstimated(mapped)) return true;
            return mapped.first == 0 || mapped.second == 0 ||
                   mapped.first == kEstimatedSize - 1 || mapped.second == kEstimatedSize - 1;
        }
        // 掩码未完全激活但有边缘种子信息：只阻挡已知的迷宫边界方向。
        // 例如出生在上边界 → 已知 row≤0 是迷宫外 → 触达该方向视为触边。
        switch (seedKind_) {
        case MaskSeedKind::Top:
        case MaskSeedKind::Bottom:
            return mapped.first <= 0 || mapped.first >= kEstimatedSize - 1;
        case MaskSeedKind::Left:
        case MaskSeedKind::Right:
            return mapped.second <= 0 || mapped.second >= kEstimatedSize - 1;
        default:
            return false;  // 不应到达（外部已过滤 Internal）
        }
    };

    // target 自身就在边界上 → 直接视为触达。
    if (blockedByMaskBoundary(localTarget)) return true;

    // BFS：从 target 出发，沿未知格向四方向扩展。
    // 遇到 observed 格阻挡（已探索区域）；遇到 blockedByMaskBoundary → true。
    std::set<Position> visited;
    std::queue<Position> queue;
    queue.push(localTarget);
    visited.insert(localTarget);
    const size_t searchLimit = static_cast<size_t>(kEstimatedSize * kEstimatedSize);
    while (!queue.empty()) {
        const auto [row, col] = queue.front();
        queue.pop();
        for (const auto [dr, dc] : kDirs) {
            const Position next{row + dr, col + dc};
            // 触达掩码边界 → 未知延伸确实通向迷宫边缘 → 返回 true。
            if (blockedByMaskBoundary(next)) return true;
            // 已访问过或已被观察过 → 不可继续扩展（避免循环、避免穿过已知区域）。
            if (visited.count(next) || observed.count(next)) continue;
            // 掩码未完全激活时，未知区域不能被当成无限平面搜索；超过 15×15 假设容量仍未触边，
            // 说明当前记忆不足以可靠证明通向边缘，按不触边处理，避免 BFS 无界扩展。
            if (!maskActive_ && visited.size() >= searchLimit) return false;
            visited.insert(next);
            queue.push(next);
        }
    }
    // BFS 耗尽所有未知连通块仍未触边 → 未知延伸被已观察区域包围 → 不触边。
    return false;
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
    // 将局部坐标 (localPos) 按指定假设 (hypothesis) 映射为估计 15×15 坐标。
    // 局部坐标系以 AI 出生点为原点 (0,0)，localPos.first=行偏移，localPos.second=列偏移。
    // 估计 15×15 坐标系以迷宫左上角为原点 (0,0)。
    switch (hypothesis.inwardDirection) {
    case Direction::Down:
        // AI 从上边界进入，面向下：局部向下走 = 估计行增加，局部向右走 = 估计列增加。
        // entry 是上边界入口位置，localPos 直接平移叠加。
        return {hypothesis.entry.first + localPos.first, hypothesis.entry.second + localPos.second};
    case Direction::Up:
        // AI 从下边界进入，面向上：局部向下走 = 估计行减少（方向反转）。
        // 行坐标取反（entry.row − local.row），列保持不变。
        return {hypothesis.entry.first - localPos.first, hypothesis.entry.second + localPos.second};
    case Direction::Right:
        // AI 从左边界进入，面向右：局部向右走 = 估计行增加；局部向下走 = 估计列增加。
        // 行与列交换映射（旋转变换）。
        return {hypothesis.entry.first + localPos.second, hypothesis.entry.second + localPos.first};
    case Direction::Left:
        // AI 从右边界进入，面向左：局部向右走 = 估计行增加；局部向下走 = 估计列减少。
        // 与 Right 类似但列方向反转。
        return {hypothesis.entry.first + localPos.second, hypothesis.entry.second - localPos.first};
    }
    // 不应到达此处；返回 entry 作为 fallback。
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
    // 从 i=1 开始遍历，跳过 path[0]（当前站立格），避免对已经结算过的格子重复加减资源。
    // path[0] 位置的资源和一次性事件（金币/陷阱）在 AI 到达该格时已经结算过了。
    for (size_t i = 1; i < path.size(); ++i) {
        const Position pos = path[i];
        const std::string tile = localMap.tile(pos);
        // 金币 +50，但只有尚未拾取的才会计入；已拾取代表资源已入账，不可重复加分。
        if (tile == "G" && !localMap.isCollected(pos)) delta += kGoldValue;
        // 陷阱 -30，但只有尚未触发的才会计入；已触发代表惩罚已扣过，不可重复扣分。
        if (tile == "T" && !localMap.isTriggered(pos)) delta += kTrapValue;
    }
    // delta 是该路径上所有一次性资源的净变化量（可能为正、负或零）。
    return delta;
}

/**
 * 功能：计算探索信息价值 I_proxy。
 * 输入：
 *   - target：候选目标局部坐标。
 *   - localMap：局部记忆地图。
 *   - poseEstimator：姿态估计器；只有掩码已完全确定时才用它裁剪 |C|。
 *   - areaCap：|C| 面积奖励上限；出口未知时通常为 Amax，出口已知后可降到 1.5。
 *   - forceAreaMax：是否将 Boss-gated 目标直接按 bossEdgeAreaBonus 计算 |C|。
 * 输出：
 *   - 返回按当前价值密度估计后的未知连通块贡献。
 * 关键逻辑：
 *   - |C| 由局部记忆地图 BFS 得到；掩码未启用时不裁剪，掩码启用后不允许 BFS 展开到掩码外部。
 *   - 对必须踏过 Boss 才能继续到达的通关推进区域，一律按 bossEdgeAreaBonus 计入。
 */
double PathValueEvaluator::informationProxy(Position target, const LocalKnownMap &localMap,
                                            const MapPoseEstimator &poseEstimator, double areaCap,
                                            bool forceAreaMax) const
{
    // 第一步：统计已观察格中的金币和陷阱数量，用于估计各类型的密度。
    int observedCount = 0;
    int goldCount = 0;
    int trapCount = 0;
    for (const auto &pos : localMap.observedPositions()) {
        ++observedCount;
        const std::string tile = localMap.tile(pos);
        if (tile == "G") ++goldCount;
        if (tile == "T") ++trapCount;
    }
    // 第二步：用贝叶斯平滑估计金币密度 rhoG 和陷阱密度 rhoT。
    // 分母加 lambda 避免观察数少时密度估计波动过大（伪计数平滑）；
    // 分子加 lambdaG/lambdaT 作为先验，避免早期观察为零时密度被估计为零。
    const double denominator = observedCount + parameters_.lambda;
    const double rhoG = (goldCount + parameters_.lambdaG) / denominator;
    const double rhoT = (trapCount + parameters_.lambdaT) / denominator;
    // 第三步：计算未知区域单位面积期望价值 v_area = 50 * rhoG - 30 * rhoT。
    // 50 和 30 分别对应金币和陷阱的资源价值（见 GameTypes.h）。
    // 然后归一化到 [rhoAreaValueMin, 1] 得到价值密度 rho_area_value。
    const double areaValue = 50.0 * rhoG - 30.0 * rhoT;
    const double areaValueDensity = std::clamp(std::max(areaValue, 0.0) / 50.0, parameters_.rhoAreaValueMin, 1.0);

    // 第四步：确定 |C| 面积裁剪上限。areaCap <= 0 表示使用默认 areaMax；
    // 出口已知后 areaCap 通常会降到 knownExitAreaCap (1.5)，削弱开阔区域奖励。
    const double activeAreaCap = areaCap > 0.0 ? areaCap : static_cast<double>(parameters_.areaMax);
    const int searchAreaCap = std::max(1, static_cast<int>(std::ceil(activeAreaCap)));
    double componentValue = 0.0;
    // 第五步：计算未知连通块价值。
    // Boss-gated 区域（forceAreaMax=true）不再要求未知延伸触达迷宫边缘，
    // 一律按 bossEdgeAreaBonus (15) 替代 |C|，给予固定通关推进权重。
    if (forceAreaMax) {
        componentValue = static_cast<double>(parameters_.bossEdgeAreaBonus) * areaValueDensity;
    } else {
        // 一般情况：对每个与目标视野接触的未知连通块，取 min(|C|, cap) 并乘以价值密度。
        for (const int size : poseEstimator.unknownComponentSizesTouchingView(target, localMap, searchAreaCap)) {
            componentValue += std::min(static_cast<double>(size), activeAreaCap) * areaValueDensity;
        }
    }
    // 第六步：乘以 kappaU (60) 得到最终 I_proxy。
    // kappaU 将"等效未知格数"映射到与金币同一量级的奖励空间。
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
    // 第一步：收集当前路径上会拾取的金币集合，避免与路径真实收益重复计入。
    // 这些金币的 +50 已经包含在 pathResourceDelta 的 DeltaR 中，
    // 如果再计入 tailUB 就会重复算同一枚金币。
    std::set<Position> coinsOnPath;
    for (size_t i = 1; i < path.size(); ++i) {
        if (localMap.tile(path[i]) == "G" && !localMap.isCollected(path[i])) coinsOnPath.insert(path[i]);
    }

    double best = 0.0;
    // 第二步：对所有已知未拾取金币，排除已在路径上的，计算从 target 出发的 50/(dist+1) 上界。
    // 50/(dist+1) 是一种边际效用估计：金币价值被步数稀释，
    // dist 是 target 到该金币的最短已知路径长度（不含 target 自身步数）。
    for (const auto &coin : localMap.knownCoins()) {
        if (coinsOnPath.find(coin) != coinsOnPath.end()) continue;
        const auto coinPath = shortestPathOnKnownMap(target, coin, localMap);
        // 如果从 target 不可达该金币，跳过。
        if (coinPath.empty()) continue;
        best = std::max(best, static_cast<double>(kGoldValue) / (pathLength(coinPath) + 1.0));
    }
    // 返回所有可达金币中最大的 50/(dist+1) 值；无可达金币时返回 0。
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
    // q_ref 是"单位步数的平均资源回报"，用于衡量每一步的机会成本。
    // 分母加 epsilon (1e-6) 防止 steps=0 时除零。
    double qRef = static_cast<double>(context.state.resource) / (context.state.steps + parameters_.epsilon);
    // 如果出口已知且可达，用出口路径估计更准确的机会成本：
    // 即"到达终点时的总资源 / 总步数"，反映了完整的资源效率。
    if (!context.exitPath.empty()) {
        const int exitDelta = pathResourceDelta(context.exitPath, localMap);
        qRef = static_cast<double>(context.state.resource + exitDelta) /
               (context.state.steps + pathLength(context.exitPath) + parameters_.epsilon);
    }
    // q_eff = max(q_ref, q_min)：取 q_ref 和 qMin (1.0) 的较大值。
    // qMin 防止开局 R=0 时 q_ref=0，导致路径长度代价完全消失。
    return std::max(qRef, parameters_.qMin);
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
    // 第一步：统计当前已观察格中的金币和陷阱数量。
    int observedCount = 0;
    int goldCount = 0;
    int trapCount = 0;
    for (const auto &pos : localMap.observedPositions()) {
        ++observedCount;
        const std::string tile = localMap.tile(pos);
        if (tile == "G") ++goldCount;
        if (tile == "T") ++trapCount;
    }

    // 第二步：计算贝叶斯平滑后的金币密度 rhoG 和陷阱密度 rhoT。
    // lambda/lambdaG/lambdaT 是伪计数先验，防止早期样本少时密度估计剧烈震荡。
    const double denominator = observedCount + parameters_.lambda;
    const double rhoG = (goldCount + parameters_.lambdaG) / denominator;
    const double rhoT = (trapCount + parameters_.lambdaT) / denominator;
    // rhoU 是未知区域比例 = 估计未知格数 / 225（15*15 迷宫总格数）。
    const double rhoU = static_cast<double>(poseEstimator.estimatedUnknownCount()) / 225.0;
    // v_unk 是未知区域的期望净值，用已观察区域的金币/陷阱密度外推。
    const double unknownValue = 50.0 * rhoG - 30.0 * rhoT;

    // 第三步：计算原始 alpha_raw。
    // 分子：alpha0 * (1 + wU*rhoU + wV*max(v_unk,0)/50)
    //   — 未知区域多 → 探索权重升高；期望净值高 → 探索权重升高。
    // 分母：1 + wR*rhoT
    //   — 陷阱密度高 → 风险大 → 探索权重被抑制。
    const double raw = parameters_.alpha0 *
                       (1.0 + parameters_.wU * rhoU + parameters_.wV * std::max(unknownValue, 0.0) / 50.0) /
                       (1.0 + parameters_.wR * rhoT);
    // 将 raw 裁剪到 [alphaMin, alphaMax] 防止极端值（例如全陷阱区域 alpha 被压到 0 以下）。
    const double clipped = std::clamp(raw, parameters_.alphaMin, parameters_.alphaMax);
    // 第四步：EMA 平滑 alpha_t = theta * alpha_{t-1} + (1-theta) * alpha_raw。
    // theta=0.8 表示上一步 alpha 占比 80%，新估计占比 20%，防止 alpha 剧烈波动。
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
 *   - 路径中间资源允许为负；只有走完整条候选路径后的资源为负时，才判为负无穷。
 *   - 若 DeltaR、I_proxy、tailUB 都为 0，说明目标没有任何收益来源，直接判为负无穷。
 *   - 其余情况使用 DeltaR + omegaI*alpha*I + beta*tailUB - qEffLengthWeight*qEff*len，不加入低资源风险项或最近访问惩罚。
 *   - 如果目标位于 Boss-gated 区域，I_proxy 的 |C| 一律按 bossEdgeAreaBonus 计算。
 */
double PathValueEvaluator::evaluate(const std::vector<Position> &path, Position target,
                                    const PathValueContext &context, const LocalKnownMap &localMap,
                                    const MapPoseEstimator &poseEstimator) const
{
    // 守卫 1：路径至少要有 2 个节点（起点+目标），单节点路径无意义。
    if (path.size() <= 1) return -1e18;
    // 守卫 2：路径上所有格子必须是在已知地图上可通行的。
    // 任何未知格、墙或已清除前的 Boss 都会使路径无效。
    for (const auto &pos : path) {
        if (!localMap.isWalkableForPlanning(pos)) return -1e18;
    }

    // 计算路径真实资源变化 DeltaR（金币+50，陷阱-30）。
    const int delta = pathResourceDelta(path, localMap);
    const int projectedResource = context.state.resource + delta;
    // 守卫 3：只检查走完路径后的总资源不能为负；路径中间允许短暂为负。
    if (projectedResource < 0) return -1e18;

    // 计算探索信息价值 I_proxy。
    // 出口未知时 areaCap = areaMax (12)；出口已知后降到 knownExitAreaCap (1.5)。
    // Boss-gated 目标（context.bossGatedAreaMaxTargets 中的目标）forceAreaMax=true，
    // 会触发 bossEdgeAreaBonus (15) 替代 |C|。
    const double info = informationProxy(target, localMap, poseEstimator,
                                         context.exitPath.empty() ? static_cast<double>(parameters_.areaMax)
                                                                  : parameters_.knownExitAreaCap,
                                         context.bossGatedAreaMaxTargets.count(target) > 0);
    // 计算边际尾部金币价值上界 V_tail^marg。
    const double tail = futureGainMarginal(target, path, localMap);
    // 守卫 5：如果 DeltaR <= 0 且 I_proxy == 0，目标没有任何收益来源（无金币、无探索价值），
    // 直接判为负无穷，避免 AI 选择纯浪费步数的目标。
    if (delta <= 0 && info == 0.0) return -1e18;
    // 计算路径长度机会成本系数 q_eff。
    const double qEff = computeQEff(context, localMap);
    // 主评分公式：
    // Score = DeltaR + omegaI * alpha * I_proxy + beta * V_tail - eta_q * qEff * len
    return delta + parameters_.omegaI * context.state.alphaSmooth * info + parameters_.beta * tail -
           parameters_.qEffLengthWeight * qEff * pathLength(path);
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
