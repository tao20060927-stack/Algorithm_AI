#include "ShortestPathStrategy.h"

#include <algorithm>
#include <set>
#include <stdexcept>

#include "AStarStrategy.h"
#include "BranchBoundStrategy.h"
#include "DijkstraStrategy.h"
#include "DivideConquerStrategy.h"

namespace ai_player {

/**
 * 功能：根据算法名称调用对应的独立路径搜索文件。
 * 输入：
 *   - data：迷宫数据。
 *   - start：路径起点。
 *   - target：路径终点。
 *   - algorithm：算法名称，支持 smart、dijkstra、astar、branch_bound、divide_conquer。
 * 输出：
 *   - 返回 start 到 target 的路径。
 * 关键逻辑：
 *   - 该函数只做调度，不承载具体算法实现；每种算法的核心逻辑放在自己的文件中。
 */
std::vector<Position> shortestPath(const MazeData &data, Position start, Position target, const std::string &algorithm)
{
    if (algorithm == "dijkstra") return dijkstraPath(data, start, target);
    if (algorithm == "astar" || algorithm == "smart") return astarPath(data, start, target);
    if (algorithm == "branch_bound") return branchBoundPath(data, start, target);
    if (algorithm == "divide_conquer") return divideConquerPath(data, start, target);
    throw std::runtime_error("unsupported algorithm: " + algorithm);
}

/**
 * 功能：规划完整探险路径。
 * 输入：
 *   - data：迷宫数据。
 *   - algorithm：用于子路径搜索的算法名称。
 * 输出：
 *   - 返回完整探险路径；路由型算法传入时抛出异常。
 * 关键逻辑：
 *   - smart 是完整探险入口。
 *   - 分治、分支限界、Dijkstra 和 A* 只作为 reward 目标选择后的两点路由器，不能在这里做完整探索。
 */
std::vector<Position> planAdventurePath(const MazeData &data, const std::string &algorithm)
{
    if (algorithm == "dijkstra" || algorithm == "astar" || algorithm == "branch_bound" ||
        algorithm == "divide_conquer") {
        throw std::runtime_error("routing algorithm is not a standalone adventure planner: " + algorithm);
    }

    MazeData planningData = data;
    std::set<Position> remaining;
    for (const auto &pos : planningData.golds) remaining.insert(pos);
    std::vector<bool> bossTriggered(planningData.bosses.size(), false);

    Position current = planningData.start;
    std::vector<Position> fullPath{current};
    while (!remaining.empty() || std::any_of(bossTriggered.begin(), bossTriggered.end(), [](bool done) { return !done; })) {
        Position best = kInvalid;
        std::vector<Position> bestPath;
        int bestBossIndex = -1;
        for (const auto &target : remaining) {
            try {
                auto candidate = shortestPath(planningData, current, target, algorithm);
                if (best == kInvalid || candidate.size() < bestPath.size()) {
                    best = target;
                    bestPath = std::move(candidate);
                    bestBossIndex = -1;
                }
            } catch (const std::exception &) {
            }
        }
        for (int i = 0; i < static_cast<int>(planningData.bosses.size()); ++i) {
            if (bossTriggered[i]) continue;
            for (const auto [dr, dc] : kDirs) {
                Position trigger{planningData.bosses[i].first + dr, planningData.bosses[i].second + dc};
                if (!passable(planningData, trigger.first, trigger.second)) continue;
                try {
                    auto candidate = shortestPath(planningData, current, trigger, algorithm);
                    if (best == kInvalid || candidate.size() < bestPath.size()) {
                        best = trigger;
                        bestPath = std::move(candidate);
                        bestBossIndex = i;
                    }
                } catch (const std::exception &) {
                }
            }
        }
        if (best == kInvalid) break;
        fullPath.insert(fullPath.end(), bestPath.begin() + 1, bestPath.end());
        current = best;
        if (bestBossIndex >= 0) {
            bossTriggered[bestBossIndex] = true;
            const auto [row, col] = planningData.bosses[bestBossIndex];
            planningData.grid[row][col] = " ";
        } else {
            remaining.erase(best);
        }
    }

    auto exitPath = shortestPath(planningData, current, planningData.exit, algorithm);
    fullPath.insert(fullPath.end(), exitPath.begin() + 1, exitPath.end());
    return fullPath;
}
} // namespace ai_player
