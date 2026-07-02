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
    // 算法调度器：根据算法名称字符串，将请求路由到对应的底层路径搜索实现。
    // "smart" 复用 A* 作为底层路由，因为 smart 的 reward 层负责目标选择，
    // 目标确定后的两点路由与 astar 一致。
    if (algorithm == "dijkstra") return dijkstraPath(data, start, target);
    if (algorithm == "astar" || algorithm == "smart") return astarPath(data, start, target);
    if (algorithm == "branch_bound") return branchBoundPath(data, start, target);
    if (algorithm == "divide_conquer") return divideConquerPath(data, start, target);
    // 未知算法名称直接抛异常，不尝试回退。
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
    // 路由型算法只做两点间的路径搜索，不支持完整探险规划。
    // 它们没有 reward/target 选择逻辑，作为完整探险入口会陷入死循环或不合理路径。
    if (algorithm == "dijkstra" || algorithm == "astar" || algorithm == "branch_bound" ||
        algorithm == "divide_conquer") {
        throw std::runtime_error("routing algorithm is not a standalone adventure planner: " + algorithm);
    }

    // 复制一份迷宫数据用于规划，后续击败 Boss 后会修改 grid（将 Boss 格变为空地）。
    MazeData planningData = data;
    // remaining 记录尚未收集的金币位置集合。
    std::set<Position> remaining;
    for (const auto &pos : planningData.golds) remaining.insert(pos);
    // bossTriggered[i] 表示第 i 个 Boss 是否已被触发（击败）。
    std::vector<bool> bossTriggered(planningData.bosses.size(), false);

    // current 表示当前所在位置，初始化为起点。
    Position current = planningData.start;
    // fullPath 累积完整的探险路径序列。
    std::vector<Position> fullPath{current};
    // 贪心最近邻循环：每次迭代找离当前位置最近的金币或 Boss 触发点。
    while (!remaining.empty() || std::any_of(bossTriggered.begin(), bossTriggered.end(), [](bool done) { return !done; })) {
        Position best = kInvalid;
        std::vector<Position> bestPath;
        int bestBossIndex = -1; // -1 表示 best 是金币，>=0 表示 best 是 Boss 触发点。
        // 遍历所有剩余金币，找到距离最短的那个。
        for (const auto &target : remaining) {
            try {
                auto candidate = shortestPath(planningData, current, target, algorithm);
                // 如果这是第一个有效候选，或者找到了更短的路径，则更新最优选择。
                if (best == kInvalid || candidate.size() < bestPath.size()) {
                    best = target;
                    bestPath = std::move(candidate);
                    bestBossIndex = -1; // 目标是金币。
                }
            } catch (const std::exception &) {
                // 某些金币可能因为迷宫结构在当前状态下不可达（例如被 Boss 挡住）。
                // 不可达的金币跳过，后续击败 Boss 打通道路后可能在下一轮变得可达。
            }
        }
        // 遍历所有未触发的 Boss，找到最近的 Boss 相邻触发点。
        for (int i = 0; i < static_cast<int>(planningData.bosses.size()); ++i) {
            if (bossTriggered[i]) continue; // 已触发的 Boss 跳过。
            // Boss 本体不可通行，需要走到 Boss 的四邻格才能触发战斗。
            for (const auto [dr, dc] : kDirs) {
                Position trigger{planningData.bosses[i].first + dr, planningData.bosses[i].second + dc};
                // 触发点必须可通行。
                if (!passable(planningData, trigger.first, trigger.second)) continue;
                try {
                    auto candidate = shortestPath(planningData, current, trigger, algorithm);
                    if (best == kInvalid || candidate.size() < bestPath.size()) {
                        best = trigger;
                        bestPath = std::move(candidate);
                        bestBossIndex = i; // 目标是 Boss 触发点。
                    }
                } catch (const std::exception &) {
                    // 当前状态下不可达的触发点跳过。
                }
            }
        }
        // 如果本轮没有找到任何可达的金币或 Boss 触发点，说明能走的都走完了。
        if (best == kInvalid) break;
        // 将当前步骤的路径追加到完整路径中（跳过起点，避免重复）。
        fullPath.insert(fullPath.end(), bestPath.begin() + 1, bestPath.end());
        current = best; // 更新当前位置。
        if (bestBossIndex >= 0) {
            // 标记 Boss 为已触发，并将其所占格子改为空地" "，
            // 这样后续搜索中 Boss 本体格变为可通行，可以穿过它去收集后面的金币。
            bossTriggered[bestBossIndex] = true;
            const auto [row, col] = planningData.bosses[bestBossIndex];
            planningData.grid[row][col] = " ";
        } else {
            // 收集金币后将其从待收集集合中移除。
            remaining.erase(best);
        }
    }

    // 所有金币收集完毕且 Boss 全部触发后，规划从当前位置到出口的路径。
    auto exitPath = shortestPath(planningData, current, planningData.exit, algorithm);
    // 将出口路径追加到完整路径中（跳过当前位置的重复）。
    fullPath.insert(fullPath.end(), exitPath.begin() + 1, exitPath.end());
    return fullPath;
}
} // namespace ai_player
