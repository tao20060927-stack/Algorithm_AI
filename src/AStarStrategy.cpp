#include "AStarStrategy.h"

#include <algorithm>
#include <climits>
#include <cstdlib>
#include <functional>
#include <map>
#include <queue>
#include <stdexcept>

#include "Reward.h"

namespace ai_player {

/**
 * 功能：使用 A* 算法在迷宫中求最短路径。
 * 输入：
 *   - data：迷宫数据，墙和未击败 Boss 本体不可通行。
 *   - start：路径起点。
 *   - target：路径终点。
 * 输出：
 *   - 返回从 start 到 target 的最短路径；不可达时抛出异常。
 * 关键逻辑：
 *   - 使用曼哈顿距离作为启发函数 h，优先扩展 g+h 较小的格子。
 */
std::vector<Position> astarPath(const MazeData &data, Position start, Position target)
{
    // 提取迷宫行列数，用于初始化二维距离表和父节点表。
    const int rows = static_cast<int>(data.grid.size());
    const int cols = static_cast<int>(data.grid[0].size());
    // dist[r][c] 记录从起点到 (r,c) 的最短已知距离，初始化为 INT_MAX 表示未访问。
    std::vector dist(rows, std::vector<int>(cols, INT_MAX));
    // parent[r][c] 记录在最短路径上到达 (r,c) 的前驱格子，用于最终路径重建。
    std::vector parent(rows, std::vector<Position>(cols, kInvalid));
    // 启发函数 h：使用曼哈顿距离作为从当前格到目标的"直线距离"下界。
    // 曼哈顿距离在四方向网格中满足可采纳性（admissible），保证 A* 能找到最优解。
    auto heuristic = [&](Position pos) {
        return std::abs(pos.first - target.first) + std::abs(pos.second - target.second);
    };

    // 优先队列存储 {f值, 格子坐标}，f = g + h。
    // std::greater 使队列按 f 值从小到大弹出（最小堆行为）。
    using Node = std::pair<int, Position>;
    std::priority_queue<Node, std::vector<Node>, std::greater<>> queue;
    // 起点 g 值为 0，初始 f = 0 + h(start)。
    dist[start.first][start.second] = 0;
    queue.push({heuristic(start), start});

    while (!queue.empty()) {
        // 从优先队列中取出当前 f 值最小的节点进行扩展。
        const auto [priority, pos] = queue.top();
        queue.pop();
        // priority 仅用于排序，扩展时只依赖 dist[pos] 记录的 g 值。
        // 这里不使用 lazy deletion（即不检查 priority != dist + h），
        // 因为同一格子的旧条目只会携带更大的 g 值，而后续 relax 时的
        // nextDist < dist 检查已经能过滤掉更差的路径。
        (void)priority;
        // 当前弹出节点恰好是目标，最短路径已经找到，停止搜索。
        if (pos == target) break;
        // 在四方向网格中扩展邻居。
        for (const auto [dr, dc] : kDirs) {
            const int nr = pos.first + dr;
            const int nc = pos.second + dc;
            // 边界检查与可通行性检查：墙和未击败 Boss 本体不可通行。
            if (!passable(data, nr, nc)) continue;
            // 从 pos 走到邻居的代价（无权网格中每步代价为 1）。
            const int nextDist = dist[pos.first][pos.second] + 1;
            // relax：若找到一条比已知更短的路径到达邻居，则更新距离并记录父节点。
            if (nextDist < dist[nr][nc]) {
                dist[nr][nc] = nextDist;
                parent[nr][nc] = pos;
                // 将邻居按 f = g + h 压入优先队列，等待后续扩展。
                queue.push({nextDist + heuristic({nr, nc}), {nr, nc}});
            }
        }
    }

    // 如果终点的距离仍为 INT_MAX，说明从起点出发无法到达目标格子。
    if (dist[target.first][target.second] == INT_MAX) {
        throw std::runtime_error("target is unreachable");
    }

    // 从 target 沿父节点链回溯到 start，重建完整路径。
    std::vector<Position> path;
    for (Position pos = target; pos != kInvalid; pos = parent[pos.first][pos.second]) {
        path.push_back(pos);
        if (pos == start) break;
    }
    // 回溯得到的是 target -> start 的逆序，反转后得到 start -> target。
    std::reverse(path.begin(), path.end());
    return path;
}

/**
 * 功能：使用 A* 在局部已知地图中求两点路径。
 * 输入：
 *   - localMap：AI 当前通过 3x3 视野维护出的已知地图。
 *   - start：局部路径起点，必须是已观察可通行格。
 *   - target：局部路径终点，必须是已观察可通行格。
 * 输出：
 *   - 返回 start 到 target 的局部路径；不可达时返回空路径。
 * 关键逻辑：
 *   - reward 层先选目标，本函数只负责用 g+h 路由到该目标。
 *   - h 使用曼哈顿距离，搜索范围限制在已知可通行格，保证不是完整迷宫探索器。
 */
std::vector<Position> astarPath(const LocalKnownMap &localMap, Position start, Position target)
{
    // 起点或终点在已知地图中不可通行时立刻返回空路径，避免无效搜索。
    if (!localMap.isWalkableForPlanning(start) || !localMap.isWalkableForPlanning(target)) return {};

    // 同全局版本，使用曼哈顿距离作为启发函数 h。
    auto heuristic = [&](Position pos) {
        return std::abs(pos.first - target.first) + std::abs(pos.second - target.second);
    };
    // 局部已知地图中的格子是稀疏的，因此用 std::map 代替二维 vector 存储距离。
    std::map<Position, int> dist;
    // 同样用 map 存储父节点，未在 map 中的格子视为从未被访问。
    std::map<Position, Position> parent;
    using Node = std::pair<int, Position>;
    std::priority_queue<Node, std::vector<Node>, std::greater<>> queue;
    // 起点 g 值为 0，f = 0 + h(start)。
    dist[start] = 0;
    queue.push({heuristic(start), start});

    while (!queue.empty()) {
        // 取出 f 值最小的节点。
        const auto [priority, current] = queue.top();
        queue.pop();
        (void)priority;
        // 到达目标，搜索完成。
        if (current == target) break;
        // 在四方向上扩展邻居。
        for (const auto [dr, dc] : kDirs) {
            const Position next{current.first + dr, current.second + dc};
            // 邻居必须在已知可通行格中（已观察且非墙）。
            if (!localMap.isWalkableForPlanning(next)) continue;
            const int nextDist = dist[current] + 1;
            // 若邻居首次被访问或找到更短路径，则更新距离和父节点。
            // dist.count(next) 为 false 说明该格子从未被扩展过。
            if (!dist.count(next) || nextDist < dist[next]) {
                dist[next] = nextDist;
                parent[next] = current;
                queue.push({nextDist + heuristic(next), next});
            }
        }
    }

    // 如果 target 从未进入 dist map，说明在已知地图内无法到达。
    if (!dist.count(target)) return {};
    // 从 target 沿父节点链回溯到 start，重建路径。
    std::vector<Position> path;
    for (Position pos = target; pos != kInvalid; pos = parent.count(pos) ? parent[pos] : kInvalid) {
        path.push_back(pos);
        if (pos == start) break;
    }
    std::reverse(path.begin(), path.end());
    // 安全检查：空路径或起点不一致时返回空路径，避免调用方拿到错误数据。
    if (path.empty()) return {};
    return path.front() == start ? path : std::vector<Position>{};
}

} // namespace ai_player
