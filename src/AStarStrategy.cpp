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
    const int rows = static_cast<int>(data.grid.size());
    const int cols = static_cast<int>(data.grid[0].size());
    std::vector dist(rows, std::vector<int>(cols, INT_MAX));
    std::vector parent(rows, std::vector<Position>(cols, kInvalid));
    auto heuristic = [&](Position pos) {
        return std::abs(pos.first - target.first) + std::abs(pos.second - target.second);
    };

    using Node = std::pair<int, Position>;
    std::priority_queue<Node, std::vector<Node>, std::greater<>> queue;
    dist[start.first][start.second] = 0;
    queue.push({heuristic(start), start});

    while (!queue.empty()) {
        const auto [priority, pos] = queue.top();
        queue.pop();
        (void)priority;
        if (pos == target) break;
        for (const auto [dr, dc] : kDirs) {
            const int nr = pos.first + dr;
            const int nc = pos.second + dc;
            if (!passable(data, nr, nc)) continue;
            const int nextDist = dist[pos.first][pos.second] + 1;
            if (nextDist < dist[nr][nc]) {
                dist[nr][nc] = nextDist;
                parent[nr][nc] = pos;
                queue.push({nextDist + heuristic({nr, nc}), {nr, nc}});
            }
        }
    }

    if (dist[target.first][target.second] == INT_MAX) {
        throw std::runtime_error("target is unreachable");
    }

    std::vector<Position> path;
    for (Position pos = target; pos != kInvalid; pos = parent[pos.first][pos.second]) {
        path.push_back(pos);
        if (pos == start) break;
    }
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
    if (!localMap.isWalkableForPlanning(start) || !localMap.isWalkableForPlanning(target)) return {};

    auto heuristic = [&](Position pos) {
        return std::abs(pos.first - target.first) + std::abs(pos.second - target.second);
    };
    std::map<Position, int> dist;
    std::map<Position, Position> parent;
    using Node = std::pair<int, Position>;
    std::priority_queue<Node, std::vector<Node>, std::greater<>> queue;
    dist[start] = 0;
    queue.push({heuristic(start), start});

    while (!queue.empty()) {
        const auto [priority, current] = queue.top();
        queue.pop();
        (void)priority;
        if (current == target) break;
        for (const auto [dr, dc] : kDirs) {
            const Position next{current.first + dr, current.second + dc};
            if (!localMap.isWalkableForPlanning(next)) continue;
            const int nextDist = dist[current] + 1;
            if (!dist.count(next) || nextDist < dist[next]) {
                dist[next] = nextDist;
                parent[next] = current;
                queue.push({nextDist + heuristic(next), next});
            }
        }
    }

    if (!dist.count(target)) return {};
    std::vector<Position> path;
    for (Position pos = target; pos != kInvalid; pos = parent.count(pos) ? parent[pos] : kInvalid) {
        path.push_back(pos);
        if (pos == start) break;
    }
    std::reverse(path.begin(), path.end());
    if (path.empty()) return {};
    return path.front() == start ? path : std::vector<Position>{};
}

} // namespace ai_player
