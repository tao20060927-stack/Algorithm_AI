#include "ResourcePickupStrategy.h"

#include <array>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace ai_player {
namespace {
constexpr std::array<Position, 4> kPickupDirs{{{-1, 0}, {0, 1}, {1, 0}, {0, -1}}};
constexpr double kEps = 1e-9;

struct ResourceCell {
    Position pos;
    int value;
    std::string tile;
};

struct BundleEval {
    int candidateIndex = -1;
    Position target{kInvalid};
    std::string cell;
    int cellValue = 0;
    int adjGoldCount = 0;
    int bundleValue = 0;
    int bundleLen = 0;
    double bundleScore = -std::numeric_limits<double>::infinity();
    std::vector<Position> bundlePath;
    int projectedResource = 0;
    int projectedSteps = 0;
    double projectedRatio = 0.0;
    bool valid = false;
    bool selected = false;
};

/**
 * 功能：从 3x3 grid 中解析玩家起点和资源格。
 * 输入：
 *   - source：用户输入 JSON。
 *   - grid：输出解析后的 3x3 字符串网格。
 *   - start：输出玩家 P 的坐标。
 *   - resources：输出所有 G/T 资源格及其分值。
 * 输出：
 *   - 无返回值；格式错误时抛出异常。
 * 关键逻辑：
 *   - 只接受 P、G、T、. 和可选墙 #；该策略固定用于 3x3 局部资源贪心。
 */
void parseResourceGrid(const Json &source,
                       std::vector<std::vector<std::string>> &grid,
                       Position &start,
                       std::vector<ResourceCell> &resources)
{
    if (!source.contains("grid") || !source["grid"].is_array()) {
        throw std::runtime_error("JSON must contain grid array for 3x3 resource pickup");
    }
    const auto &inputGrid = source["grid"];
    if (inputGrid.size() != 3) {
        throw std::runtime_error("grid must have exactly 3 rows");
    }

    grid.assign(3, std::vector<std::string>(3));
    int startCount = 0;
    for (int row = 0; row < 3; ++row) {
        if (!inputGrid[row].is_array() || inputGrid[row].size() != 3) {
            throw std::runtime_error("grid must be a 3x3 array");
        }
        for (int col = 0; col < 3; ++col) {
            const std::string tile = inputGrid[row][col].get<std::string>();
            if (tile != "." && tile != "P" && tile != "G" && tile != "T" && tile != "#") {
                throw std::runtime_error("unknown 3x3 resource cell: " + tile);
            }
            grid[row][col] = tile;
            if (tile == "P") {
                start = {row, col};
                ++startCount;
            } else if (tile == "G") {
                resources.push_back({{row, col}, kGoldValue, tile});
            } else if (tile == "T") {
                resources.push_back({{row, col}, kTrapValue, tile});
            }
        }
    }
    if (startCount != 1) {
        throw std::runtime_error("grid must contain exactly one player P");
    }
}

/**
 * 功能：判断 3x3 坐标是否在边界内。
 * 输入：
 *   - pos：待检查坐标。
 * 输出：
 *   - 返回该坐标是否位于 3x3 网格内。
 * 关键逻辑：
 *   - 所有局部束候选和邻接金币都必须先通过边界检查。
 */
bool inside3x3(Position pos)
{
    return pos.first >= 0 && pos.second >= 0 && pos.first < 3 && pos.second < 3;
}

/**
 * 功能：判断某个资源编号是否已经结算。
 * 输入：
 *   - mask：已结算资源集合。
 *   - index：资源编号。
 * 输出：
 *   - 返回该资源是否已触发。
 * 关键逻辑：
 *   - 金币和陷阱都只触发一次；重复经过已触发格不会再次加分或扣分。
 */
bool isTriggered(int mask, int index)
{
    return (mask & (1 << index)) != 0;
}

/**
 * 功能：查找指定坐标对应的资源编号。
 * 输入：
 *   - resources：所有 G/T 资源格。
 *   - pos：待查找坐标。
 * 输出：
 *   - 返回资源编号；如果该坐标不是资源格，返回 -1。
 * 关键逻辑：
 *   - 通过坐标映射到资源 mask 位，用于判断该格是否已经结算。
 */
int resourceIndexAt(const std::vector<ResourceCell> &resources, Position pos)
{
    for (int i = 0; i < static_cast<int>(resources.size()); ++i) {
        if (resources[i].pos == pos) return i;
    }
    return -1;
}

/**
 * 功能：计算进入某个格子时的单格资源变化。
 * 输入：
 *   - resources：所有 G/T 资源格。
 *   - pos：候选格坐标。
 *   - mask：当前已触发资源集合。
 * 输出：
 *   - 返回未触发 G/T 的资源变化；非资源格或已触发资源返回 0。
 * 关键逻辑：
 *   - 该值对应公式中的 cellValue(x)。
 */
int cellValueAt(const std::vector<ResourceCell> &resources, Position pos, int mask)
{
    const int index = resourceIndexAt(resources, pos);
    if (index < 0 || isTriggered(mask, index)) return 0;
    return resources[index].value;
}

/**
 * 功能：计算候选格的邻接未收集金币列表。
 * 输入：
 *   - grid：3x3 输入网格。
 *   - resources：所有 G/T 资源格。
 *   - target：候选格坐标。
 *   - mask：当前已触发资源集合。
 * 输出：
 *   - 返回按“上、右、下、左”排序的未收集邻接金币坐标。
 * 关键逻辑：
 *   - 局部束只顺手收集候选格四邻域内尚未收集的金币，不扩展搜索其它位置。
 */
std::vector<Position> adjacentUncollectedGolds(const std::vector<std::vector<std::string>> &grid,
                                               const std::vector<ResourceCell> &resources,
                                               Position target,
                                               int mask)
{
    std::vector<Position> golds;
    for (const auto [dr, dc] : kPickupDirs) {
        const Position next{target.first + dr, target.second + dc};
        if (!inside3x3(next) || grid[next.first][next.second] != "G") continue;
        const int index = resourceIndexAt(resources, next);
        if (index >= 0 && !isTriggered(mask, index)) golds.push_back(next);
    }
    return golds;
}

/**
 * 功能：计算候选格子的局部束 reward。
 * 输入：
 *   - grid：3x3 输入网格。
 *   - current：当前玩家坐标。
 *   - target：四邻域候选格坐标。
 *   - resources：所有 G/T 资源格。
 *   - mask：当前已触发资源集合。
 *   - candidateIndex：候选方向编号，按上、右、下、左递增。
 * 输出：
 *   - 返回 BundleEval，包含 BundleValue、BundleLen、BundleScore 和局部束路径。
 * 关键逻辑：
 *   - 不做 DP、不枚举路径，只计算“走到 target 并顺手收集 target 四邻域金币”的固定局部束。
 */
BundleEval evaluateBundleCandidate(const std::vector<std::vector<std::string>> &grid,
                                   Position current,
                                   Position target,
                                   const std::vector<ResourceCell> &resources,
                                   int mask,
                                   int candidateIndex)
{
    BundleEval eval;
    eval.candidateIndex = candidateIndex;
    eval.target = target;
    if (!inside3x3(target) || grid[target.first][target.second] == "#") return eval;

    const std::vector<Position> golds = adjacentUncollectedGolds(grid, resources, target, mask);
    eval.valid = true;
    eval.cell = grid[target.first][target.second];
    eval.cellValue = cellValueAt(resources, target, mask);
    eval.adjGoldCount = static_cast<int>(golds.size());
    eval.bundleValue = eval.cellValue + kGoldValue * eval.adjGoldCount;
    eval.bundleLen = eval.adjGoldCount == 0 ? 1 : 2 * eval.adjGoldCount;
    eval.bundleScore = eval.bundleLen == 0 ? 0.0 : static_cast<double>(eval.bundleValue) / eval.bundleLen;

    eval.bundlePath = {current, target};
    for (int i = 0; i < static_cast<int>(golds.size()); ++i) {
        if (i > 0) eval.bundlePath.push_back(target);
        eval.bundlePath.push_back(golds[i]);
    }
    return eval;
}

/**
 * 功能：比较两个局部束候选。
 * 输入：
 *   - best：当前最优候选。
 *   - candidate：待比较候选。
 * 输出：
 *   - candidate 应替换 best 时返回 true。
 * 关键逻辑：
 *   - 按 BundleScore、BundleValue、BundleLen、方向顺序进行贪心 tie-break；不使用全路径搜索。
 */
bool betterBundle(const BundleEval &best, const BundleEval &candidate)
{
    if (!candidate.valid) return false;
    if (!best.valid) return true;
    if (candidate.bundleScore > best.bundleScore + kEps) return true;
    if (std::abs(candidate.bundleScore - best.bundleScore) <= kEps && candidate.bundleValue != best.bundleValue) {
        return candidate.bundleValue > best.bundleValue;
    }
    if (std::abs(candidate.bundleScore - best.bundleScore) <= kEps && candidate.bundleValue == best.bundleValue &&
        candidate.bundleLen != best.bundleLen) {
        return candidate.bundleLen < best.bundleLen;
    }
    return false;
}

/**
 * 功能：从当前位置四邻域中选择 BundleScore 最大的候选。
 * 输入：
 *   - grid：3x3 输入网格。
 *   - current：当前玩家坐标。
 *   - resources：所有 G/T 资源格。
 *   - mask：当前已触发资源集合。
 *   - candidates：输出四邻域候选评分表。
 * 输出：
 *   - 返回最佳局部束候选；无可走候选时 valid=false。
 * 关键逻辑：
 *   - 每轮只检查上、右、下、左四个格子，每个格子只看自己的四邻域金币，因此 reward 计算为 O(1)。
 */
BundleEval chooseBestBundleGreedy(const std::vector<std::vector<std::string>> &grid,
                                  Position current,
                                  const std::vector<ResourceCell> &resources,
                                  int mask,
                                  std::vector<BundleEval> &candidates)
{
    BundleEval best;
    int candidateIndex = 0;
    for (const auto [dr, dc] : kPickupDirs) {
        const Position target{current.first + dr, current.second + dc};
        BundleEval candidate = evaluateBundleCandidate(grid, current, target, resources, mask, candidateIndex++);
        if (candidate.valid) {
            candidates.push_back(candidate);
            if (betterBundle(best, candidate)) best = candidate;
        }
    }
    return best;
}

/**
 * 功能：把坐标路径转为前端 JSON。
 * 输入：
 *   - path：坐标序列。
 * 输出：
 *   - 返回 [{"row":r,"col":c}, ...] 形式的 JSON 数组。
 * 关键逻辑：
 *   - 顶层 path、决策路径和播放帧统一使用同一坐标格式。
 */
Json pathJson(const std::vector<Position> &path)
{
    Json result = Json::array();
    for (const auto &pos : path) result.push_back({{"row", pos.first}, {"col", pos.second}});
    return result;
}

/**
 * 功能：把局部束候选写成调试 JSON。
 * 输入：
 *   - candidates：当前轮四邻域候选。
 *   - selected：最终选中的候选方向编号；未执行时为 -1。
 * 输出：
 *   - 返回前端评分监控可读的候选表。
 * 关键逻辑：
 *   - 同时输出新字段和兼容字段，让前端可以展示 BundleScore，也能继续使用旧表格逻辑。
 */
Json candidatesJson(const std::vector<BundleEval> &candidates, int selected)
{
    Json rows = Json::array();
    for (const auto &candidate : candidates) {
        rows.push_back({{"target", {{"row", candidate.target.first}, {"col", candidate.target.second}}},
                        {"cell", candidate.cell},
                        {"cellValue", candidate.cellValue},
                        {"adjGoldCount", candidate.adjGoldCount},
                        {"bundleValue", candidate.bundleValue},
                        {"bundleLen", candidate.bundleLen},
                        {"bundleScore", candidate.bundleScore},
                        {"pathLen", candidate.bundleLen},
                        {"delta", candidate.bundleValue},
                        {"cleanup", candidate.adjGoldCount},
                        {"actionable", candidate.bundleValue > 0},
                        {"projectedResource", candidate.projectedResource},
                        {"projectedSteps", candidate.projectedSteps},
                        {"projectedRatio", candidate.projectedRatio},
                        {"score", candidate.bundleScore},
                        {"executedBundlePath", pathJson(candidate.bundlePath)},
                        {"selected", candidate.candidateIndex == selected}});
    }
    return rows;
}

/**
 * 功能：执行一段局部束路径并结算资源。
 * 输入：
 *   - path：本轮局部束路径，包含当前位置。
 *   - resources：所有 G/T 资源格。
 *   - fullPath：总路径输出参数。
 *   - mask：已触发资源集合，会在执行中更新。
 *   - resource：累计资源，会在执行中更新。
 *   - steps：累计步数，会在执行中更新。
 * 输出：
 *   - 返回本段实际新增资源。
 * 关键逻辑：
 *   - 从 path[1] 开始逐步移动；已触发的 G/T 重复经过时不再结算。
 */
int executeBundlePath(const std::vector<Position> &path,
                      const std::vector<ResourceCell> &resources,
                      std::vector<Position> &fullPath,
                      int &mask,
                      int &resource,
                      int &steps)
{
    int delta = 0;
    for (size_t i = 1; i < path.size(); ++i) {
        const Position pos = path[i];
        fullPath.push_back(pos);
        ++steps;
        const int index = resourceIndexAt(resources, pos);
        if (index >= 0 && !isTriggered(mask, index)) {
            mask |= 1 << index;
            resource += resources[index].value;
            delta += resources[index].value;
        }
    }
    return delta;
}

/**
 * 功能：根据最终路径重新生成前端播放帧。
 * 输入：
 *   - grid：3x3 网格。
 *   - path：贪心生成的路径坐标序列。
 *   - resources：资源格列表。
 * 输出：
 *   - 返回 frames 数组，并同步输出最终资源值和 G/T 触发次数。
 * 关键逻辑：
 *   - 重新沿路径结算一次资源，保证帧里的 delta、resource 和“G/T 只触发一次”规则一致。
 */
Json buildFrames(const std::vector<std::vector<std::string>> &grid,
                 const std::vector<Position> &path,
                 const std::vector<ResourceCell> &resources,
                 int &resource,
                 int &goldTriggers,
                 int &trapTriggers)
{
    Json frames = Json::array();
    int mask = 0;
    resource = 0;
    goldTriggers = 0;
    trapTriggers = 0;
    for (size_t step = 0; step < path.size(); ++step) {
        const auto [row, col] = path[step];
        int delta = 0;
        bool triggered = false;
        const int index = resourceIndexAt(resources, path[step]);
        if (index >= 0 && !isTriggered(mask, index)) {
            mask |= 1 << index;
            delta = resources[index].value;
            resource += delta;
            triggered = true;
            if (resources[index].tile == "G") ++goldTriggers;
            if (resources[index].tile == "T") ++trapTriggers;
        }
        frames.push_back({{"step", step},
                          {"row", row},
                          {"col", col},
                          {"tile", grid[row][col]},
                          {"delta", delta},
                          {"resource", resource},
                          {"triggered", triggered}});
    }
    return frames;
}

/**
 * 功能：把每轮资源贪心评分绑定到前端播放帧。
 * 输入：
 *   - frames：按路径步数生成的播放帧数组。
 *   - greedyRounds：每轮贪心决策记录，step 表示该轮决策发生时的累计步数。
 * 输出：
 *   - 无返回值，直接给对应 frame 添加 debug 字段。
 * 关键逻辑：
 *   - 前端评分监控按当前播放帧读取 frames[idx].debug，因此把轮级 reward 表挂到对应步数的帧上。
 */
void attachFrameDebug(Json &frames, const Json &greedyRounds)
{
    if (!frames.is_array() || frames.empty()) return;
    for (const auto &round : greedyRounds) {
        int step = round.value("step", 0);
        if (step < 0) step = 0;
        if (step >= static_cast<int>(frames.size())) step = static_cast<int>(frames.size()) - 1;
        Json debug = round;
        debug["resourcePickup"] = true;
        frames[step]["debug"] = debug;
    }
}
} // namespace

/**
 * 功能：求解 3x3 局部资源贪心任务。
 * 输入：
 *   - source：包含 3x3 grid 的 JSON。
 * 输出：
 *   - 返回路径、累计资源、路径长度、ratio、每轮 decision trace 和前端播放帧。
 * 关键逻辑：
 *   - 该算法是局部结构感知的简单贪心，不是 DP，不枚举所有路径，不保证任意 3x3 全局最优。
 *   - 每轮只计算四邻域局部束 reward，并用累计 ratio guard 防止继续拾取低性价比资源拉低总比值。
 */
Json solveResourcePickupJson(const Json &source)
{
    std::vector<std::vector<std::string>> grid;
    std::vector<ResourceCell> resources;
    Position start{kInvalid};
    parseResourceGrid(source, grid, start, resources);

    Position current = start;
    int mask = 0;
    int resource = 0;
    int steps = 0;
    std::string stopReason = "no valid move";
    std::vector<Position> fullPath{start};
    Json decisions = Json::array();

    while (true) {
        const double currentRatio = steps == 0 ? 0.0 : static_cast<double>(resource) / steps;
        std::vector<BundleEval> candidates;
        BundleEval best = chooseBestBundleGreedy(grid, current, resources, mask, candidates);

        const bool hasBest = best.valid;
        if (!hasBest) {
            stopReason = "no valid move";
        } else if (best.bundleValue <= 0) {
            stopReason = "no positive bundle";
        } else {
            best.projectedResource = resource + best.bundleValue;
            best.projectedSteps = steps + best.bundleLen;
            best.projectedRatio = best.projectedSteps == 0 ? 0.0 :
                static_cast<double>(best.projectedResource) / best.projectedSteps;
            const bool allowed = steps == 0 ? best.bundleValue > 0 : best.projectedRatio + kEps >= currentRatio;
            if (!allowed) stopReason = "ratio would decrease";
        }

        const bool allowedByRatioGuard = hasBest && best.bundleValue > 0 &&
            (steps == 0 || best.projectedRatio + kEps >= currentRatio);
        for (auto &candidate : candidates) {
            candidate.projectedResource = resource + candidate.bundleValue;
            candidate.projectedSteps = steps + candidate.bundleLen;
            candidate.projectedRatio = candidate.projectedSteps == 0 ? 0.0 :
                static_cast<double>(candidate.projectedResource) / candidate.projectedSteps;
        }

        decisions.push_back({{"step", steps},
                             {"position", {{"row", current.first}, {"col", current.second}}},
                             {"current", {{"row", current.first}, {"col", current.second}}},
                             {"resource", resource},
                             {"currentRatio", currentRatio},
                             {"currentRatioBefore", currentRatio},
                             {"decision", allowedByRatioGuard ? "move" : "stop"},
                             {"selected", allowedByRatioGuard ? best.candidateIndex : -1},
                             {"chosenTarget",
                              hasBest ? Json({{"row", best.target.first}, {"col", best.target.second}}) : Json(nullptr)},
                             {"deltaR", hasBest ? best.bundleValue : 0},
                             {"deltaL", hasBest ? best.bundleLen : 0},
                             {"projectedRatio", hasBest ? best.projectedRatio : currentRatio},
                             {"allowedByRatioGuard", allowedByRatioGuard},
                             {"executedBundlePath", allowedByRatioGuard ? pathJson(best.bundlePath) : Json::array()},
                             {"candidates", candidatesJson(candidates, allowedByRatioGuard ? best.candidateIndex : -1)}});

        if (!allowedByRatioGuard) break;

        executeBundlePath(best.bundlePath, resources, fullPath, mask, resource, steps);
        current = fullPath.back();
    }

    int finalResource = 0;
    int goldTriggers = 0;
    int trapTriggers = 0;
    Json frames = buildFrames(grid, fullPath, resources, finalResource, goldTriggers, trapTriggers);
    attachFrameDebug(frames, decisions);
    const int finalSteps = fullPath.empty() ? 0 : static_cast<int>(fullPath.size()) - 1;
    const double ratio = finalSteps == 0 ? 0.0 : static_cast<double>(finalResource) / finalSteps;

    Json result{{"ok", true},
                {"mode", "resource-pickup-3x3"},
                {"algorithm", "3x3 local bundle greedy with cumulative ratio guard"},
                {"greedy_formula",
                 "BundleValue(x)=cellValue(x)+50*adjGoldCount(x); BundleLen(x)=adjGoldCount(x)==0?1:2*adjGoldCount(x); BundleScore=BundleValue/BundleLen; execute only if projected cumulative R/L does not decrease"},
                {"case_id", source.value("case_id", 0)},
                {"grid", grid},
                {"start", {{"row", start.first}, {"col", start.second}}},
                {"end", pathJson(fullPath).empty() ? Json(nullptr) : pathJson(fullPath).back()},
                {"path", pathJson(fullPath)},
                {"frames", frames},
                {"events", Json::array()},
                {"greedy_rounds", decisions},
                {"decisions", decisions},
                {"stopReason", stopReason},
                {"resource", finalResource},
                {"totalResource", finalResource},
                {"steps", finalSteps},
                {"pathLength", finalSteps},
                {"score_ratio", ratio},
                {"ratio", ratio},
                {"average_resource_per_step", ratio},
                {"finished", true},
                {"gold_triggers", goldTriggers},
                {"trap_triggers", trapTriggers},
                {"resource_cell_count", resources.size()},
                {"complexity",
                 {{"time", "O(1) for fixed 3x3: each round checks at most 4 candidates and 4 neighbors"},
                  {"space", "O(1) for fixed 3x3"}}},
                {"limitations",
                 "local greedy only; no DP, no state compression, no all-path enumeration, no global optimality guarantee"},
                {"rules",
                 {{"endpoint", "stops when no positive bundle or cumulative ratio would decrease"},
                  {"trigger", "G/T trigger only once"},
                  {"moves", "up/right/down/left"},
                  {"objective", "local bundle reward with cumulative resource/path-length ratio guard"}}}};
    return result;
}

} // namespace ai_player
