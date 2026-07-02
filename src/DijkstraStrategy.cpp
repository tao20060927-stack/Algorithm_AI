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
    // 提取迷宫尺寸。
    const int rows = static_cast<int>(data.grid.size());
    const int cols = static_cast<int>(data.grid[0].size());
    // dist[r][c] 记录起点到 (r,c) 的最短距离；初始化为 INT_MAX 表示无穷远。
    std::vector dist(rows, std::vector<int>(cols, INT_MAX));
    // parent[r][c] 记录最短路径树中 (r,c) 的父节点，用于最终回溯重建路径。
    std::vector parent(rows, std::vector<Position>(cols, kInvalid));

    // 优先队列元素为 {距离, 坐标}，按距离升序排列（最小堆）。
    // Dijkstra 算法等价于在无权网格上做 BFS，但使用优先队列保证每次扩展距离最小的节点。
    using Node = std::pair<int, Position>;
    std::priority_queue<Node, std::vector<Node>, std::greater<>> queue;
    dist[start.first][start.second] = 0;
    queue.push({0, start});

    while (!queue.empty()) {
        // 取出当前距离最小的未处理节点。
        const auto [currentDist, pos] = queue.top();
        queue.pop();
        // Lazy deletion：同一个格子可能以不同距离被多次压入队列。
        // 如果当前弹出的距离与 dist 表中记录的最短距离不一致，说明这是一条已过期的旧记录，直接跳过。
        if (currentDist != dist[pos.first][pos.second]) continue;
        // 当前出队节点恰好是目标，最短距离已经确定，无需继续搜索。
        if (pos == target) break;
        // 向四方向扩展邻居。
        for (const auto [dr, dc] : kDirs) {
            const int nr = pos.first + dr;
            const int nc = pos.second + dc;
            // 边界与障碍检查。
            if (!passable(data, nr, nc)) continue;
            // 到达邻居的新距离 = 当前节点距离 + 1（无权图每步代价为 1）。
            const int nextDist = currentDist + 1;
            // relax：如果找到一条更短的路径到达邻居，则更新距离和父节点。
            if (nextDist < dist[nr][nc]) {
                dist[nr][nc] = nextDist;
                parent[nr][nc] = pos;
                // 将邻居的新距离状态压入队列。旧状态（如果存在）将在稍后通过 lazy deletion 被跳过。
                queue.push({nextDist, {nr, nc}});
            }
        }
    }

    // 终点从未被更新过，说明从起点不可达。
    if (dist[target.first][target.second] == INT_MAX) {
        throw std::runtime_error("target is unreachable");
    }

    // 沿父节点链从 target 回溯到 start，记录经过的每个格子。
    std::vector<Position> path;
    for (Position pos = target; pos != kInvalid; pos = parent[pos.first][pos.second]) {
        path.push_back(pos);
        if (pos == start) break;
    }
    // 回溯得到的顺序是反的，反转后得到 start -> target 的正向路径。
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
    // 起点或终点在已知地图中不可通行时直接返回空路径。
    if (!localMap.isWalkableForPlanning(start) || !localMap.isWalkableForPlanning(target)) return {};

    // 局部已知地图是稀疏的，使用 std::map 而不是完整二维数组来存储距离。
    // 未在 map 中的格子视为从未访问（距离无穷大）。
    std::map<Position, int> dist;
    // 父节点表同样使用 map，只在格子被首次发现或更新时写入。
    std::map<Position, Position> parent;
    using Node = std::pair<int, Position>;
    std::priority_queue<Node, std::vector<Node>, std::greater<>> queue;
    dist[start] = 0;
    queue.push({0, start});

    while (!queue.empty()) {
        // 取出距离最小的节点。
        const auto [currentDist, current] = queue.top();
        queue.pop();
        // Lazy deletion：跳过已过期的旧记录（当前弹出距离与已知最短距离不一致）。
        if (dist[current] != currentDist) continue;
        // 到达目标，最短路径已确定。
        if (current == target) break;
        // 向四方向扩展。
        for (const auto [dr, dc] : kDirs) {
            const Position next{current.first + dr, current.second + dc};
            // 邻居必须在已观察且可通行的区域内。
            if (!localMap.isWalkableForPlanning(next)) continue;
            const int nextDist = currentDist + 1;
            // 如果邻居首次被访问或找到了更短路径，则更新。
            if (!dist.count(next) || nextDist < dist[next]) {
                dist[next] = nextDist;
                parent[next] = current;
                queue.push({nextDist, next});
            }
        }
    }

    // target 不在 dist 中，说明从 start 在已知地图范围内不可达。
    if (!dist.count(target)) return {};
    // 沿父节点链从 target 回溯到 start 重建路径。
    std::vector<Position> path;
    for (Position pos = target; pos != kInvalid; pos = parent.count(pos) ? parent[pos] : kInvalid) {
        path.push_back(pos);
        if (pos == start) break;
    }
    std::reverse(path.begin(), path.end());
    // 安全检查：确保返回的路径非空且起点正确。
    if (path.empty()) return {};
    return path.front() == start ? path : std::vector<Position>{};
}

} // namespace ai_player
