#include "DijkstraStrategy.h"

#include <algorithm>
#include <climits>
#include <functional>
#include <map>
#include <queue>
#include <stdexcept>

#include "Reward.h"

namespace ai_player {

/**
 * 功能：使用普通 Dijkstra 算法在迷宫中求两点最短路径。
 * 输入：
 *   - data：迷宫数据，墙和未击败 Boss 本体不可通行。
 *   - start：路径起点。
 *   - target：路径终点。
 * 输出：
 *   - 返回从 start 到 target 的最短路径；不可达时抛出异常。
 * 关键逻辑：
 *   - 该函数只作为其它算法可复用的底层两点路径工具，不再作为 Dijkstra 完整探险主策略。
 */
std::vector<Position> dijkstraPath(const MazeData &data, Position start, Position target)
{
    const int rows = static_cast<int>(data.grid.size());
    const int cols = static_cast<int>(data.grid[0].size());
    std::vector dist(rows, std::vector<int>(cols, INT_MAX));
    std::vector parent(rows, std::vector<Position>(cols, kInvalid));

    using Node = std::pair<int, Position>;
    std::priority_queue<Node, std::vector<Node>, std::greater<>> queue;
    dist[start.first][start.second] = 0;
    queue.push({0, start});

    while (!queue.empty()) {
        const auto [currentDist, pos] = queue.top();
        queue.pop();
        if (currentDist != dist[pos.first][pos.second]) continue;
        if (pos == target) break;
        for (const auto [dr, dc] : kDirs) {
            const int nr = pos.first + dr;
            const int nc = pos.second + dc;
            if (!passable(data, nr, nc)) continue;
            const int nextDist = currentDist + 1;
            if (nextDist < dist[nr][nc]) {
                dist[nr][nc] = nextDist;
                parent[nr][nc] = pos;
                queue.push({nextDist, {nr, nc}});
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
 * 功能：使用 Dijkstra 在局部已知地图中求两点路径。
 * 输入：
 *   - localMap：AI 当前通过 3x3 视野维护出的已知地图。
 *   - start：局部路径起点，必须是已观察可通行格。
 *   - target：局部路径终点，必须是已观察可通行格。
 * 输出：
 *   - 返回 start 到 target 的最短局部路径；不可达时返回空路径。
 * 关键逻辑：
 *   - 该函数只做路由，不选择探索目标；目标由 reward 贪心层决定。
 *   - 搜索图只包含 localMap 中已观察、非墙格子，避免偷看完整迷宫。
 */
std::vector<Position> dijkstraPath(const LocalKnownMap &localMap, Position start, Position target)
{
    if (!localMap.isWalkableForPlanning(start) || !localMap.isWalkableForPlanning(target)) return {};

    std::map<Position, int> dist;
    std::map<Position, Position> parent;
    using Node = std::pair<int, Position>;
    std::priority_queue<Node, std::vector<Node>, std::greater<>> queue;
    dist[start] = 0;
    queue.push({0, start});

    while (!queue.empty()) {
        const auto [currentDist, current] = queue.top();
        queue.pop();
        if (dist[current] != currentDist) continue;
        if (current == target) break;
        for (const auto [dr, dc] : kDirs) {
            const Position next{current.first + dr, current.second + dc};
            if (!localMap.isWalkableForPlanning(next)) continue;
            const int nextDist = currentDist + 1;
            if (!dist.count(next) || nextDist < dist[next]) {
                dist[next] = nextDist;
                parent[next] = current;
                queue.push({nextDist, next});
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
