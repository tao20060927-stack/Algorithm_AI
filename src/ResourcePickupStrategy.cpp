#include "ResourcePickupStrategy.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <queue>
#include <stdexcept>
#include <string>
#include <vector>

namespace ai_player {
namespace {
constexpr std::array<Position, 4> kPickupDirs{{{0, 1}, {1, 0}, {-1, 0}, {0, -1}}};

struct ResourceCell {
    Position pos;
    int value;
    std::string tile;
};

struct GreedyCandidate {
    int candidateIndex = -1;
    Position target{kInvalid};
    std::vector<Position> path;
    int delta = 0;
    int cleanup = 0;
    int nextMask = 0;
    int projectedResource = 0;
    int projectedSteps = 0;
    bool actionable = false;
    double score = -std::numeric_limits<double>::infinity();
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
 *   - 只接受 PDF 第一问需要的 P、G、T、. 和可选墙 #，保证贪心选择的输入含义明确。
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
 * 功能：枚举两点之间的所有最短可行路径。
 * 输入：
 *   - grid：3x3 地图，# 表示不可通行。
 *   - start：当前玩家坐标。
 *   - target：候选资源格坐标。
 * 输出：
 *   - 返回所有包含 start 和 target 的最短路径；不可达时返回空数组。
 * 关键逻辑：
 *   - 先用 BFS 计算最短距离，再只沿距离递增的边回溯所有最短路径。
 */
std::vector<std::vector<Position>> allShortestPaths(const std::vector<std::vector<std::string>> &grid,
                                                    Position start,
                                                    Position target)
{
    std::vector distance(3, std::vector<int>(3, -1));
    std::queue<Position> queue;
    queue.push(start);
    distance[start.first][start.second] = 0;

    while (!queue.empty()) {
        const auto [row, col] = queue.front();
        queue.pop();
        for (const auto [dr, dc] : kPickupDirs) {
            const int nr = row + dr;
            const int nc = col + dc;
            if (nr < 0 || nc < 0 || nr >= 3 || nc >= 3 || distance[nr][nc] != -1 || grid[nr][nc] == "#") {
                continue;
            }
            distance[nr][nc] = distance[row][col] + 1;
            queue.push({nr, nc});
        }
    }

    if (distance[target.first][target.second] < 0) return {};

    std::vector<std::vector<Position>> paths;
    std::vector<Position> path{start};
    auto dfs = [&](auto &&self, Position current) -> void {
        if (current == target) {
            paths.push_back(path);
            return;
        }
        const auto [row, col] = current;
        for (const auto [dr, dc] : kPickupDirs) {
            const int nr = row + dr;
            const int nc = col + dc;
            if (nr < 0 || nc < 0 || nr >= 3 || nc >= 3 || grid[nr][nc] == "#") continue;
            if (distance[nr][nc] != distance[row][col] + 1) continue;
            path.push_back({nr, nc});
            self(self, {nr, nc});
            path.pop_back();
        }
    };
    dfs(dfs, start);
    return paths;
}

/**
 * 功能：判断某个资源格是否已经触发。
 * 输入：
 *   - mask：已触发资源集合。
 *   - index：资源格编号。
 * 输出：
 *   - 返回该编号对应的资源格是否已被触发。
 * 关键逻辑：
 *   - 用位集合记录 G/T 是否结算，保证重复经过同一资源格时不会重复加分或扣分。
 */
bool isTriggered(int mask, int index)
{
    return (mask & (1 << index)) != 0;
}

/**
 * 功能：模拟沿候选路径移动后的资源变化。
 * 输入：
 *   - path：从当前位置到候选目标的路径。
 *   - resources：所有资源格。
 *   - mask：进入路径前已经触发的资源集合。
 * 输出：
 *   - 通过 nextMask 返回移动后的触发集合，函数返回本段路径新增资源值。
 * 关键逻辑：
 *   - 路径中途经过的 G/T 也会被触发；这符合“走到资源格就结算”的规则。
 */
int pathDelta(const std::vector<Position> &path,
              const std::vector<ResourceCell> &resources,
              int mask,
              int &nextMask)
{
    int delta = 0;
    nextMask = mask;
    for (size_t step = 1; step < path.size(); ++step) {
        for (size_t i = 0; i < resources.size(); ++i) {
            if (resources[i].pos == path[step] && !isTriggered(nextMask, static_cast<int>(i))) {
                nextMask |= 1 << i;
                delta += resources[i].value;
                break;
            }
        }
    }
    return delta;
}

/**
 * 功能：统计陷阱旁边相邻的金币数量。
 * 输入：
 *   - grid：3x3 输入地图。
 *   - trap：陷阱格坐标。
 * 输出：
 *   - 返回上下左右相邻格中 G 的数量。
 * 关键逻辑：
 *   - 清理度只作为第二优先级；陷阱周围金币越多，说明这条路径清理了越关键的风险点。
 */
int adjacentGoldCount(const std::vector<std::vector<std::string>> &grid, Position trap)
{
    int count = 0;
    for (const auto [dr, dc] : kDirs) {
        const int row = trap.first + dr;
        const int col = trap.second + dc;
        if (row >= 0 && col >= 0 && row < 3 && col < 3 && grid[row][col] == "G") ++count;
    }
    return count;
}

/**
 * 功能：计算一条路径的清理度。
 * 输入：
 *   - grid：3x3 输入地图。
 *   - path：当前候选最短路径。
 *   - resources：所有 G/T 资源格。
 *   - mask：进入路径前已经触发的资源集合。
 * 输出：
 *   - 返回路径中新触发陷阱周围相邻金币数量之和。
 * 关键逻辑：
 *   - 只统计新触发的 T；重复经过已触发陷阱不再增加清理度，避免重复奖励。
 */
int cleanupScore(const std::vector<std::vector<std::string>> &grid,
                 const std::vector<Position> &path,
                 const std::vector<ResourceCell> &resources,
                 int mask)
{
    int score = 0;
    int localMask = mask;
    for (size_t step = 1; step < path.size(); ++step) {
        for (size_t i = 0; i < resources.size(); ++i) {
            if (resources[i].pos != path[step] || isTriggered(localMask, static_cast<int>(i))) continue;
            localMask |= 1 << i;
            if (resources[i].tile == "T") score += adjacentGoldCount(grid, path[step]);
            break;
        }
    }
    return score;
}

/**
 * 功能：比较两个贪心候选目标。
 * 输入：
 *   - best：当前最优候选。
 *   - candidate：待比较候选。
 * 输出：
 *   - 如果 candidate 应成为新的最优候选，返回 true。
 * 关键逻辑：
 *   - 主准则是投影后的总资源/总步数比值；并列时优先清理度，再优先新增资源更高。
 */
bool betterCandidate(const GreedyCandidate &best, const GreedyCandidate &candidate)
{
    constexpr double kEps = 1e-12;
    if (candidate.score > best.score + kEps) return true;
    if (std::abs(candidate.score - best.score) <= kEps && candidate.cleanup != best.cleanup) {
        return candidate.cleanup > best.cleanup;
    }
    if (std::abs(candidate.score - best.score) <= kEps && candidate.delta != best.delta) {
        return candidate.delta > best.delta;
    }
    if (std::abs(candidate.score - best.score) <= kEps && candidate.delta == best.delta) {
        if (candidate.path.size() != best.path.size()) return candidate.path.size() < best.path.size();
        if (candidate.target.first != best.target.first) return candidate.target.first < best.target.first;
        return candidate.target.second < best.target.second;
    }
    return false;
}

/**
 * 功能：把当前轮所有候选写成调试 JSON。
 * 输入：
 *   - candidates：当前轮每个未触发资源格的贪心评分。
 *   - selected：最终选中的候选资源编号。
 * 输出：
 *   - 返回前端和测试可读的候选表。
 * 关键逻辑：
 *   - 暴露 delta、projectedResource、projectedSteps 和 score，方便核对贪心为何继续或停止。
 */
Json candidatesJson(const std::vector<GreedyCandidate> &candidates, int selected)
{
    Json rows = Json::array();
    for (const auto &candidate : candidates) {
        Json path = Json::array();
        for (const auto &pos : candidate.path) path.push_back({{"row", pos.first}, {"col", pos.second}});
        rows.push_back({{"target", {{"row", candidate.target.first}, {"col", candidate.target.second}}},
                        {"pathLen", candidate.path.empty() ? 0 : static_cast<int>(candidate.path.size()) - 1},
                        {"delta", candidate.delta},
                        {"cleanup", candidate.cleanup},
                        {"actionable", candidate.actionable},
                        {"projectedResource", candidate.projectedResource},
                        {"projectedSteps", candidate.projectedSteps},
                        {"score", candidate.score},
                        {"path", path},
                        {"selected", candidate.candidateIndex == selected}});
    }
    return rows;
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
        for (size_t i = 0; i < resources.size(); ++i) {
            if (resources[i].pos == Position{row, col} && !isTriggered(mask, static_cast<int>(i))) {
                mask |= 1 << i;
                delta = resources[i].value;
                resource += delta;
                triggered = true;
                if (resources[i].tile == "G") ++goldTriggers;
                if (resources[i].tile == "T") ++trapTriggers;
                break;
            }
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
} // namespace

Json solveResourcePickupJson(const Json &source)
{
    std::vector<std::vector<std::string>> grid;
    std::vector<ResourceCell> resources;
    Position start{kInvalid};
    parseResourceGrid(source, grid, start, resources);
    if (resources.size() > 20) {
        throw std::runtime_error("too many resource cells");
    }

    Position current = start;
    int mask = 0;
    int resource = 0;
    int steps = 0;
    std::vector<Position> fullPath{start};
    Json greedyRounds = Json::array();

    while (true) {
        const double currentRatio = steps == 0 ? 0.0 : static_cast<double>(resource) / steps;
        std::vector<GreedyCandidate> candidates;
        GreedyCandidate best;

        int candidateIndex = 0;
        for (int row = 0; row < 3; ++row) {
            for (int col = 0; col < 3; ++col) {
                const Position target{row, col};
                if (target == current || grid[row][col] == "#") continue;

                GreedyCandidate cellBest;
                cellBest.candidateIndex = candidateIndex++;
                cellBest.target = target;
                for (const auto &path : allShortestPaths(grid, current, target)) {
                    if (path.size() <= 1) continue;
                    int nextMask = mask;
                    const int delta = pathDelta(path, resources, mask, nextMask);
                    const int projectedSteps = steps + static_cast<int>(path.size()) - 1;
                    const int projectedResource = resource + delta;
                    const double score = projectedSteps == 0 ? 0.0 : static_cast<double>(projectedResource) / projectedSteps;
                    GreedyCandidate pathCandidate{cellBest.candidateIndex,
                                                  target,
                                                  path,
                                                  delta,
                                                  cleanupScore(grid, path, resources, mask),
                                                  nextMask,
                                                  projectedResource,
                                                  projectedSteps,
                                                  nextMask != mask,
                                                  score};
                    if (betterCandidate(cellBest, pathCandidate)) cellBest = pathCandidate;
                }
                candidates.push_back(cellBest);
                if (betterCandidate(best, cellBest)) best = cellBest;
            }
        }

        const bool canImprove = best.candidateIndex >= 0 && best.actionable && best.score + 1e-12 >= currentRatio;
        greedyRounds.push_back({{"step", steps},
                                {"current", {{"row", current.first}, {"col", current.second}}},
                                {"resource", resource},
                                {"currentRatio", currentRatio},
                                {"decision", canImprove ? "move" : "stop"},
                                {"selected", best.candidateIndex},
                                {"candidates", candidatesJson(candidates, canImprove ? best.candidateIndex : -1)}});
        if (!canImprove) break;

        for (size_t i = 1; i < best.path.size(); ++i) fullPath.push_back(best.path[i]);
        current = best.target;
        mask = best.nextMask;
        resource += best.delta;
        steps += static_cast<int>(best.path.size()) - 1;
    }

    Json pathJson = Json::array();
    for (const auto &pos : fullPath) {
        pathJson.push_back({{"row", pos.first}, {"col", pos.second}});
    }

    int finalResource = 0;
    int goldTriggers = 0;
    int trapTriggers = 0;
    Json frames = buildFrames(grid, fullPath, resources, finalResource, goldTriggers, trapTriggers);
    const int finalSteps = fullPath.empty() ? 0 : static_cast<int>(fullPath.size()) - 1;
    const double ratio = finalSteps == 0 ? 0.0 : static_cast<double>(finalResource) / finalSteps;

    Json result{{"ok", true},
                {"mode", "resource-pickup-3x3"},
                {"algorithm", "greedy best-R/L among all shortest paths"},
                {"greedy_formula", "for each other cell t, reward(t)=max over shortest paths p from current to t of (R+DeltaR(p))/(L+len(p)); tie by cleanup score; move if selected reward>=current R/L and path triggers new resource"},
                {"case_id", source.value("case_id", 0)},
                {"grid", grid},
                {"start", {{"row", start.first}, {"col", start.second}}},
                {"end", pathJson.empty() ? Json(nullptr) : pathJson.back()},
                {"path", pathJson},
                {"frames", frames},
                {"events", Json::array()},
                {"greedy_rounds", greedyRounds},
                {"resource", finalResource},
                {"steps", finalSteps},
                {"score_ratio", ratio},
                {"average_resource_per_step", ratio},
                {"finished", true},
                {"gold_triggers", goldTriggers},
                {"trap_triggers", trapTriggers},
                {"resource_cell_count", resources.size()},
                {"rules", {{"endpoint", "arbitrary"},
                           {"trigger", "G/T trigger only once"},
                           {"moves", "up/down/left/right"},
                           {"objective", "greedily improve resource / path length; stopping is allowed"}}}};
    return result;
}

} // namespace ai_player
